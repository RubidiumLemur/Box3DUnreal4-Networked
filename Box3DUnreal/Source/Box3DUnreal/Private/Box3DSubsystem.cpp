// Author: Antonio Lattanzio - emptyvessel

#include "Box3DSubsystem.h"
#include "Box3DBodyComponent.h"
#include "Box3DCollisionData.h"
#include "Box3DConversion.h"
#include "Box3DStaticGeometry.h"
#include "Box3DLog.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

static TAutoConsoleVariable<int32> CVarBox3DDebugDraw(
	TEXT("box3d.DebugDraw"),
	0,
	TEXT("Draw box3d dynamic bodies at their actual simulation transform (1 = on)."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarBox3DEnabled(
	TEXT("box3d.Enabled"),
	1,
	TEXT("Master switch for the box3d simulation. 1 = on, 0 = off (no world, bodies or static geometry).\n")
	TEXT("Toggle live in PIE to compare against no-box3d; also forced off at launch by -DisableBox3D."),
	ECVF_Default);

bool UBox3DSubsystem::IsBox3DEnabled()
{
	return CVarBox3DEnabled.GetValueOnGameThread() != 0;
}

bool UBox3DSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Simulation only runs in play worlds. Editor preview support can be added later.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UBox3DSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// box3d is server-authoritative: only Standalone and servers simulate. A pure
	// client leaves its actors to UE's replicated movement, so it never spins up a
	// second (diverging) world.
	bIsAuthority = InWorld.GetNetMode() != NM_Client;

	// Watch box3d.Enabled so it can be toggled live in PIE. The sink fires for any cvar
	// change; OnEnabledCVarChanged filters to an actual on<->off transition.
	EnabledSinkHandle = IConsoleManager::Get().RegisterConsoleVariableSink_Handle(
		FConsoleCommandDelegate::CreateUObject(this, &UBox3DSubsystem::OnEnabledCVarChanged));

	if (!bIsAuthority)
	{
		UE_LOG(LogBox3D, Log,
			TEXT("box3d: client world - simulation disabled; actors follow replicated movement."));
		return;
	}

	if (IsBox3DEnabled())
	{
		EnableSimulation();
	}
	else
	{
		UE_LOG(LogBox3D, Log,
			TEXT("box3d: disabled (box3d.Enabled=0 / -DisableBox3D). Set 'box3d.Enabled 1' to build the simulation."));
	}
}

void UBox3DSubsystem::EnableSimulation()
{
	CreateBox3DWorld();
	if (!bWorldValid)
	{
		return; // creation failed; stay inactive so a later toggle can retry
	}
	bEnabledActive = true;

	// Pre-baked static collision: instantiate cached bodies with no runtime cooking.
	LoadBakedStaticGeometry();

	// Opt-in bulk static path: watch level streaming and scan the already-loaded
	// levels. Static bodies never move, so only the authority (which simulates)
	// needs them - clients follow replicated dynamics and create no world.
	if (!StaticGeometryTag.IsNone())
	{
		LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UBox3DSubsystem::OnLevelAddedToWorld);
		LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UBox3DSubsystem::OnLevelRemovedFromWorld);
		if (UWorld* World = GetWorld())
		{
			for (ULevel* Level : World->GetLevels())
			{
				RegisterLevelStaticGeometry(Level);
			}
		}
	}

	// Runtime re-enable: components already registered (in their BeginPlay) but with no
	// body. Build them now. On the initial startup this list is empty - each component
	// builds itself in BeginPlay - so this is a no-op then.
	for (int32 Index = AllBodies.Num() - 1; Index >= 0; --Index)
	{
		if (UBox3DBodyComponent* Body = AllBodies[Index].Get())
		{
			Body->RebuildSimulationBody();
		}
		else
		{
			AllBodies.RemoveAtSwap(Index);
		}
	}
}

