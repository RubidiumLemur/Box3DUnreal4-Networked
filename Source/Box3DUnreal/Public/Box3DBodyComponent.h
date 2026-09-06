// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include <box3d/box3d.h>
#include "Box3DBodyComponent.generated.h"

class AActor;
class UBox3DSubsystem;

/**
 * @brief Request payload sent by a client to the authoritative server.
 *
 * @details The client never owns the final physics state. It only describes the intent,
 * the simulation tick it was based on, and the timestamp it used to reconcile the request.
 * The server rewinds to the matching history frame, validates the action, and resolves the
 * authoritative outcome.
 */
USTRUCT(BlueprintType)
struct BOX3DUNREAL_API FBox3DNetworkInputRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int64 SimulationTick = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	double Timestamp = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	FVector DesiredLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	FRotator DesiredRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	FVector RequestedLinearVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	FVector RequestedAngularVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	FVector InputVector = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 SequenceNumber = 0;
};

/**
 * @brief Authoritative state snapshot replicated to relevant clients.
 *
 * @details This payload is sent from the server after validation and can be used by clients to
 * correct prediction and reconcile any unacknowledged local inputs against the authoritative sim.
 */
USTRUCT(BlueprintType)
struct BOX3DUNREAL_API FBox3DNetworkState
{
	GENERATED_BODY()

	static constexpr float PositionQuantizationScale = 0.25f;
	static constexpr float VelocityQuantizationScale = 1.0f;
	static constexpr float RotationQuantizationScale = 0.5f;
	static constexpr float RotationRange = 4096.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int64 SimulationTick = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	double Timestamp = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 SequenceNumber = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 LocationX = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 LocationY = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 LocationZ = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 RotationPitch = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 RotationYaw = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 RotationRoll = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 LinearVelocityX = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 LinearVelocityY = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 LinearVelocityZ = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 AngularVelocityX = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 AngularVelocityY = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	int32 AngularVelocityZ = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	uint8 Flags = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Box3D|Network")
	bool bSleeping = false;

	FORCEINLINE FVector GetLocation() const
	{
		return FVector(LocationX * PositionQuantizationScale,
			LocationY * PositionQuantizationScale,
			LocationZ * PositionQuantizationScale);
	}

	FORCEINLINE FRotator GetRotation() const
	{
		return FRotator(RotationPitch * RotationQuantizationScale,
			RotationYaw * RotationQuantizationScale,
			RotationRoll * RotationQuantizationScale);
	}

	FORCEINLINE FVector GetLinearVelocity() const
	{
		return FVector(LinearVelocityX * VelocityQuantizationScale,
			LinearVelocityY * VelocityQuantizationScale,
			LinearVelocityZ * VelocityQuantizationScale);
	}

	FORCEINLINE FVector GetAngularVelocity() const
	{
		return FVector(AngularVelocityX * VelocityQuantizationScale,
			AngularVelocityY * VelocityQuantizationScale,
			AngularVelocityZ * VelocityQuantizationScale);
	}

	FORCEINLINE bool IsSleeping() const { return bSleeping; }
	FORCEINLINE bool HasLinearVelocity() const { return (Flags & 0x01) != 0; }
	FORCEINLINE bool HasAngularVelocity() const { return (Flags & 0x02) != 0; }
	FORCEINLINE bool HasRotation() const { return (Flags & 0x04) != 0; }
	FORCEINLINE bool HasLocation() const { return (Flags & 0x08) != 0; }

	FORCEINLINE bool IsEquivalentTo(const FBox3DNetworkState& Other) const
	{
		return SimulationTick == Other.SimulationTick &&
			SequenceNumber == Other.SequenceNumber &&
			LocationX == Other.LocationX &&
			LocationY == Other.LocationY &&
			LocationZ == Other.LocationZ &&
			RotationPitch == Other.RotationPitch &&
			RotationYaw == Other.RotationYaw &&
			RotationRoll == Other.RotationRoll &&
			LinearVelocityX == Other.LinearVelocityX &&
			LinearVelocityY == Other.LinearVelocityY &&
			LinearVelocityZ == Other.LinearVelocityZ &&
			AngularVelocityX == Other.AngularVelocityX &&
			AngularVelocityY == Other.AngularVelocityY &&
			AngularVelocityZ == Other.AngularVelocityZ &&
			bSleeping == Other.bSleeping &&
			Flags == Other.Flags;
	}

