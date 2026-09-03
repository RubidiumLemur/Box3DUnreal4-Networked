// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Box3DQueryTypes.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/WorldSubsystem.h"
#include <box3d/box3d.h>
#include "Box3DSubsystem.generated.h"

class AActor;
class APlayerController;
class UBox3DBodyComponent;
class ABox3DInstancedBodyActor;
class UBox3DCollisionData;
class ULevel;

/**
 * @brief Historical snapshot of a Box3D body for rewind and validation.
 *
 * @details This structure stores the minimal state needed to restore and re-simulate an
 * authoritative body at a prior tick. It is intentionally bounded and used by the server-side
 * replay path to validate client input without trusting any actor-side transform as authority.
 */
USTRUCT()
struct FBox3DHistoryFrame
{
	GENERATED_BODY()

	int64 SimulationTick = 0;
	double Timestamp = 0.0;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	bool bSleeping = false;
};

/**
 * @brief World-scoped owner of the Box3D simulation.
 *
 * @details The subsystem creates and owns the single Box3D world used by the current Unreal
 * world. It advances the simulation on a fixed timestep, registers dynamic and kinematic bodies,
 * handles static geometry, and maintains the bounded historical state used by the server-side
 * rewind and validation flow for networked bodies.
 */
// Config=Game: world-subsystem UPROPERTYs have no details-panel, so the bulk-static
// settings are read from DefaultGame.ini's [/Script/Box3DUnreal.Box3DSubsystem].
UCLASS(Config = Game)
class BOX3DUNREAL_API UBox3DSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem / UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Master switch, shared by the subsystem and every body component. Off means no
	 *  box3d world, bodies or static geometry. Backed by the box3d.Enabled cvar (also
	 *  forced off at launch by -DisableBox3D); toggle at runtime to A/B against no-box3d. */
	static bool IsBox3DEnabled();

	/** The box3d world id. Only valid while IsWorldValid(). */
	b3WorldId GetWorldId() const { return WorldId; }
	bool IsWorldValid() const { return bWorldValid; }

	/** True where box3d simulates: Standalone and servers, never a pure client. */
	bool IsSimulationAuthority() const { return bIsAuthority; }

	/** Monotonic count of fixed steps taken since world create - the deterministic timeline a
	 *  rollback aligns to (doc §11b). Same step count from the same start gives the same state. */
	int64 GetSimulationFrame() const { return SimulationFrame; }

	/** djb2 digest of every dynamic body's state (transform + velocity), for determinism /
	 *  desync detection (doc §14). Bodies are folded in a stable order (owner path name) so the
	 *  same world hashes the same across runs. OutBodyCount reports how many contributed.
	 *  NOTE: stable within one build/scenario; a cross-machine desync check additionally needs a
	 *  shared body ordering (networked ids - a D2 concern). Zero if no world / no dynamic bodies. */
	uint32 ComputeWorldStateHash(int32& OutBodyCount) const;

	/** All bodies register for debug draw; dynamic ones also register for sync. */
	void RegisterBody(UBox3DBodyComponent* Component);
	void RegisterDynamicBody(UBox3DBodyComponent* Component);
	void RegisterKinematicBody(UBox3DBodyComponent* Component);
	void UnregisterBody(UBox3DBodyComponent* Component);
	void RegisterInstancedBodyActor(ABox3DInstancedBodyActor* Actor);
	void UnregisterInstancedBodyActor(ABox3DInstancedBodyActor* Actor);

	// --- Spatial queries (doc §11) --------------------------------------------------
	// All positions/extents are Unreal world space in cm; results come back the same way.
	// Queries run against the box3d world, so they only return hits where box3d simulates:
	// Standalone and servers. On a pure client there is no world and every query returns
	// false/empty - use UE traces there, or call these on the authority.

	/** Closest shape hit along Start->End. Initial overlap at Start is ignored. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool RaycastClosest(const FVector& Start, const FVector& End, const FBox3DQueryFilter& Filter,
		FBox3DHitResult& OutHit) const;

	/** Every shape hit along Start->End, sorted near to far. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool RaycastMulti(const FVector& Start, const FVector& End, const FBox3DQueryFilter& Filter,
		TArray<FBox3DHitResult>& OutHits) const;

	/** Broadphase only: actors whose shape bounds *potentially* overlap the box. Cheap, but
	 *  a hit is not an exact overlap - use OverlapSphere/OverlapBox for a narrow-phase test. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool OverlapAABB(const FVector& Center, const FVector& HalfExtent, const FBox3DQueryFilter& Filter,
		TArray<AActor*>& OutActors) const;

	/** Actors exactly overlapping the sphere. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool OverlapSphere(const FVector& Center, float Radius, const FBox3DQueryFilter& Filter,
		TArray<AActor*>& OutActors) const;

	/** Actors exactly overlapping the oriented box. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool OverlapBox(const FVector& Center, const FVector& HalfExtent, const FRotator& Rotation,
		const FBox3DQueryFilter& Filter, TArray<AActor*>& OutActors) const;

	/** Sweep a sphere Start->End and return the first blocking hit. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool SphereCast(const FVector& Start, const FVector& End, float Radius, const FBox3DQueryFilter& Filter,
		FBox3DHitResult& OutHit) const;

	/** Sweep an oriented box Start->End (no rotation over the sweep) and return the first hit. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Query")
	bool BoxCast(const FVector& Start, const FVector& End, const FVector& HalfExtent, const FRotator& Rotation,
		const FBox3DQueryFilter& Filter, FBox3DHitResult& OutHit) const;

	/**
	 * @brief Records the current authoritative state for a body into the bounded rewind history.
	 *
	 * @details This is used on the authoritative server to capture the minimal state required for
	 * later validation and replay. The data is stored in a bounded ring-like history window so the
	 * server can rewind only the relevant state without unbounded memory growth.
	 */
	void CaptureNetworkHistoryForBody(UBox3DBodyComponent* Body);
	/**
	 * @brief Attempts to restore the closest historical body state matching the client's requested tick.
	 *
	 * @return True when a valid frame exists within the configured rewind window and was copied to OutState.
	 */
	bool TryRewindBodyHistory(UBox3DBodyComponent* Body, int64 RequestTick, double RequestTimestamp,
		FBox3DHistoryFrame& OutState) const;
	/** @brief Restores a previously captured historical state back onto the authoritative body. */
	void RestoreBodyHistory(UBox3DBodyComponent* Body, const FBox3DHistoryFrame& State);
	/** @brief Returns whether a body is within the current player's relevance distance. */
	bool IsBodyRelevantToPlayer(const UBox3DBodyComponent* Body, const APlayerController* PC) const;
	/** @brief Returns whether a body is within a given location-based relevance threshold. */
	bool IsBodyRelevantToPlayerLocation(const UBox3DBodyComponent* Body, const FVector& PlayerLocation) const;