void UBox3DSubsystem::DisableSimulation()
{
	// Destroy component-owned bodies while the world is still valid (b3DestroyWorld would
	// otherwise leave their handles dangling). They stay registered in AllBodies so a
	// later enable rebuilds them.
	for (int32 Index = AllBodies.Num() - 1; Index >= 0; --Index)
	{
		if (UBox3DBodyComponent* Body = AllBodies[Index].Get())
		{
			Body->TeardownSimulationBody();
		}
		else
		{
			AllBodies.RemoveAtSwap(Index);
		}
	}
	// Step lists are rebuilt on re-enable (the components re-register).
	DynamicBodies.Reset();
	KinematicBodies.Reset();

	if (LevelAddedHandle.IsValid())
	{
		FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
		LevelAddedHandle.Reset();
	}
	if (LevelRemovedHandle.IsValid())
	{
		FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
		LevelRemovedHandle.Reset();
	}

	DestroyBox3DWorld(); // also destroys bulk static bodies/meshes
	bEnabledActive = false;

	UE_LOG(LogBox3D, Log, TEXT("box3d: simulation torn down (box3d.Enabled=0)."));
}

void UBox3DSubsystem::OnEnabledCVarChanged()
{
	// Only the authority builds a world; clients never simulate regardless of the switch.
	if (!bIsAuthority)
	{
		return;
	}

	const bool bWantEnabled = IsBox3DEnabled();
	if (bWantEnabled == bEnabledActive)
	{
		return; // some other cvar changed, or no net transition
	}

	if (bWantEnabled)
	{
		EnableSimulation();
	}
	else
	{
		DisableSimulation();
	}
}

void UBox3DSubsystem::Deinitialize()
{
	// Always registered in OnWorldBeginPlay (authority and client); drop it here.
	IConsoleManager::Get().UnregisterConsoleVariableSink_Handle(EnabledSinkHandle);
	if (LevelAddedHandle.IsValid())
	{
		FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
		LevelAddedHandle.Reset();
	}
	if (LevelRemovedHandle.IsValid())
	{
		FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
		LevelRemovedHandle.Reset();
	}

	DestroyBox3DWorld();

	// Drop component registrations on full shutdown (the enable/disable path keeps them).
	DynamicBodies.Reset();
	KinematicBodies.Reset();
	AllBodies.Reset();
	bEnabledActive = false;

	Super::Deinitialize();
}

void UBox3DSubsystem::CreateBox3DWorld()
{
	if (bWorldValid)
	{
		return;
	}

	b3WorldDef Def = b3DefaultWorldDef();
	Def.gravity = Box3D::ToBox3DVector(Gravity);

	WorldId = b3CreateWorld(&Def);
	bWorldValid = b3World_IsValid(WorldId);

	if (bWorldValid)
	{
		const b3Version V = b3GetVersion();
		UE_LOG(LogBox3D, Log, TEXT("box3d %d.%d.%d world created (gravity %s cm/s^2, %.0f Hz x%d substeps)."),
			V.major, V.minor, V.revision, *Gravity.ToCompactString(), 1.0f / FixedTimeStep, SubStepCount);
	}
	else
	{
		UE_LOG(LogBox3D, Error, TEXT("box3d world creation failed."));
	}
}

void UBox3DSubsystem::DestroyBox3DWorld()
{
	// Component registrations are preserved (the enable/disable toggle rebuilds from
	// them); Deinitialize drops them on full shutdown. Only tear down box3d resources here.
	if (bWorldValid)
	{
		b3DestroyWorld(WorldId); // destroys every body, including bulk static ones
	}

	// Bulk tri-mesh data are separate allocations the (now destroyed) shapes referenced;
	// free them after the world so nothing dangles.
	for (TPair<TWeakObjectPtr<ULevel>, FBulkStaticLevel>& Pair : BulkStaticLevels)
	{
		for (b3MeshData* Mesh : Pair.Value.Meshes)
		{
			if (Mesh != nullptr)
			{
				b3DestroyMesh(Mesh);
			}
		}
	}
	BulkStaticLevels.Reset();

	// Same for baked-asset bodies' mesh data.
	for (FBulkStaticLevel& Bucket : BakedStaticBuckets)
	{
		for (b3MeshData* Mesh : Bucket.Meshes)
		{
			if (Mesh != nullptr)
			{
				b3DestroyMesh(Mesh);
			}
		}
	}
	BakedStaticBuckets.Reset();

	WorldId = b3_nullWorldId;
	bWorldValid = false;
	Accumulator = 0.0;
}

