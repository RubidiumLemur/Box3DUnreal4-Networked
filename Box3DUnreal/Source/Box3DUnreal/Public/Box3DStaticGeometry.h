// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include <box3d/box3d.h>

class AActor;
struct FBox3DBakedBody;

// Extracts UE cooked collision into box3d static shapes.
//
// Two stages, shared by the live and baked paths (milestone 5):
//   1. Extract  - UE cooked collision -> plain FBox3DBakedBody (box3d-space buffers).
//                 Editor/PIE only (GetPhysicsTriMeshData needs CPU-side collision).
//   2. Instantiate - FBox3DBakedBody -> box3d shapes on a body. Any platform.
// AddStaticShapes composes both for the un-baked runtime path.
namespace Box3D::StaticGeometry
{
	// Which cooked collision to mirror for a Static body.
	enum class ESource : uint8
	{
		Auto,             // Complex tri-mesh if present, else simple.
		SimpleCollision,  // AggGeom convex/box/sphere/capsule.
		ComplexCollision, // Cooked tri-mesh (meshes & landscape).
	};

	// Stage 1: convert Owner's root-primitive cooked collision into plain baked data
	// (Out.Shapes in box3d local space; Out.WorldTransform = actor transform). Editor/PIE
	// only. Returns true if any shape was extracted.
	BOX3DUNREAL_API bool ExtractStaticCollision(
		AActor* Owner,
		ESource Source,
		bool bInvertWinding,
		FBox3DBakedBody& Out);

	// Stage 2: attach baked shapes to an already-created Body. OutOwnedMeshes collects
	// b3MeshData the mesh shapes reference (not cloned) for the caller to destroy later.
	// Works on any platform - no UE collision access. Returns true if any shape was created.
	BOX3DUNREAL_API bool AddBakedShapes(
		b3BodyId Body,
		const b3ShapeDef& Base,
		const FBox3DBakedBody& Baked,
		TArray<b3MeshData*>& OutOwnedMeshes);

	// Live path: extract Owner's cooked collision and attach it to Body in one call
	// (ExtractStaticCollision + AddBakedShapes). Editor/PIE only; packaged builds use a
	// pre-baked UBox3DCollisionData asset via AddBakedShapes.
	BOX3DUNREAL_API bool AddStaticShapes(
		b3BodyId Body,
		const b3ShapeDef& Base,
		AActor* Owner,
		ESource Source,
		bool bInvertWinding,
		TArray<b3MeshData*>& OutOwnedMeshes);

	// "major.minor.revision" of the linked box3d - stamped into baked assets so a version
	// bump is visible as potentially stale collision. Wraps b3GetVersion (keeps box3d out
	// of the editor module, which only links the runtime module's exports).
	BOX3DUNREAL_API FString GetBox3DVersionString();
}
