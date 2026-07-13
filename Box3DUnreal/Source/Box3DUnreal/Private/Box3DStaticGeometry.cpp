// Author: Antonio Lattanzio - emptyvessel

#include "Box3DStaticGeometry.h"
#include "Box3DConversion.h"
#include "Box3DLog.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Interface_CollisionDataProviderCore.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"

namespace Box3D::StaticGeometry
{
	namespace
	{
		constexpr int32 MaxHullVertices = 64;

		// Local vertex (cm) -> box3d point (m): bake scale, negate Y (see Box3DConversion.h).
		FORCEINLINE b3Vec3 LocalToBox3D(const FVector& V, const FVector& Scale)
		{
			return b3Vec3{
				static_cast<float>(V.X * Scale.X * Box3D::UnrealToMeters),
				static_cast<float>(-V.Y * Scale.Y * Box3D::UnrealToMeters),
				static_cast<float>(V.Z * Scale.Z * Box3D::UnrealToMeters) };
		}

		// UE and box3d use opposite front-face winding; the negate-Y flip already reconciles
		// them, so only a mirrored (negative) scale needs reversing. bInvert = manual override.
		FORCEINLINE bool ShouldReverseWinding(const FVector& Scale, bool bInvert)
		{
			const bool bNegativeScale = (Scale.X * Scale.Y * Scale.Z) < 0.0;
			return bNegativeScale ^ bInvert;
		}