void UBox3DSubsystem::OnLevelAddedToWorld(ULevel* Level, UWorld* World)
{
	// FWorldDelegates are global; only mirror levels belonging to our world.
	if (World == GetWorld())
	{
		RegisterLevelStaticGeometry(Level);
	}
}

void UBox3DSubsystem::OnLevelRemovedFromWorld(ULevel* Level, UWorld* World)
{
	if (World == GetWorld())
	{
		UnregisterLevelStaticGeometry(Level);
	}
}

void UBox3DSubsystem::RegisterLevelStaticGeometry(ULevel* Level)
{
	if (!bWorldValid || Level == nullptr || StaticGeometryTag.IsNone() || BulkStaticLevels.Contains(Level))
	{
		return;
	}

	TArray<AActor*> Tagged;
	for (AActor* Actor : Level->Actors)
	{
		if (Actor != nullptr && Actor->ActorHasTag(StaticGeometryTag))
		{
			Tagged.Add(Actor);
		}
	}
	if (Tagged.Num() == 0)
	{
		return;
	}

	// Deterministic creation order: streaming completion order is not stable, but body
	// creation order affects island assignment / reproducibility (doc §8). Sort by the
	// stable full path name.
	Tagged.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });

	FBulkStaticLevel Bulk;
	for (AActor* Actor : Tagged)
	{
		CreateBulkStaticBody(Actor, Bulk);
	}

	if (Bulk.Bodies.Num() > 0)
	{
		UE_LOG(LogBox3D, Log, TEXT("box3d: bulk-registered %d static bodies from level '%s' (tag '%s')."),
			Bulk.Bodies.Num(), *GetNameSafe(Level), *StaticGeometryTag.ToString());
		BulkStaticLevels.Add(Level, MoveTemp(Bulk));
	}
	else
	{
		// Every actor extracted nothing; drop any meshes a partial attempt allocated.
		for (b3MeshData* Mesh : Bulk.Meshes)
		{
			if (Mesh != nullptr)
			{
				b3DestroyMesh(Mesh);
			}
		}
	}
}

void UBox3DSubsystem::UnregisterLevelStaticGeometry(ULevel* Level)
{
	FBulkStaticLevel Bulk;
	if (!BulkStaticLevels.RemoveAndCopyValue(Level, Bulk))
	{
		return;
	}

	if (bWorldValid)
	{
		for (b3BodyId Body : Bulk.Bodies)
		{
			if (B3_IS_NON_NULL(Body))
			{
				b3DestroyBody(Body);
			}
		}
	}
	// Meshes were referenced by the shapes we just destroyed; free them after.
	for (b3MeshData* Mesh : Bulk.Meshes)
	{
		if (Mesh != nullptr)
		{
			b3DestroyMesh(Mesh);
		}
	}
}

