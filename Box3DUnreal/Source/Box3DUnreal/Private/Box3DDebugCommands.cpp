// Author: Antonio Lattanzio - emptyvessel

// Demo/debug helpers: console commands that pour a heap of box3d bodies in front of the
// player and fire the spatial queries from the player's view. Purpose-built for capturing
// the convex pour + debug wireframe reveal and for eyeballing query results; not part of
// the runtime API.

#include "Box3DBodyComponent.h"
#include "Box3DSubsystem.h"
#include "Box3DLog.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Asset path of the mesh to pour. Empty -> engine fallback. Point this at a prop with
	// simple CONVEX collision so box3d.Spawn's convex shape has a hull to build.
	TAutoConsoleVariable<FString> CVarSpawnMesh(
		TEXT("box3d.SpawnMesh"),
		TEXT(""),
		TEXT("Asset path of the StaticMesh box3d.Spawn pours (e.g. /Game/Props/SM_Rock.SM_Rock).\n")
		TEXT("Empty uses an engine fallback. Use a mesh with simple CONVEX collision for the convex shape."),
		ECVF_Default);

	// EBox3DShape value for spawned bodies. Default 4 = Convex (the feature being shown).
	TAutoConsoleVariable<int32> CVarSpawnShape(
		TEXT("box3d.SpawnShape"),
		static_cast<int32>(EBox3DShape::Convex),
		TEXT("Shape for box3d.Spawn bodies: 0 Auto, 1 Box, 2 Sphere, 3 Capsule, 4 Convex."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSpawnHeight(
		TEXT("box3d.SpawnHeight"),
		600.0f,
		TEXT("Height (cm) above the player that box3d.Spawn drops props from."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSpawnForward(
		TEXT("box3d.SpawnForward"),
		400.0f,
		TEXT("Forward offset (cm) from the player to the pour, so it lands in view."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSpawnSpread(
		TEXT("box3d.SpawnSpread"),
		150.0f,
		TEXT("Horizontal jitter radius (cm) so props pour into a pile instead of stacking."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSpawnScale(
		TEXT("box3d.SpawnScale"),
		1.0f,
		TEXT("Uniform scale applied to each spawned prop."),
		ECVF_Default);

	// A visible engine mesh so the command still does something when SpawnMesh is unset.
	// Cone has simple collision, so the convex path still exercises (a coarse hull).
	constexpr const TCHAR* FallbackMeshPath = TEXT("/Engine/BasicShapes/Cone.Cone");

	UStaticMesh* ResolveSpawnMesh()
	{
		const FString Path = CVarSpawnMesh.GetValueOnGameThread();
		if (!Path.IsEmpty())
		{
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
			{
				return Mesh;
			}
			UE_LOG(LogBox3D, Warning, TEXT("box3d.Spawn: could not load mesh '%s'; using fallback."), *Path);
		}
		return LoadObject<UStaticMesh>(nullptr, FallbackMeshPath);
	}

	// Reference pose to pour relative to: the local player's pawn, else its camera.
	bool GetPlayerReference(UWorld* World, FVector& OutLocation, FRotator& OutRotation)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC == nullptr)
		{
			return false;
		}
		if (const APawn* Pawn = PC->GetPawn())
		{
			OutLocation = Pawn->GetActorLocation();
			OutRotation = PC->GetControlRotation();
			return true;
		}
		FVector CamLoc;
		FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		OutLocation = CamLoc;
		OutRotation = CamRot;
		return true;
	}

	void SpawnOneProp(UWorld* World, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, float Scale, EBox3DShape Shape)
	{
		const FTransform SpawnXform(Rotation, Location, FVector(Scale));

		AStaticMeshActor* Actor = World->SpawnActorDeferred<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), SpawnXform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Actor == nullptr)
		{
			return;
		}

		// box3d owns movement; the mesh must be Movable so its transform can change.
		UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
		MeshComp->SetMobility(EComponentMobility::Movable);
		MeshComp->SetStaticMesh(Mesh);

		// Register the body BEFORE FinishSpawning so its BeginPlay runs with the actor's.
		UBox3DBodyComponent* Body = NewObject<UBox3DBodyComponent>(Actor);
		Body->BodyType = EBox3DBodyType::Dynamic;
		Body->Shape = Shape;
		Body->Density = 1000.0f;
		Body->Friction = 0.6f;
		Body->Restitution = 0.1f;
		Body->RegisterComponent();

		Actor->FinishSpawning(SpawnXform);
	}

	void SpawnPour(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		if (!UBox3DSubsystem::IsBox3DEnabled())
		{
			UE_LOG(LogBox3D, Warning, TEXT("box3d.Spawn: box3d is disabled; set 'box3d.Enabled 1' first."));
			return;
		}

		int32 Count = 50;
		if (Args.Num() > 0)
		{
			Count = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 500);
		}

		UStaticMesh* Mesh = ResolveSpawnMesh();
		if (Mesh == nullptr)
		{
			UE_LOG(LogBox3D, Warning, TEXT("box3d.Spawn: no mesh resolved (set box3d.SpawnMesh)."));
			return;
		}

		FVector RefLocation;
		FRotator RefRotation;
		if (!GetPlayerReference(World, RefLocation, RefRotation))
		{
			UE_LOG(LogBox3D, Warning, TEXT("box3d.Spawn: no player to pour in front of."));
			return;
		}

		const float Height = CVarSpawnHeight.GetValueOnGameThread();
		const float Forward = CVarSpawnForward.GetValueOnGameThread();
		const float Spread = CVarSpawnSpread.GetValueOnGameThread();
		const float Scale = CVarSpawnScale.GetValueOnGameThread();
		const auto Shape = static_cast<EBox3DShape>(FMath::Clamp(CVarSpawnShape.GetValueOnGameThread(), 0, 4));

		// Pour centre: forward of the player (flattened so it doesn't aim up/down) and up.
		FVector Fwd = RefRotation.Vector();
		Fwd.Z = 0.0;
		Fwd = Fwd.GetSafeNormal();
		const FVector Centre = RefLocation + Fwd * Forward + FVector(0, 0, Height);

		// Stagger height per prop so they fall as a stream and settle into a pile rather
		// than exploding out of a shared spawn point. Random spin varies how hulls land.
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D Disk = FMath::RandPointInCircle(Spread);
			const FVector Loc = Centre + FVector(Disk.X, Disk.Y, i * 60.0f);
			const FRotator Rot = FMath::VRand().Rotation();
			SpawnOneProp(World, Mesh, Loc, Rot, Scale, Shape);
		}

		UE_LOG(LogBox3D, Log, TEXT("box3d.Spawn: poured %d '%s' props (shape %d)."),
			Count, *Mesh->GetName(), static_cast<int32>(Shape));
	}

	FAutoConsoleCommandWithWorldAndArgs GBox3DSpawnCommand(
		TEXT("box3d.Spawn"),
		TEXT("Pour N box3d props in front of the player (default 50). Configure with ")
		TEXT("box3d.SpawnMesh / SpawnShape / SpawnHeight / SpawnForward / SpawnSpread / SpawnScale."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnPour));

	// --- Query smoke tests -------------------------------------------------------------
	// Fire the subsystem's queries from the player's viewpoint and draw what came back.
	// The draws persist ~5s so the result can be inspected after the call.

	constexpr float QueryDrawSeconds = 5.0f;

	UBox3DSubsystem* GetQuerySubsystem(UWorld* World, const TCHAR* CommandName)
	{
		UBox3DSubsystem* Subsystem = World ? World->GetSubsystem<UBox3DSubsystem>() : nullptr;
		if (Subsystem == nullptr || !Subsystem->IsWorldValid())
		{
			// No box3d world: disabled, or this is a client (queries only run where box3d sims).
			UE_LOG(LogBox3D, Warning, TEXT("%s: no box3d world to query (disabled, or a client)."), CommandName);
			return nullptr;
		}
		return Subsystem;
	}

	void QueryRay(const TArray<FString>& Args, UWorld* World)
	{
		UBox3DSubsystem* Subsystem = GetQuerySubsystem(World, TEXT("box3d.QueryRay"));
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (Subsystem == nullptr || PC == nullptr)
		{
			return;
		}

		const float Distance = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5000.0f;
		FVector Start;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(Start, ViewRotation);
		const FVector End = Start + ViewRotation.Vector() * Distance;

		FBox3DHitResult Hit;
		const bool bHit = Subsystem->RaycastClosest(Start, End, FBox3DQueryFilter(), Hit);

		DrawDebugLine(World, Start, bHit ? Hit.Location : End, bHit ? FColor::Green : FColor::Red, false,
			QueryDrawSeconds, 0, 1.0f);
		if (!bHit)
		{
			UE_LOG(LogBox3D, Log, TEXT("box3d.QueryRay: no hit within %.0fcm."), Distance);
			return;
		}

		DrawDebugPoint(World, Hit.Location, 10.0f, FColor::Yellow, false, QueryDrawSeconds);
		DrawDebugDirectionalArrow(World, Hit.Location, Hit.Location + Hit.Normal * 50.0, 8.0f, FColor::Cyan, false,
			QueryDrawSeconds, 0, 1.0f);
		UE_LOG(LogBox3D, Log, TEXT("box3d.QueryRay: hit '%s' at %s (%.1fcm, tri %d), normal %s."),
			Hit.HitActor ? *Hit.HitActor->GetName() : TEXT("<baked static>"), *Hit.Location.ToCompactString(),
			Hit.Distance, Hit.TriangleIndex, *Hit.Normal.ToCompactString());
	}

	void QueryOverlap(const TArray<FString>& Args, UWorld* World)
	{
		UBox3DSubsystem* Subsystem = GetQuerySubsystem(World, TEXT("box3d.QueryOverlap"));
		if (Subsystem == nullptr)
		{
			return;
		}

		FVector Centre;
		FRotator RefRotation;
		if (!GetPlayerReference(World, Centre, RefRotation))
		{
			UE_LOG(LogBox3D, Warning, TEXT("box3d.QueryOverlap: no player to query around."));
			return;
		}

		const float Radius = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 500.0f;
		TArray<AActor*> Actors;
		Subsystem->OverlapSphere(Centre, Radius, FBox3DQueryFilter(), Actors);

		DrawDebugSphere(World, Centre, Radius, 24, Actors.Num() > 0 ? FColor::Green : FColor::Red, false,
			QueryDrawSeconds, 0, 1.0f);
		for (const AActor* Actor : Actors)
		{
			DrawDebugPoint(World, Actor->GetActorLocation(), 12.0f, FColor::Yellow, false, QueryDrawSeconds);
		}
		UE_LOG(LogBox3D, Log, TEXT("box3d.QueryOverlap: %d actor(s) within %.0fcm."), Actors.Num(), Radius);
	}

	FAutoConsoleCommandWithWorldAndArgs GBox3DQueryRayCommand(
		TEXT("box3d.QueryRay"),
		TEXT("Raycast the box3d world from the player's view (default 5000cm) and draw the closest hit."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QueryRay));

	FAutoConsoleCommandWithWorldAndArgs GBox3DQueryOverlapCommand(
		TEXT("box3d.QueryOverlap"),
		TEXT("Sphere-overlap the box3d world around the player (default radius 500cm) and list the actors."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QueryOverlap));

	// Print the live world's state hash. Run it on server and client with the sim paused
	// (pause box3d.Enabled or the game) to eyeball a desync; the number is stable for a given build
	// and scenario. Cross-machine comparison still needs a shared body ordering (a D2 concern).
	void HashState(UWorld* World)
	{
		UBox3DSubsystem* Subsystem = GetQuerySubsystem(World, TEXT("box3d.HashState"));
		if (Subsystem == nullptr)
		{
			return;
		}
		int32 BodyCount = 0;
		const uint32 Hash = Subsystem->ComputeWorldStateHash(BodyCount);
		UE_LOG(LogBox3D, Log, TEXT("box3d.HashState: frame %lld, 0x%08X over %d dynamic bodies."),
			Subsystem->GetSimulationFrame(), Hash, BodyCount);
	}

	FAutoConsoleCommandWithWorld GBox3DHashStateCommand(
		TEXT("box3d.HashState"),
		TEXT("Print the djb2 state hash of the live world's dynamic bodies (determinism / desync check)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&HashState));
} // namespace