protected:
	void CreateBox3DWorld();
	void DestroyBox3DWorld();
	void StepFixed(float DeltaTime);
	void DebugDraw();

	// Master-switch plumbing. EnableSimulation builds the world, static geometry and
	// (on a runtime toggle) any already-registered body; DisableSimulation tears them
	// down but keeps the component registrations so a later enable can rebuild.
	void EnableSimulation();
	void DisableSimulation();
	void OnEnabledCVarChanged();

	// Bulk static geometry: scan streamed levels for tagged actors and mirror their
	// cooked collision, no per-actor component needed. Opt-in via StaticGeometryTag.
	void OnLevelAddedToWorld(ULevel* Level, UWorld* World);
	void OnLevelRemovedFromWorld(ULevel* Level, UWorld* World);
	void RegisterLevelStaticGeometry(ULevel* Level);
	void UnregisterLevelStaticGeometry(ULevel* Level);

	// Baked static geometry: instantiate pre-cooked collision from UBox3DCollisionData
	// assets (no runtime cooking - works in packaged builds). Opt-in via BakedCollisionAssets.
	void LoadBakedStaticGeometry();

	/** The configured assets plus, if bAutoDiscoverBakedCollision, this map's BC_ asset. Deduped,
	 *  so listing an auto-discovered asset explicitly is harmless. */
	TArray<UBox3DCollisionData*> GatherBakedCollisionAssets() const;
	UBox3DCollisionData* FindBakedAssetForCurrentMap() const;

	/** Editor/PIE only: warn when a bake no longer matches its source level or box3d version.
	 *  A packaged build can't re-bake, so the check runs where it can still be acted on. */
	void WarnIfBakeStale(const UBox3DCollisionData* Data) const;

