// Author: Antonio Lattanzio - emptyvessel

#include "Box3DBakeCommandlet.h"
#include "Box3DBodyComponent.h"
#include "Box3DCollisionData.h"
#include "Box3DStaticGeometry.h"
#include "Box3DLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

UBox3DBakeCommandlet::UBox3DBakeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	// Whether this actor should be baked, and with which extraction settings.
	bool ResolveBakeSettings(
		AActor* Actor, FName StaticTag,
		Box3D::StaticGeometry::ESource& OutSource, bool& bOutInvertWinding)
	{
		if (const UBox3DBodyComponent* Comp = Actor->FindComponentByClass<UBox3DBodyComponent>())
		{
			if (Comp->BodyType != EBox3DBodyType::Static)
			{
				return false; // dynamic/kinematic bodies are simulated live, not baked
			}
			OutSource = static_cast<Box3D::StaticGeometry::ESource>(Comp->StaticSource);
			bOutInvertWinding = Comp->bInvertMeshWinding;
			return true;
		}

		// Tag path: no component, so use Auto extraction and no winding override.
		if (!StaticTag.IsNone() && Actor->ActorHasTag(StaticTag))
		{
			OutSource = Box3D::StaticGeometry::ESource::Auto;
			bOutInvertWinding = false;
			return true;
		}

		return false;
	}
}

int32 UBox3DBakeCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	// Maps come from -Map= (comma-separated) and/or bare tokens.
	TArray<FString> Maps;
	FString MapValue;
	if (FParse::Value(*Params, TEXT("Map="), MapValue))
	{
		MapValue.ParseIntoArray(Maps, TEXT(","));
	}
	for (const FString& Token : Tokens)
	{
		Maps.AddUnique(Token);
	}

	if (Maps.Num() == 0)
	{
		UE_LOG(LogBox3D, Error, TEXT("Box3DBake: no maps given. Use -Map=/Game/Maps/Foo[,/Game/Maps/Bar]."));
		return 1;
	}

	FString OutValue;
	const bool bHasOut = FParse::Value(*Params, TEXT("Out="), OutValue);

	FString TagValue;
	const FName StaticTag = FParse::Value(*Params, TEXT("Tag="), TagValue) ? FName(*TagValue) : NAME_None;

	int32 Failures = 0;
	for (const FString& Map : Maps)
	{
		const FString MapPackage = Map.TrimStartAndEnd();
		// -Out only applies to a single-map bake; otherwise derive per map.
		const FString OutPackage =
			(bHasOut && Maps.Num() == 1) ? OutValue : UBox3DCollisionData::DeriveAssetPackageName(MapPackage);
		if (!BakeMap(MapPackage, OutPackage, StaticTag))
		{
			++Failures;
		}
	}

	UE_LOG(LogBox3D, Display, TEXT("Box3DBake: %d/%d maps baked."), Maps.Num() - Failures, Maps.Num());
	return Failures > 0 ? 1 : 0;
}