	static FBox3DNetworkState Pack(
		const FVector& InLocation,
		const FRotator& InRotation,
		const FVector& InLinearVelocity,
		const FVector& InAngularVelocity,
		int64 InSimulationTick,
		double InTimestamp,
		int32 InSequenceNumber,
		bool bInSleeping)
	{
		FBox3DNetworkState State;
		State.SimulationTick = InSimulationTick;
		State.Timestamp = InTimestamp;
		State.SequenceNumber = InSequenceNumber;
		State.LocationX = FMath::RoundToInt(InLocation.X / PositionQuantizationScale);
		State.LocationY = FMath::RoundToInt(InLocation.Y / PositionQuantizationScale);
		State.LocationZ = FMath::RoundToInt(InLocation.Z / PositionQuantizationScale);
		State.RotationPitch = FMath::RoundToInt(InRotation.Pitch / RotationQuantizationScale);
		State.RotationYaw = FMath::RoundToInt(InRotation.Yaw / RotationQuantizationScale);
		State.RotationRoll = FMath::RoundToInt(InRotation.Roll / RotationQuantizationScale);
		State.LinearVelocityX = FMath::RoundToInt(InLinearVelocity.X / VelocityQuantizationScale);
		State.LinearVelocityY = FMath::RoundToInt(InLinearVelocity.Y / VelocityQuantizationScale);
		State.LinearVelocityZ = FMath::RoundToInt(InLinearVelocity.Z / VelocityQuantizationScale);
		State.AngularVelocityX = FMath::RoundToInt(InAngularVelocity.X / VelocityQuantizationScale);
		State.AngularVelocityY = FMath::RoundToInt(InAngularVelocity.Y / VelocityQuantizationScale);
		State.AngularVelocityZ = FMath::RoundToInt(InAngularVelocity.Z / VelocityQuantizationScale);
		State.bSleeping = bInSleeping;
		State.Flags = 0;
		State.Flags |= InLinearVelocity.IsNearlyZero() ? 0 : 0x01;
		State.Flags |= InAngularVelocity.IsNearlyZero() ? 0 : 0x02;
		State.Flags |= 0x04 | 0x08;
		return State;
	}
};

/**
 * @brief Runtime body type used by Box3D-backed actors.
 *
 * @details This enum describes how the owning actor participates in the box3d simulation.
 * Static bodies are fixed level collision, kinematic bodies are driven by gameplay transforms,
 * and dynamic bodies are solved by the box3d world.
 */
UENUM(BlueprintType)
enum class EBox3DBodyType : uint8
{
	Static,     // Level geometry: created once, never moved.
	Kinematic,  // Driven by gameplay (push is a later milestone).
	Dynamic     // Simulated by box3d; drives the owning actor.
};

/**
 * @brief Collision primitive shape used when creating a Box3D body.
 *
 * @details Auto chooses a practical default from the root primitive bounds, while Convex uses
 * the mesh's simple collision data when available and falls back to a box if no simple collision
 * is present.
 */
UENUM(BlueprintType)
enum class EBox3DShape : uint8
{
	Auto,    // Box derived from the root primitive's local bounds.
	Box,
	Sphere,
	Capsule,
	Convex   // Hull(s) from the mesh's simple convex collision (falls back to a box).
};

/**
 * @brief Source of the cooked collision mirrored by a static Box3D body.
 *
 * @details This mirrors the static geometry extraction choices used by the engine-side collision
 * pipeline. It allows the runtime system to either use simple collision data or a tri-mesh path,
 * depending on the desired fidelity and the content in the actor.
 */
UENUM(BlueprintType)
enum class EBox3DStaticSource : uint8
{
	Auto,             // Complex tri-mesh if present, else simple.
	SimpleCollision,  // AggGeom convex/box/sphere/capsule.
	ComplexCollision  // Cooked tri-mesh (meshes & landscape).
};

