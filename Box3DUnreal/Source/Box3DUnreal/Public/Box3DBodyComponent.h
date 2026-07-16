// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include <box3d/box3d.h>
#include "Box3DBodyComponent.generated.h"

class UBox3DSubsystem;

UENUM(BlueprintType)
enum class EBox3DBodyType : uint8
{
	Static,     // Level geometry: created once, never moved.
	Kinematic,  // Driven by gameplay (push is a later milestone).
	Dynamic     // Simulated by box3d; drives the owning actor.
};

UENUM(BlueprintType)
enum class EBox3DShape : uint8
{
	Auto,    // Box derived from the root primitive's local bounds.
	Box,
	Sphere,
	Capsule,
	Convex   // Hull(s) from the mesh's simple convex collision (falls back to a box).
};

// Which cooked collision a Static body mirrors. UENUM mirror of StaticGeometry::ESource.
UENUM(BlueprintType)
enum class EBox3DStaticSource : uint8
{
	Auto,             // Complex tri-mesh if present, else simple.
	SimpleCollision,  // AggGeom convex/box/sphere/capsule.
	ComplexCollision  // Cooked tri-mesh (meshes & landscape).
};

/**
 * Add to any actor to give it a box3d rigid body. On BeginPlay it creates the body
 * at the actor's transform and, for Dynamic bodies, registers with UBox3DSubsystem
 * which steps the sim and writes the interpolated result back to the actor.
 */
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class BOX3DUNREAL_API UBox3DBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBox3DBodyComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	b3BodyId GetBodyId() const { return BodyId; }

	/** Subsystem hook: build this component's box3d body if it is eligible and none
	 *  exists yet. Called from BeginPlay and on a runtime box3d.Enabled -> on toggle. */
	void RebuildSimulationBody();
	/** Subsystem hook: destroy the box3d body but stay registered for a later rebuild. */
	void TeardownSimulationBody();

	/** Subsystem hook: roll prev<-curr and read the new post-step transform. */
	void CaptureStepTransform();
	/** Subsystem hook: write Lerp(prev, curr, Alpha) to the owning actor. */
	void ApplyInterpolatedTransform(float Alpha);
	/** Subsystem hook: set the kinematic velocity to reach the actor pose in TimeStep. */
	void PushKinematicTarget(float TimeStep);
	/** Subsystem hook: draw this body's shape at the owning actor's current pose. */
	void DrawDebug() const;

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

	/** Category bitmask this body belongs to. 0 = collide with everything. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Collision", meta = (Bitmask))
	int32 CollisionCategory = 0;

	/** Categories this body collides with. 0 = collide with everything. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Collision", meta = (Bitmask))
	int32 CollisionMask = 0;

	/** box3d group index: >0 always collide, <0 never collide within the group, 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Box3D|Collision")
	int32 CollisionGroup = 0;

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
	TObjectPtr<UBox3DSubsystem> Subsystem = nullptr;

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

	/** Resolved box half-extents (cm) used for Auto/Box shapes; for debug draw. */
	FVector ResolvedHalfExtent = FVector(50.0, 50.0, 50.0);

	/** Convex hull wireframe as line-list pairs in local Unreal space (cm, scale baked).
	 *  Consecutive elements (2i, 2i+1) are one edge's endpoints. Empty unless Shape==Convex. */
	TArray<FVector> ConvexDebugSegments;

	/** Interpolation endpoints in Unreal space (last two fixed steps). */
	FTransform PrevTransform = FTransform::Identity;
	FTransform CurrTransform = FTransform::Identity;
};