void UBox3DSubsystem::CreateBulkStaticBody(AActor* Actor, FBulkStaticLevel& Bulk)
{
	const FTransform Xform = Actor->GetActorTransform();

	b3BodyDef Def = b3DefaultBodyDef();
	Def.type = b3_staticBody;
	Def.position = Box3D::ToBox3DPosition(Xform.GetLocation());
	Def.rotation = Box3D::ToBox3DQuat(Xform.GetRotation());

	// Lets a query hit resolve back to the actor; the body is freed on stream-out with it.
	Def.userData = Actor;

	b3BodyId Body = b3CreateBody(WorldId, &Def);
	if (B3_IS_NULL(Body))
	{
		return;
	}

	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	ShapeDef.baseMaterial.friction = StaticGeometryFriction;
	ShapeDef.baseMaterial.restitution = StaticGeometryRestitution;

	// Reuse the component path's cooked-collision extraction (scale/handedness/winding
	// all handled there). Auto = complex tri-mesh if present, else simple primitives.
	if (Box3D::StaticGeometry::AddStaticShapes(
			Body, ShapeDef, Actor, Box3D::StaticGeometry::ESource::Auto, /*bInvertWinding=*/false, Bulk.Meshes))
	{
		Bulk.Bodies.Add(Body);
	}
	else
	{
		b3DestroyBody(Body);
	}
}

TArray<UBox3DCollisionData*> UBox3DSubsystem::GatherBakedCollisionAssets() const
{
	TArray<UBox3DCollisionData*> Assets;

	for (const TSoftObjectPtr<UBox3DCollisionData>& AssetPtr : BakedCollisionAssets)
	{
		if (UBox3DCollisionData* Data = AssetPtr.LoadSynchronous())
		{
			Assets.AddUnique(Data);
		}
		else
		{
			UE_LOG(LogBox3D, Warning, TEXT("box3d: baked collision asset '%s' could not be loaded."),
				*AssetPtr.ToString());
		}
	}

	if (bAutoDiscoverBakedCollision)
	{
		UBox3DCollisionData* Found = FindBakedAssetForCurrentMap();
		if (Found != nullptr && !Assets.Contains(Found))
		{
			UE_LOG(LogBox3D, Log, TEXT("box3d: auto-discovered baked collision '%s' for this map."),
				*GetNameSafe(Found));
			Assets.Add(Found);
		}
	}

	return Assets;
}

UBox3DCollisionData* UBox3DSubsystem::FindBakedAssetForCurrentMap() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// PIE renames the package to UEDPIE_0_<Map>; the bake keyed off the real map name.
	const FString MapPackage = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	const FString AssetPackage = UBox3DCollisionData::DeriveAssetPackageName(MapPackage);
	if (!FPackageName::DoesPackageExist(AssetPackage))
	{
		return nullptr; // nothing baked for this map; the live component/tag paths still apply
	}

	const FString ObjectPath =
		FString::Printf(TEXT("%s.%s"), *AssetPackage, *FPackageName::GetShortName(AssetPackage));
	return LoadObject<UBox3DCollisionData>(nullptr, *ObjectPath);
}

void UBox3DSubsystem::WarnIfBakeStale(const UBox3DCollisionData* Data) const
{
#if WITH_EDITOR
	FString Reason;
	if (Data != nullptr && Data->IsStale(Reason))
	{
		// Warn, never refuse: stale geometry is usually still close enough to keep iterating on,
		// and an implicit re-bake here would stall PIE for however long the level takes.
		UE_LOG(LogBox3D, Warning,
			TEXT("box3d: baked collision '%s' is stale - %s. Static geometry may not match the level; ")
			TEXT("re-bake with: -run=Box3DBake -Map=%s"),
			*GetNameSafe(Data), *Reason, *Data->SourceLevel);
	}
#endif
}