		IInterface_CollisionDataProvider* FindTriMeshProvider(UPrimitiveComponent* Prim)
		{
			// Landscape collision components implement the provider directly.
			if (IInterface_CollisionDataProvider* Direct = Cast<IInterface_CollisionDataProvider>(Prim))
			{
				return Direct;
			}
			if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Prim))
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					return Cast<IInterface_CollisionDataProvider>(Mesh);
				}
			}
			return nullptr;
		}

		// Convex hull from a point cloud. box3d clones the hull, so we free ours after.
		bool AddHullFromPoints(b3BodyId Body, const b3ShapeDef& Def, const TArray<FVector>& Points, const FVector& Scale)
		{
			if (Points.Num() < 4)
			{
				return false;
			}

			TArray<b3Vec3> Converted;
			Converted.Reserve(Points.Num());
			for (const FVector& P : Points)
			{
				Converted.Add(LocalToBox3D(P, Scale));
			}

			b3HullData* Hull = b3CreateHull(Converted.GetData(), Converted.Num(), MaxHullVertices);
			if (Hull == nullptr)
			{
				return false;
			}

			b3CreateHullShape(Body, &Def, Hull);
			b3DestroyHull(Hull);
			return true;
		}

		// Simple collision: AggGeom convex/box/sphere/capsule. Returns count created.
		int32 AddSimpleCollision(b3BodyId Body, const b3ShapeDef& Def, UPrimitiveComponent* Prim, const FVector& Scale)
		{
			UBodySetup* Setup = Prim->GetBodySetup();
			if (Setup == nullptr)
			{
				return 0;
			}

			const FKAggregateGeom& Agg = Setup->AggGeom;
			int32 Created = 0;

			// Convex: element transform places local verts into body space.
			for (const FKConvexElem& Convex : Agg.ConvexElems)
			{
				const FTransform ElemTM = Convex.GetTransform();
				TArray<FVector> Points;
				Points.Reserve(Convex.VertexData.Num());
				for (const FVector& V : Convex.VertexData)
				{
					Points.Add(ElemTM.TransformPosition(V));
				}
				Created += AddHullFromPoints(Body, Def, Points, Scale) ? 1 : 0;
			}

			// Boxes: 8 corners through the hull path.
			for (const FKBoxElem& Box : Agg.BoxElems)
			{
				const FTransform ElemTM(Box.Rotation, Box.Center);
				const FVector He(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f);
				TArray<FVector> Corners;
				Corners.Reserve(8);
				for (int32 Sx = -1; Sx <= 1; Sx += 2)
				for (int32 Sy = -1; Sy <= 1; Sy += 2)
				for (int32 Sz = -1; Sz <= 1; Sz += 2)
				{
					Corners.Add(ElemTM.TransformPosition(FVector(Sx * He.X, Sy * He.Y, Sz * He.Z)));
				}
				Created += AddHullFromPoints(Body, Def, Corners, Scale) ? 1 : 0;
			}

			// Non-uniform scale can't stay round, so radii use the min axis scale.
			const float RadialScale = static_cast<float>(Scale.GetAbsMin());
			const float M = static_cast<float>(Box3D::UnrealToMeters);
			for (const FKSphereElem& Sph : Agg.SphereElems)
			{
				b3Sphere Shape;
				Shape.center = LocalToBox3D(Sph.Center, Scale);
				Shape.radius = Sph.Radius * RadialScale * M;
				b3CreateSphereShape(Body, &Def, &Shape);
				++Created;
			}

			// Sphyls: local-Z axis, Length spans the two hemisphere centers.
			for (const FKSphylElem& Capsule : Agg.SphylElems)
			{
				const FTransform ElemTM(Capsule.Rotation, Capsule.Center);
				const float HalfLen = Capsule.Length * 0.5f;
				b3Capsule Shape;
				Shape.center1 = LocalToBox3D(ElemTM.TransformPosition(FVector(0, 0, +HalfLen)), Scale);
				Shape.center2 = LocalToBox3D(ElemTM.TransformPosition(FVector(0, 0, -HalfLen)), Scale);
				Shape.radius = Capsule.Radius * RadialScale * M;
				b3CreateCapsuleShape(Body, &Def, &Shape);
				++Created;
			}

			return Created;
		}

		// Complex collision: cooked tri-mesh -> b3Mesh. box3d references the mesh data
		// (doesn't clone), so it's handed back via OutOwnedMeshes for later destroy.
		bool AddComplexTriMesh(
			b3BodyId Body, const b3ShapeDef& Def, IInterface_CollisionDataProvider* Provider,
			const FVector& Scale, bool bInvertWinding, TArray<b3MeshData*>& OutOwnedMeshes)
		{
			if (Provider == nullptr || !Provider->ContainsPhysicsTriMeshData(true))
			{
				return false;
			}

			FTriMeshCollisionData TriData;
			if (!Provider->GetPhysicsTriMeshData(&TriData, /*InUseAllTriData=*/true))
			{
				return false;
			}
			if (TriData.Vertices.Num() < 3 || TriData.Indices.Num() < 1)
			{
				return false;
			}

			TArray<b3Vec3> Vertices;
			Vertices.Reserve(TriData.Vertices.Num());
			for (const FVector3f& V : TriData.Vertices)
			{
				Vertices.Add(LocalToBox3D(FVector(V), Scale));
			}

			const bool bReverse = ShouldReverseWinding(Scale, bInvertWinding);
			TArray<int32> Indices;
			Indices.Reserve(TriData.Indices.Num() * 3);
			for (const FTriIndices& Tri : TriData.Indices)
			{
				Indices.Add(Tri.v0);
				if (bReverse)
				{
					Indices.Add(Tri.v2);
					Indices.Add(Tri.v1);
				}
				else
				{
					Indices.Add(Tri.v1);
					Indices.Add(Tri.v2);
				}
			}

			b3MeshDef MeshDef{};
			MeshDef.vertices = Vertices.GetData();
			MeshDef.indices = Indices.GetData();
			MeshDef.materialIndices = nullptr; // base material for all triangles
			MeshDef.weldTolerance = 0.0f;
			MeshDef.vertexCount = Vertices.Num();
			MeshDef.triangleCount = Indices.Num() / 3;
			MeshDef.weldVertices = false;
			MeshDef.useMedianSplit = false;
			MeshDef.identifyEdges = true;   // adjacency avoids ghost collisions

			b3MeshData* Mesh = b3CreateMesh(&MeshDef, nullptr, 0);
			if (Mesh == nullptr)
			{
				return false;
			}

			// Scale already baked into verts, so unit shape scale.
			b3CreateMeshShape(Body, &Def, Mesh, b3Vec3{ 1.0f, 1.0f, 1.0f });
			OutOwnedMeshes.Add(Mesh);
			return true;
		}
	} // namespace

	bool AddStaticShapes(
		b3BodyId Body, const b3ShapeDef& Base, AActor* Owner, ESource Source,
		bool bInvertWinding, TArray<b3MeshData*>& OutOwnedMeshes)
	{
		UPrimitiveComponent* Prim = Owner ? Cast<UPrimitiveComponent>(Owner->GetRootComponent()) : nullptr;
		if (Prim == nullptr)
		{
			return false;
		}

		const FVector Scale = Prim->GetComponentScale();

		if (Source == ESource::ComplexCollision || Source == ESource::Auto)
		{
			IInterface_CollisionDataProvider* Provider = FindTriMeshProvider(Prim);
			if (AddComplexTriMesh(Body, Base, Provider, Scale, bInvertWinding, OutOwnedMeshes))
			{
				return true;
			}
			if (Source == ESource::ComplexCollision)
			{
				UE_LOG(LogBox3D, Warning,
					TEXT("%s: no complex (tri-mesh) collision available; static body has no shape. ")
					TEXT("Enable 'Allow CPU Access' / complex collision on the mesh, or use Simple/Auto."),
					*GetNameSafe(Owner));
				return false;
			}
		}

		// Auto fell through, or SimpleCollision requested.
		const int32 SimpleCount = AddSimpleCollision(Body, Base, Prim, Scale);
		if (SimpleCount > 0)
		{
			return true;
		}

		UE_LOG(LogBox3D, Warning,
			TEXT("%s: no cooked collision found for static body (no tri-mesh, no simple primitives)."),
			*GetNameSafe(Owner));
		return false;
	}
} // namespace Box3D::StaticGeometry