private:
	b3WorldId WorldId = b3_nullWorldId;
	bool bWorldValid = false;
	bool bIsAuthority = false;

	/** True while the simulation is built. Tracks the master switch so the runtime
	 *  sink only rebuilds/tears down on an actual on<->off transition. */
	bool bEnabledActive = false;

	/** Fires when any cvar changes; we re-check box3d.Enabled to build or tear down. */
	FConsoleVariableSinkHandle EnabledSinkHandle;

	/** Real time carried between frames, consumed in fixed increments. */
	double Accumulator = 0.0;

	/** Fixed steps taken since world create (see GetSimulationFrame). */
	int64 SimulationFrame = 0;

	UPROPERTY(EditAnywhere, Category = "Box3D")
	float FixedTimeStep = 1.0f / 60.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D")
	int32 SubStepCount = 4;

	/** box3d solver threads. Keep at 1 for a deterministic sim (server authority / rollback):
	 *  box3d partitions the constraint graph by worker count, so a different count changes the
	 *  result. Raise only for a single-machine sim that needs no cross-peer reproducibility. */
	UPROPERTY(EditAnywhere, Config, Category = "Box3D", meta = (ClampMin = "1"))
	int32 WorkerCount = 1;

	/** Spiral-of-death guard: never simulate more than this much time per frame. */
	UPROPERTY(EditAnywhere, Category = "Box3D")
	float MaxFrameTime = 0.25f;

	/** Gravity in Unreal space (cm/s^2). Converted to box3d meters on world create. */
	UPROPERTY(EditAnywhere, Category = "Box3D")
	FVector Gravity = FVector(0.0, 0.0, -980.0);

	/** Actors carrying this tag are bulk-registered as static box3d geometry on level
	 *  load/stream-in - no per-actor UBox3DBodyComponent needed. None (default) disables
	 *  the scan; the component-per-actor path stays primary (see doc §8). */
	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Bulk Static")
	FName StaticGeometryTag = NAME_None;

	/** Material applied to bulk-registered static bodies (the tagged-actor path has no
	 *  component to read per-actor material from). */
	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Bulk Static", meta = (ClampMin = "0.0"))
	float StaticGeometryFriction = 0.6f;

	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Bulk Static", meta = (ClampMin = "0.0"))
	float StaticGeometryRestitution = 0.0f;

	/** Pre-baked static collision assets to instantiate on world begin. Produced by the
	 *  bake commandlet; unlike the tag-scan path these need no runtime cooking, so they are
	 *  the packaged-build path for static geometry (doc §8, milestone 5). The material above
	 *  (StaticGeometryFriction/Restitution) is applied to their shapes. Loaded on top of
	 *  whatever bAutoDiscoverBakedCollision finds. */
	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Bulk Static")
	TArray<TSoftObjectPtr<UBox3DCollisionData>> BakedCollisionAssets;

	/** Also load the current map's baked asset (BC_<MapName> beside the map) without it being
	 *  listed above. On by default: forgetting to list an asset silently loads a level with no
	 *  static collision, which looks like a physics bug rather than a config mistake. */
	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Bulk Static")
	bool bAutoDiscoverBakedCollision = true;

	/** Dynamic bodies driven each frame. Weak so a destroyed actor drops out safely. */
	TArray<TWeakObjectPtr<UBox3DBodyComponent>> DynamicBodies;

	/** Kinematic bodies pushed from their actor transform before each step. */
	TArray<TWeakObjectPtr<UBox3DBodyComponent>> KinematicBodies;

	/** Every registered body (all types), used only for debug draw. */
	TArray<TWeakObjectPtr<UBox3DBodyComponent>> AllBodies;

	/** Actors that own compact arrays of Box3D bodies rendered through HISM instances. */
	TArray<TWeakObjectPtr<ABox3DInstancedBodyActor>> InstancedBodyActors;

	/** Networked Box3D state buffer: a bounded rewind history for server validation. */
	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Network")
	float NetworkRewindSeconds = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Box3D|Network", meta = (ClampMin = "1"))
	int32 NetworkRewindMaxFrames = 120;

	TMap<TWeakObjectPtr<UBox3DBodyComponent>, TArray<FBox3DHistoryFrame>> NetworkHistory;

	/** box3d resources for one level's bulk-registered static geometry. Not UPROPERTYs -
	 *  these are raw box3d handles this subsystem owns and destroys explicitly. */
	struct FBulkStaticLevel
	{
		TArray<b3BodyId> Bodies;
		TArray<b3MeshData*> Meshes; // tri-mesh data the shapes reference; freed after bodies
	};

	void CreateBulkStaticBody(AActor* Actor, FBulkStaticLevel& Bulk);

	TMap<TWeakObjectPtr<ULevel>, FBulkStaticLevel> BulkStaticLevels;

	/** Bodies instantiated from the baked collision assets (one bucket per loaded asset).
	 *  Owned like the bulk levels: freed in DestroyBox3DWorld. */
	TArray<FBulkStaticLevel> BakedStaticBuckets;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
};
