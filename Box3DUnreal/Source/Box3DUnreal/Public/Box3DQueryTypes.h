// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Box3DQueryTypes.generated.h"

class AActor;

/**
 * Collision filtering for a spatial query. Mirrors the component's
 * CollisionCategory/CollisionMask convention: 0 keeps the box3d default (hit
 * everything), so a default-constructed filter queries the whole world.
 */
USTRUCT(BlueprintType)
struct BOX3DUNREAL_API FBox3DQueryFilter
{
	GENERATED_BODY()

	/** Category bits of the query itself. 0 = default (all categories). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Query", meta = (Bitmask))
	int32 Category = 0;

	/** Shape categories this query is allowed to hit. 0 = default (everything). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Query", meta = (Bitmask))
	int32 Mask = 0;
};

/** One hit from a box3d ray or shape cast, in Unreal world space (cm). */
USTRUCT(BlueprintType)
struct BOX3DUNREAL_API FBox3DHitResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	bool bHit = false;

	/** World point of intersection, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	FVector Location = FVector::ZeroVector;

	/** Surface normal at the hit point, unit length. */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	FVector Normal = FVector::ZeroVector;

	/** Distance from Start to the hit point, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	double Distance = 0.0;

	/** Hit position along Start->End, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	float Fraction = 0.0f;

	/** Actor owning the hit body. Null for baked static geometry, which has no
	 *  source actor at runtime (bodies are instantiated straight from the asset). */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	TObjectPtr<AActor> HitActor = nullptr;

	/** Triangle hit on a mesh/height-field shape, else -1. */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	int32 TriangleIndex = -1;

	/** Child shape hit on a compound (e.g. a multi-hull convex body), else -1. */
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Query")
	int32 ChildIndex = -1;
};