bool UBox3DBakeCommandlet::BakeMap(const FString& MapPackageName, const FString& OutPackageName, FName StaticTag)
{
	if (!FPackageName::IsValidLongPackageName(MapPackageName))
	{
		UE_LOG(LogBox3D, Error, TEXT("Box3DBake: '%s' is not a valid package name."), *MapPackageName);
		return false;
	}

	// Load through the editor so the world is fully initialized: components are registered
	// and their transforms/collision are valid (ExtractStaticCollision reads GetActorTransform
	// and GetComponentScale, which need a live world - a raw LoadPackage leaves them stale).
	FString MapFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(MapPackageName, MapFilename, FPackageName::GetMapPackageExtension()))
	{
		UE_LOG(LogBox3D, Error, TEXT("Box3DBake: could not resolve a filename for '%s'."), *MapPackageName);
		return false;
	}

	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	if (World == nullptr)
	{
		UE_LOG(LogBox3D, Error, TEXT("Box3DBake: could not load map '%s'."), *MapPackageName);
		return false;
	}

	// Gather bake candidates. Sort by path name for deterministic body order (matches the
	// runtime bulk path - creation order affects island assignment).
	TArray<AActor*> Candidates;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		Box3D::StaticGeometry::ESource Source;
		bool bInvert;
		if (Actor != nullptr && ResolveBakeSettings(Actor, StaticTag, Source, bInvert))
		{
			Candidates.Add(Actor);
		}
	}
	Candidates.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });

	if (Candidates.Num() == 0)
	{
		UE_LOG(LogBox3D, Warning,
			TEXT("Box3DBake: '%s' has no Static-body actors%s to bake; nothing written."),
			*MapPackageName, StaticTag.IsNone() ? TEXT("") : TEXT(" (or tagged actors)"));
		return false;
	}

	// Build the baked bodies.
	TArray<FBox3DBakedBody> BakedBodies;
	bool bAnyTriMesh = false;
	for (AActor* Actor : Candidates)
	{
		Box3D::StaticGeometry::ESource Source;
		bool bInvert;
		ResolveBakeSettings(Actor, StaticTag, Source, bInvert);

		FBox3DBakedBody Baked;
		if (!Box3D::StaticGeometry::ExtractStaticCollision(Actor, Source, bInvert, Baked))
		{
			continue; // ExtractStaticCollision already warned
		}
		for (const FBox3DBakedShape& Shape : Baked.Shapes)
		{
			bAnyTriMesh |= (Shape.Kind == EBox3DBakedShapeKind::Mesh);
		}
		BakedBodies.Add(MoveTemp(Baked));
	}

	if (BakedBodies.Num() == 0)
	{
		UE_LOG(LogBox3D, Warning, TEXT("Box3DBake: '%s' produced no shapes; nothing written."), *MapPackageName);
		return false;
	}

	// Create or reuse the output asset.
	const FString AssetName = FPackageName::GetShortName(OutPackageName);
	UPackage* OutPkg = FPackageName::DoesPackageExist(OutPackageName)
		? LoadPackage(nullptr, *OutPackageName, LOAD_None)
		: nullptr;
	if (OutPkg == nullptr)
	{
		OutPkg = CreatePackage(*OutPackageName);
	}
	if (OutPkg == nullptr)
	{
		UE_LOG(LogBox3D, Error, TEXT("Box3DBake: could not create output package '%s'."), *OutPackageName);
		return false;
	}

	UBox3DCollisionData* Data = FindObject<UBox3DCollisionData>(OutPkg, *AssetName);
	const bool bNewAsset = (Data == nullptr);
	if (bNewAsset)
	{
		Data = NewObject<UBox3DCollisionData>(OutPkg, *AssetName, RF_Public | RF_Standalone);
	}

	Data->SourceLevel = MapPackageName;
	Data->Box3DVersion = Box3D::StaticGeometry::GetBox3DVersionString();
	Data->bContainsTriMesh = bAnyTriMesh;
	Data->BakeTag = StaticTag;
	Data->BakeTime = FDateTime::UtcNow();
	// Fingerprint the level as it was on disk when we extracted it, so a later edit reads
	// as stale. LoadMap read from those same files, so computing it here matches the bake.
	Data->SourceFingerprint = UBox3DCollisionData::ComputeSourceFingerprint(MapPackageName);
	Data->Bodies = MoveTemp(BakedBodies);

	Data->MarkPackageDirty();
	if (bNewAsset)
	{
		FAssetRegistryModule::AssetCreated(Data);
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(
		OutPackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(OutPkg, Data, *Filename, SaveArgs))
	{
		UE_LOG(LogBox3D, Error, TEXT("Box3DBake: failed to save '%s'."), *Filename);
		return false;
	}

	UE_LOG(LogBox3D, Display, TEXT("Box3DBake: '%s' -> '%s' (%d bodies, tri-mesh=%s)."),
		*MapPackageName, *OutPackageName, Data->Bodies.Num(), bAnyTriMesh ? TEXT("yes") : TEXT("no"));
	return true;
}