void UBox3DSubsystem::LoadBakedStaticGeometry()
{
	if (!bWorldValid)
	{
		return;
	}

	const TArray<UBox3DCollisionData*> Assets = GatherBakedCollisionAssets();
	if (Assets.Num() == 0)
	{
		return;
	}

	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	ShapeDef.baseMaterial.friction = StaticGeometryFriction;
	ShapeDef.baseMaterial.restitution = StaticGeometryRestitution;

	for (UBox3DCollisionData* Data : Assets)
	{
		WarnIfBakeStale(Data);

		FBulkStaticLevel Bucket;
		for (const FBox3DBakedBody& Baked : Data->Bodies)
		{
			b3BodyDef Def = b3DefaultBodyDef();
			Def.type = b3_staticBody;
			Def.position = Box3D::ToBox3DPosition(Baked.WorldTransform.GetLocation());
			Def.rotation = Box3D::ToBox3DQuat(Baked.WorldTransform.GetRotation());

			b3BodyId Body = b3CreateBody(WorldId, &Def);
			if (B3_IS_NULL(Body))
			{
				continue;
			}

			// Baked shapes are already fully converted; just instantiate them (no cooking).
			if (Box3D::StaticGeometry::AddBakedShapes(Body, ShapeDef, Baked, Bucket.Meshes))
			{
				Bucket.Bodies.Add(Body);
			}
			else
			{
				b3DestroyBody(Body);
			}
		}

		UE_LOG(LogBox3D, Log, TEXT("box3d: loaded %d baked static bodies from '%s'."),
			Bucket.Bodies.Num(), *GetNameSafe(Data));
		if (Bucket.Bodies.Num() > 0)
		{
			BakedStaticBuckets.Add(MoveTemp(Bucket));
		}
		else
		{
			for (b3MeshData* Mesh : Bucket.Meshes)
			{
				if (Mesh != nullptr)
				{
					b3DestroyMesh(Mesh);
				}
			}
		}
	}
}

void UBox3DSubsystem::RegisterBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		AllBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::RegisterDynamicBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		DynamicBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::RegisterKinematicBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		KinematicBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::UnregisterBody(UBox3DBodyComponent* Component)
{
	DynamicBodies.RemoveSingleSwap(Component);
	KinematicBodies.RemoveSingleSwap(Component);
	AllBodies.RemoveSingleSwap(Component);
}

bool UBox3DSubsystem::IsTickable() const
{
	// Authority worlds tick to step; client worlds tick only to debug-draw the
	// replicated bodies they registered.
	return bWorldValid || AllBodies.Num() > 0;
}

TStatId UBox3DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBox3DSubsystem, STATGROUP_Tickables);
}

void UBox3DSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only authority worlds step; but debug draw runs everywhere so clients can see
	// their replicated bodies.
	if (bWorldValid)
	{
		StepFixed(DeltaTime);
	}

	// Debug draw only while box3d is enabled - a disabled sim leaves no bodies to show,
	// so the wireframes/counter must vanish too (else it looks like it's still running).
	if (IsBox3DEnabled() && CVarBox3DDebugDraw.GetValueOnGameThread() != 0)
	{
		DebugDraw();
	}
}

