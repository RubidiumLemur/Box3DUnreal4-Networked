// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include <box3d/box3d.h>

class AActor;

// Extracts UE cooked collision into box3d static shapes. See the integration doc §8.
namespace Box3D::StaticGeometry
{
	// Which cooked collision to mirror for a Static body.
	enum class ESource : uint8
	{
		Auto,             // Complex tri-mesh if present, else simple.
		SimpleCollision,  // AggGeom convex/box/sphere/capsule.
		ComplexCollision, // Cooked tri-mesh (meshes & landscape).
	};

	// Attaches shapes from Owner's root primitive to Body. OutOwnedMeshes collects
	// b3MeshData the shapes reference (not cloned) for the caller to destroy later.
	// Returns true if any shape was created.
	BOX3DUNREAL_API bool AddStaticShapes(
		b3BodyId Body,
		const b3ShapeDef& Base,
		AActor* Owner,
		ESource Source,
		bool bInvertWinding,
		TArray<b3MeshData*>& OutOwnedMeshes);
}
