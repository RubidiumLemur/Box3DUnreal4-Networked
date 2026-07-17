// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Box3DCollisionData.generated.h"

// Baked static collision: UE cooked collision already converted into box3d local
// space (meters, scale baked, Y negated, winding fixed). Saved once in the editor
// so packaged builds never runtime-cook tri-meshes (GetPhysicsTriMeshData is
// editor/PIE-reliable only). See the integration doc §5, §8, milestone 5.

// One shape's kind. Mirrors what the extraction produces; the loader switches on it.
UENUM()
enum class EBox3DBakedShapeKind : uint8
{
	Hull,     // Convex hull rebuilt at load from Points (b3CreateHull is deterministic).
	Mesh,     // Static tri-mesh: Points = vertices, Indices = triangles (winding fixed).
	Sphere,   // CenterA + Radius.
	Capsule   // CenterA/CenterB (hemisphere centers) + Radius.
};

// One box3d shape in a body's local space. All coordinates are box3d meters with the
// owning actor's scale already baked in (box3d shapes carry no scale).
USTRUCT()
struct FBox3DBakedShape
{
	GENERATED_BODY()

	UPROPERTY()
	EBox3DBakedShapeKind Kind = EBox3DBakedShapeKind::Hull;

	// Hull point cloud, or Mesh vertices. Empty for sphere/capsule.
	UPROPERTY()
	TArray<FVector3f> Points;

	// Mesh triangle indices (3 per triangle). Empty unless Kind == Mesh.
	UPROPERTY()
	TArray<int32> Indices;

	// Sphere center / capsule first hemisphere center.
	UPROPERTY()
	FVector3f CenterA = FVector3f::ZeroVector;

	// Capsule second hemisphere center (unused otherwise).
	UPROPERTY()
	FVector3f CenterB = FVector3f::ZeroVector;

	// Sphere / capsule radius (meters).
	UPROPERTY()
	float Radius = 0.0f;
};

// One static body: the actor's world transform (scale baked into the shapes) plus its
// shapes. Self-contained, so the runtime instantiates it without the source actor.
USTRUCT()
struct FBox3DBakedBody
{
	GENERATED_BODY()

	// Actor world transform at bake time (Unreal space). Only location + rotation are
	// used to place the body; scale is baked into the shape geometry.
	UPROPERTY()
	FTransform WorldTransform = FTransform::Identity;

	// Source actor path within its level - metadata for rebake diffing, not used at load.
	UPROPERTY()
	FString ActorKey;

	UPROPERTY()
	TArray<FBox3DBakedShape> Shapes;
};

/**
 * Cached static collision for one level (or asset). Produced by the bake commandlet
 * from the level's Static-body actors and loaded at runtime by UBox3DSubsystem, which
 * instantiates every body directly - no per-actor components, no runtime cooking, so it
 * works in packaged builds and stays deterministic across runs.
 */
UCLASS(BlueprintType)
class BOX3DUNREAL_API UBox3DCollisionData : public UObject
{
	GENERATED_BODY()

public:
	// Level (or source) this was baked from, for identification in the editor.
	UPROPERTY(VisibleAnywhere, Category = "Box3D")
	FString SourceLevel;

	// box3d version string at bake time, so a version bump can be spotted as stale.
	UPROPERTY(VisibleAnywhere, Category = "Box3D")
	FString Box3DVersion;

	// True if any body was baked from complex (tri-mesh) collision.
	UPROPERTY(VisibleAnywhere, Category = "Box3D")
	bool bContainsTriMesh = false;

	UPROPERTY()
	TArray<FBox3DBakedBody> Bodies;
};