void UBox3DSubsystem::DebugDraw()
{
	// One-shot confirmation the *new* debug path is running (settles stale-build
	// questions: if you don't see this line after enabling the cvar, the binary
	// wasn't rebuilt).
	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		bLoggedOnce = true;
		UE_LOG(LogBox3D, Log, TEXT("box3d.DebugDraw active: drawing bodies at actor pose."));
	}

	int32 BulkBodyCount = 0;
	for (const TPair<TWeakObjectPtr<ULevel>, FBulkStaticLevel>& Pair : BulkStaticLevels)
	{
		BulkBodyCount += Pair.Value.Bodies.Num();
	}
	int32 BakedBodyCount = 0;
	for (const FBulkStaticLevel& Bucket : BakedStaticBuckets)
	{
		BakedBodyCount += Bucket.Bodies.Num();
	}

	if (GEngine != nullptr)
	{
		// AUTH = this world simulates; CLIENT = it only displays replicated poses.
		const TCHAR* RoleTag = bIsAuthority ? TEXT("AUTH") : TEXT("CLIENT");
		GEngine->AddOnScreenDebugMessage(
			static_cast<uint64>(reinterpret_cast<UPTRINT>(this)), 0.0f, FColor::Green,
			FString::Printf(TEXT("[box3d|%s] %d bodies (%d dynamic, %d bulk static, %d baked) @ %.0f Hz x%d"),
				RoleTag, AllBodies.Num() + BulkBodyCount + BakedBodyCount, DynamicBodies.Num(),
				BulkBodyCount, BakedBodyCount, 1.0f / FixedTimeStep, SubStepCount));
	}

	for (int32 Index = AllBodies.Num() - 1; Index >= 0; --Index)
	{
		if (const UBox3DBodyComponent* Body = AllBodies[Index].Get())
		{
			Body->DrawDebug();
		}
		else
		{
			AllBodies.RemoveAtSwap(Index);
		}
	}

	// Bulk static bodies have no component to draw themselves, so draw each one's world
	// AABB here (cyan, the static colour). It's an approximation - the box3d shape is the
	// actor's cooked collision, not a box - but it confirms a body exists at the right
	// place and span. Only the authority holds these (clients keep BulkStaticLevels empty).
	if (UWorld* World = GetWorld())
	{
		auto DrawBodyAABBs = [World](const TArray<b3BodyId>& Bodies)
		{
			for (const b3BodyId Body : Bodies)
			{
				if (B3_IS_NULL(Body))
				{
					continue;
				}

				// Convert both corners: the Y-negation swaps min/max on Y, so rebuild the
				// box from the two converted points rather than assuming lower<upper.
				const b3AABB Box = b3Body_ComputeAABB(Body);
				FBox UEBox(ForceInit);
				UEBox += Box3D::FromBox3DVector(Box.lowerBound);
				UEBox += Box3D::FromBox3DVector(Box.upperBound);
				DrawDebugBox(World, UEBox.GetCenter(), UEBox.GetExtent(), FColor::Cyan, false, -1.0f, 0, 1.0f);
			}
		};

		for (const TPair<TWeakObjectPtr<ULevel>, FBulkStaticLevel>& Pair : BulkStaticLevels)
		{
			DrawBodyAABBs(Pair.Value.Bodies);
		}
		for (const FBulkStaticLevel& Bucket : BakedStaticBuckets)
		{
			DrawBodyAABBs(Bucket.Bodies);
		}
	}
}

void UBox3DSubsystem::StepFixed(float DeltaTime)
{
	// Consume real time in whole fixed steps. After each step, capture every
	// dynamic body's transform so we always have the last two states to blend.
	Accumulator = FMath::Min(Accumulator + DeltaTime, static_cast<double>(MaxFrameTime));

	while (Accumulator >= FixedTimeStep)
	{
		// Drive kinematic bodies from their (gameplay-moved) actor transform so they
		// carry resting dynamics.
		for (int32 Index = KinematicBodies.Num() - 1; Index >= 0; --Index)
		{
			if (UBox3DBodyComponent* Body = KinematicBodies[Index].Get())
			{
				Body->PushKinematicTarget(FixedTimeStep);
			}
			else
			{
				KinematicBodies.RemoveAtSwap(Index);
			}
		}

		b3World_Step(WorldId, FixedTimeStep, SubStepCount);
		Accumulator -= FixedTimeStep;

		for (int32 Index = DynamicBodies.Num() - 1; Index >= 0; --Index)
		{
			if (UBox3DBodyComponent* Body = DynamicBodies[Index].Get())
			{
				Body->CaptureStepTransform();
			}
			else
			{
				DynamicBodies.RemoveAtSwap(Index);
			}
		}
	}

	// Interpolate the render pose between the last two steps (leftover fraction).
	const float Alpha = static_cast<float>(Accumulator / FixedTimeStep);
	for (const TWeakObjectPtr<UBox3DBodyComponent>& WeakBody : DynamicBodies)
	{
		if (UBox3DBodyComponent* Body = WeakBody.Get())
		{
			Body->ApplyInterpolatedTransform(Alpha);
		}
	}
}
