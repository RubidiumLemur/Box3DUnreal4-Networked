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
	Capsule
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

	/** Subsystem hook: roll prev<-curr and read the new post-step transform. */
	void CaptureStepTransform();
	/** Subsystem hook: write Lerp(prev, curr, Alpha) to the owning actor. */
	void ApplyInterpolatedTransform(float Alpha);
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

	UPROPERTY(EditAnywhere, Category = "Box3D|Damping", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D|Damping", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.05f;

protected:
	void CreateBody();
	void AddShape();
	void DestroyBody();
	void EnforceAuthorityContract();
	void EnableReplication();
	FVector ComputeAutoBoxHalfExtent() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBox3DSubsystem> Subsystem = nullptr;

	b3BodyId BodyId = b3_nullBodyId;

	/** box3d carries no scale, so we preserve the spawn scale when writing back. */
	FVector SpawnScale = FVector::OneVector;

	/** Tri-mesh data referenced (not cloned) by static shapes; freed after the body. */
	TArray<b3MeshData*> OwnedMeshes;

	/** Resolved box half-extents (cm) used for Auto/Box shapes; for debug draw. */
	FVector ResolvedHalfExtent = FVector(50.0, 50.0, 50.0);

	/** Interpolation endpoints in Unreal space (last two fixed steps). */
	FTransform PrevTransform = FTransform::Identity;
	FTransform CurrTransform = FTransform::Identity;
};