/**
 * @brief Component that binds an Unreal actor to a single Box3D rigid body.
 *
 * @details This component owns the body lifecycle, establishes the Box3D shape, handles
 * replication and server authority, and integrates with the subsystem's fixed-step simulation.
 * In the current implementation, the authoritative simulation runs on the server and clients
 * receive replicated state for reconciliation and rendering.
 */
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class BOX3DUNREAL_API UBox3DBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBox3DBodyComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	b3BodyId GetBodyId() const { return BodyId; }

	/**
	 * @brief Creates the underlying box3d rigid body for this component when the actor is
	 * eligible to simulate.
	 *
	 * @details Called from BeginPlay and when the subsystem toggles box3d back on. This is the
	 * runtime entry point for transforming the actor into a Box3D-backed body while preserving the
	 * rest of the Unreal actor configuration.
	 */
	void RebuildSimulationBody();
	/**
	 * @brief Destroys the Box3D body but keeps the component registered for a future rebuild.
	 *
	 * @details This supports runtime toggling without losing the component registration list.
	 * The actor is restored to its prior Unreal physics state when appropriate.
	 */
	void TeardownSimulationBody();

	/** @brief Captures the previous and current transforms used by the fixed-step simulation. */
	void CaptureStepTransform();
	/** @brief Interpolates between prior and current Box3D transforms for render-time smoothing. */
	void ApplyInterpolatedTransform(float Alpha);
	/** @brief Pushes a kinematic target transform toward the owner pose over a timestep. */
	void PushKinematicTarget(float TimeStep);
	/** @brief Draws the Box3D shape using the current simulation pose for debugging. */
	void DrawDebug() const;

	/**
	 * @brief Applies a linear impulse at the body's center of mass.
	 *
	 * @details This operation is authoritative: it succeeds only on the server or in standalone
	 * worlds and never lets a client directly modify Box3D state.
	 *
	 * @param Impulse Impulse in Unreal units, converted internally to Box3D units.
	 * @return True when the impulse was accepted and applied to a valid dynamic body.
	 */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Physics")
	bool AddImpulse(const FVector& Impulse);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D")
	EBox3DBodyType BodyType = EBox3DBodyType::Dynamic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D")
	EBox3DShape Shape = EBox3DShape::Auto;

	/** Box half-extents in cm (used when Shape == Box). */
	UPROPERTY(EditAnywhere, Category = "Box3D|Shape",
		meta = (EditCondition = "Shape == EBox3DShape::Box", EditConditionHides))
	FVector BoxHalfExtent = FVector(50.0, 50.0, 50.0);

	/** Radius in cm (used when Shape == Sphere or Capsule). */
	UPROPERTY(EditAnywhere, Category = "Box3D|Shape", meta = (ClampMin = "0.0",
		EditCondition = "Shape == EBox3DShape::Sphere || Shape == EBox3DShape::Capsule", EditConditionHides))
	float Radius = 50.0f;

	/** Half the distance between the capsule's hemisphere centers, in cm. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Shape", meta = (ClampMin = "0.0",
		EditCondition = "Shape == EBox3DShape::Capsule", EditConditionHides))
	float HalfHeight = 50.0f;

	/** Static bodies only: which cooked collision to mirror. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Static",
		meta = (EditCondition = "BodyType == EBox3DBodyType::Static", EditConditionHides))
	EBox3DStaticSource StaticSource = EBox3DStaticSource::Auto;

	/** Flip if dynamics fall through this mesh (one-sided triangles wound the wrong way). */
	UPROPERTY(EditAnywhere, Category = "Box3D|Static",
		meta = (EditCondition = "BodyType == EBox3DBodyType::Static", EditConditionHides))
	bool bInvertMeshWinding = false;

	/** Density in kg/m^3 (water ~= 1000). */
	UPROPERTY(EditAnywhere, Category = "Box3D|Material", meta = (ClampMin = "0.0"))
	float Density = 1000.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D|Material", meta = (ClampMin = "0.0"))
	float Friction = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Box3D|Material", meta = (ClampMin = "0.0"))
	float Restitution = 0.0f;

	/** Rolling resistance for spheres/capsules (ignored by other shapes). */
	UPROPERTY(EditAnywhere, Category = "Box3D|Material", meta = (ClampMin = "0.0"))
	float RollingResistance = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D|Damping", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D|Damping", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.05f;

	/**
	 * @brief Allows this body to enter Box3D's sleeping state when it becomes sufficiently still.
	 *
	 * @details Sleeping is enabled by default and reduces simulation work for resting dynamic
	 * bodies. The world-level sleeping setting must also remain enabled for this to take effect.
	 */
	UPROPERTY(EditAnywhere, Category = "Box3D|Sleeping",
		meta = (EditCondition = "BodyType == EBox3DBodyType::Dynamic", EditConditionHides))
	bool bEnableSleep = true;

	/**
	 * @brief Linear-speed threshold below which this body can become sleepy, in Unreal units per second.
	 *
	 * @details Box3D stores this threshold in meters per second; the integration converts this
	 * value from Unreal centimeters per second when creating the body. The default 5 cm/s matches
	 * Box3D's default 0.05 m/s threshold.
	 */
	UPROPERTY(EditAnywhere, Category = "Box3D|Sleeping", meta = (ClampMin = "0.0",
		EditCondition = "BodyType == EBox3DBodyType::Dynamic", EditConditionHides))
	float SleepThreshold = 5.0f;

	/** Category bitmask this body belongs to. 0 = collide with everything. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Collision", meta = (Bitmask))
	int32 CollisionCategory = 0;

	/** Categories this body collides with. 0 = collide with everything. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Collision", meta = (Bitmask))
	int32 CollisionMask = 0;

	/** box3d group index: >0 always collide, <0 never collide within the group, 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Collision")
	int32 CollisionGroup = 0;

	/**
	 * @brief Enables the server-authoritative networking path for this body.
	 *
	 * @details When enabled, the authority owns the body state and clients treat incoming state as
	 * authoritative for reconciliation rather than direct control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network")
	bool bEnableNetworkedSimulation = true;

	/**
	 * @brief Maximum squared-distance relevance limit used before a client performs extra validation work.
	 *
	 * @details This value defaults to 3000 Unreal units and can be overridden per component. It is
	 * used as a network relevance gate to reduce unnecessary validation and state replication.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network", meta = (ClampMin = "0.0"))
	float MaxServerDistance = 3000.0f;

	/**
	 * @brief Minimum elapsed time between authoritative state replicas for this body.
	 *
	 * @details This reduces unnecessary network traffic for debris and other non-player bodies while
	 * still keeping the authoritative state fresh enough for interpolation and reconciliation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReplicationIntervalSeconds = 0.05f;

	/**
	 * @brief Last authoritative network snapshot for this component.
	 *
	 * @details This replicated state is the server-chosen result that clients apply after prediction
	 * and reconciliation. It carries transform, velocities, tick, and sequence metadata.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_NetworkState, EditDefaultsOnly, BlueprintReadOnly, Category = "Box3D|Network")
	FBox3DNetworkState ReplicatedState;

	/**
	 * @brief Submits a client intent request to the authoritative server.
	 *
	 * @details The request contains the simulation tick, timestamp, desired transform, and velocity
	 * values that the client attempted. The server validates the request against the historical
	 * authoritative state and returns the corrected result.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSubmitInput(const FBox3DNetworkInputRequest& Request);

	/**
	 * @brief Delivers an authoritative corrected state back to a relevant client.
	 *
	 * @details The server sends the final state after validation/rewind. Clients use it to resolve
	 * any prediction errors and replay buffered inputs after the corrected tick.
	 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveAuthoritativeState(const FBox3DNetworkState& State);

	/**
	 * @brief Returns whether a component is within the configured network relevance radius.
	 *
	 * @param OtherActor The actor that is being checked against this component's authority range.
	 * @return True if the two actors are within the configured MaxServerDistance threshold.
	 */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Network")
	bool IsWithinMaxServerDistance(const AActor* OtherActor) const;

	/** @brief Applies a replicated authoritative state on clients after a network update. */
	UFUNCTION()
	void OnRep_NetworkState();

	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void CreateBody();
	void AddShape();
	/** Attach convex hull shape(s) from the mesh's simple collision. Returns false if
	 *  the mesh has no convex/box simple collision to build a hull from. */
	bool AddConvexShapes(const b3ShapeDef& ShapeDef);
	/** Cache the convex hull wireframe (local Unreal space) for debug draw. Runs on
	 *  server and client so both can show the shape; independent of body creation. */
	void BuildConvexDebugGeometry();
	void DestroyBody();
	void EnforceAuthorityContract();
	void EnableReplication();
	bool ComputeSimulationEligibility();
	FVector ComputeAutoBoxHalfExtent() const;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UBox3DSubsystem> Subsystem = nullptr;

	b3BodyId BodyId = b3_nullBodyId;

	/** Net-role eligibility, resolved once in BeginPlay. Independent of the master switch
	 *  and world validity so a runtime enable can still build this body. */
	bool bSimulationEligible = false;

	/** Whether the root was simulating Chaos physics before box3d took it over. Restored
	 *  on teardown so a runtime disable hands the actor back instead of freezing it. */
	bool bRestoreChaosSimulation = false;

	/** box3d carries no scale, so we preserve the spawn scale when writing back. */
	FVector SpawnScale = FVector::OneVector;

	/** Tri-mesh data referenced (not cloned) by static shapes; freed after the body. */
	TArray<b3MeshData*> OwnedMeshes;

	/** Last time this component replicated an authoritative state packet to relevant clients. */
	double LastReplicatedNetworkTime = -1.0;

	/** Last sequence number the server accepted for networked state replication. */
	int32 LastReplicatedSequence = -1;

	/** Client-side interpolation target for replicated authoritative state. */
	FVector SmoothedNetworkLocation = FVector::ZeroVector;

	/** Client-side rotation interpolation target for replicated authoritative state. */
	FRotator SmoothedNetworkRotation = FRotator::ZeroRotator;

	/** True once a valid replicated state has arrived for client-side smoothing. */
	bool bHasReceivedAuthoritativeState = false;

	/** Resolved box half-extents (cm) used for Auto/Box shapes; for debug draw. */
	FVector ResolvedHalfExtent = FVector(50.0, 50.0, 50.0);

	/** Convex hull wireframe as line-list pairs in local Unreal space (cm, scale baked).
	 *  Consecutive elements (2i, 2i+1) are one edge's endpoints. Empty unless Shape==Convex. */
	TArray<FVector> ConvexDebugSegments;

	/** Interpolation endpoints in Unreal space (last two fixed steps). */
	FTransform PrevTransform = FTransform::Identity;
	FTransform CurrTransform = FTransform::Identity;
};
