// Author: Antonio Lattanzio - emptyvessel

#include "Box3DBodyComponent.h"
#include "Box3DSubsystem.h"
#include "Box3DConversion.h"
#include "Box3DStaticGeometry.h"
#include "Box3DLog.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"

namespace
{
	// Cap the hull vertex count, matching the static-geometry path.
	constexpr int32 ConvexMaxHullVertices = 64;

	// Local vertex (cm) -> box3d point (m): bake scale, negate Y (see Box3DConversion.h).
	FORCEINLINE b3Vec3 ConvexLocalToBox3D(const FVector& V, const FVector& Scale)
	{
		return b3Vec3{
			static_cast<float>(V.X * Scale.X * Box3D::UnrealToMeters),
			static_cast<float>(-V.Y * Scale.Y * Box3D::UnrealToMeters),
			static_cast<float>(V.Z * Scale.Z * Box3D::UnrealToMeters) };
	}

	// Gather one primitive's simple convex/box collision as box3d-space point clouds, one
	// per element. Convex hulls from render verts are wrong; this uses the cooked simple
	// collision.
	//
	// SourceToRoot places the primitive's unscaled local space into the root's unscaled
	// local space (identity for the root itself); RootScale is then baked in, since box3d
	// shapes carry no scale.
	void GatherPrimitiveClouds(UPrimitiveComponent* Prim, const FTransform& SourceToRoot,
		const FVector& RootScale, TArray<TArray<b3Vec3>>& OutClouds)
	{
		UBodySetup* Setup = Prim ? Prim->GetBodySetup() : nullptr;
		if (Setup == nullptr)
		{
			return;
		}

		const FKAggregateGeom& Agg = Setup->AggGeom;

		// Convex: element transform places local verts into body space.
		for (const FKConvexElem& Convex : Agg.ConvexElems)
		{
			if (Convex.VertexData.Num() < 4)
			{
				continue;
			}
			const FTransform ElemTM = Convex.GetTransform();
			TArray<b3Vec3>& Cloud = OutClouds.AddDefaulted_GetRef();
			Cloud.Reserve(Convex.VertexData.Num());
			for (const FVector& V : Convex.VertexData)
			{
				Cloud.Add(ConvexLocalToBox3D(SourceToRoot.TransformPosition(ElemTM.TransformPosition(V)), RootScale));
			}
		}

		// Boxes: 8 corners into a hull (box simple collision, e.g. the default).
		for (const FKBoxElem& Box : Agg.BoxElems)
		{
			const FTransform ElemTM(Box.Rotation, Box.Center);
			const FVector He(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f);
			TArray<b3Vec3>& Cloud = OutClouds.AddDefaulted_GetRef();
			Cloud.Reserve(8);
			for (int32 Sx = -1; Sx <= 1; Sx += 2)
			for (int32 Sy = -1; Sy <= 1; Sy += 2)
			for (int32 Sz = -1; Sz <= 1; Sz += 2)
			{
				const FVector Corner(Sx * He.X, Sy * He.Y, Sz * He.Z);
				Cloud.Add(ConvexLocalToBox3D(SourceToRoot.TransformPosition(ElemTM.TransformPosition(Corner)), RootScale));
			}
		}
	}

	// Collect the root's clouds and, when bIncludeChildren, every attached primitive
	// descendant's as well - the compound case (e.g. a group of fracture pieces simulated
	// as one body). The root may be a bare USceneComponent, which contributes no geometry
	// of its own but gives the compound a stable pivot that outlives any single piece.
	void GatherConvexPointClouds(USceneComponent* Root, bool bIncludeChildren,
		TArray<TArray<b3Vec3>>& OutClouds)
	{
		if (Root == nullptr)
		{
			return;
		}

		const FVector RootScale = Root->GetComponentScale();
		if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Root))
		{
			GatherPrimitiveClouds(RootPrim, FTransform::Identity, RootScale, OutClouds);
		}

		if (!bIncludeChildren)
		{
			return;
		}

		TArray<USceneComponent*> Children;
		Root->GetChildrenComponents(/*bIncludeAllDescendants=*/true, Children);

		const FTransform RootTM = Root->GetComponentTransform();
		for (USceneComponent* Child : Children)
		{
			UPrimitiveComponent* ChildPrim = Cast<UPrimitiveComponent>(Child);
			if (ChildPrim == nullptr || ChildPrim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				continue;
			}
			// Relative transform divides out the root's scale, so the result is in the
			// root's UNSCALED local space - exactly what GatherPrimitiveClouds expects
			// before it bakes RootScale.
			GatherPrimitiveClouds(ChildPrim, ChildPrim->GetComponentTransform().GetRelativeTransform(RootTM),
				RootScale, OutClouds);
		}
	}
} // namespace

UBox3DBodyComponent::UBox3DBodyComponent()
{
	// The subsystem drives capture/interpolation; the component itself never ticks.
	PrimaryComponentTick.bCanEverTick = false;
}

void UBox3DBodyComponent::BeginPlay()
{
	Super::BeginPlay();

	Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UBox3DSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		return;
	}

	// Resolve the box extent now so debug draw works even on clients, which create
	// no body but still show the replicated actor pose. Convex resolves it too as the
	// fallback box used when the mesh has no simple convex collision.
	if (Shape == EBox3DShape::Box)
	{
		// Author-specified extents are centred on the body origin by definition.
		ResolvedHalfExtent = BoxHalfExtent.GetAbs();
		ResolvedBoxCenter = FVector::ZeroVector;
	}
	else if (Shape == EBox3DShape::Auto || Shape == EBox3DShape::Convex)
	{
		ResolveAutoBoxBounds();
	}

	// Cache the convex wireframe on both server and client so both can draw the shape.
	if (Shape == EBox3DShape::Convex)
	{
		BuildConvexDebugGeometry();
	}

	// Register for debug draw in every world (server and client).
	Subsystem->RegisterBody(this);

	// Resolve net-role eligibility once. It doesn't depend on the master switch or world
	// validity, so a runtime 'box3d.Enabled 1' can still build this body later.
	bSimulationEligible = ComputeSimulationEligibility();

	// Build the body now unless box3d is globally disabled; if it is, the subsystem
	// rebuilds every eligible body when the switch is turned back on.
	if (bSimulationEligible && UBox3DSubsystem::IsBox3DEnabled())
	{
		RebuildSimulationBody();
	}
}

bool UBox3DBodyComponent::ComputeSimulationEligibility()
{
	// Only the authority simulates; a client displays replicated poses. HasAuthority() is
	// the per-actor gate, but it only tells server from client when the actor is
	// REPLICATED - a non-replicated actor reports authority on every instance, so each
	// would run its own (diverging) body. The explicit NM_Client block catches pure
	// clients regardless; the warning below flags the remaining hole (level actors).
	AActor* Owner = GetOwner();
	const ENetMode NetMode = GetWorld()->GetNetMode();
	if (Owner == nullptr || NetMode == NM_Client || !Owner->HasAuthority())
	{
		return false;
	}

	// Level-placed actors can't be fixed by runtime SetReplicates - the client instance
	// already began play as authority and will simulate a duplicate. Must be set in editor.
	if (NetMode != NM_Standalone && Owner->IsNetStartupActor() && !Owner->GetIsReplicated())
	{
		UE_LOG(LogBox3D, Warning,
			TEXT("%s: level-placed body actor is not replicated; the client will simulate a duplicate, ")
			TEXT("out-of-sync body. Enable 'Replicates' on the actor in the editor."),
			*GetNameSafe(Owner));
	}

	return true;
}

void UBox3DBodyComponent::RebuildSimulationBody()
{
	// Idempotent: skip if a body already exists, the role is ineligible, or the world
	// isn't up (e.g. box3d still disabled). The subsystem calls this on a runtime enable.
	if (B3_IS_NON_NULL(BodyId) || !bSimulationEligible || Subsystem == nullptr || !Subsystem->IsWorldValid())
	{
		return;
	}

	CreateBody();
	if (B3_IS_NULL(BodyId))
	{
		return;
	}

	if (bIsSensor && BodyType == EBox3DBodyType::Dynamic)
	{
		UE_LOG(LogBox3D, Warning,
			TEXT("%s: a trigger never collides, so this Dynamic body will fall through the level. ")
			TEXT("Use Static or Kinematic."),
			*GetNameSafe(GetOwner()));
	}

	AddShape();

	// Dynamic and kinematic both move at runtime and must stream to clients. The
	// authority contract (Chaos off, Movable) matters for BOTH: with Chaos simulating,
	// SetReplicateMovement flips into physics-state replication, which expects the client
	// to simulate - so the client, which never does, would never follow the server.
	if (BodyType == EBox3DBodyType::Dynamic || BodyType == EBox3DBodyType::Kinematic)
	{
		EnforceAuthorityContract();
		EnableReplication();

		if (BodyType == EBox3DBodyType::Dynamic)
		{
			Subsystem->RegisterDynamicBody(this);   // box3d writes the actor each step

			// Flush any force/velocity requested before the body existed.
			if (!PendingLinearImpulse.IsNearlyZero())
			{
				AddImpulse(PendingLinearImpulse, /*bWake=*/true);
				PendingLinearImpulse = FVector::ZeroVector;
			}
			if (bHasPendingAngularVelocity)
			{
				SetAngularVelocity(PendingAngularVelocity, /*bWake=*/true);
				bHasPendingAngularVelocity = false;
			}
		}
		else
		{
			Subsystem->RegisterKinematicBody(this); // gameplay pose pushed in each step
		}
	}
}

void UBox3DBodyComponent::AddImpulse(const FVector& Impulse, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic)
	{
		return;
	}
	if (B3_IS_NULL(BodyId))
	{
		PendingLinearImpulse += Impulse; // apply once the body is built
		return;
	}
	b3Body_ApplyLinearImpulseToCenter(BodyId, Box3D::ToBox3DVector(Impulse), bWake);
}

void UBox3DBodyComponent::AddImpulseAtLocation(const FVector& Impulse, const FVector& WorldLocation, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic || B3_IS_NULL(BodyId))
	{
		return; // not queued: the world point would be stale by the time the body exists
	}
	b3Body_ApplyLinearImpulse(BodyId, Box3D::ToBox3DVector(Impulse),
		Box3D::ToBox3DPosition(WorldLocation), bWake);
}

void UBox3DBodyComponent::AddForce(const FVector& Force, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic || B3_IS_NULL(BodyId))
	{
		return; // not queued: a force lasts one step, so it would land at the wrong time
	}
	b3Body_ApplyForceToCenter(BodyId, Box3D::ToBox3DVector(Force), bWake);
}

void UBox3DBodyComponent::AddTorque(const FVector& Torque, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic || B3_IS_NULL(BodyId))
	{
		return;
	}
	// N*m = kg*m^2/s^2, so the cm->m factor applies twice.
	constexpr float TorqueToBox3D = static_cast<float>(Box3D::UnrealToMeters * Box3D::UnrealToMeters);
	b3Body_ApplyTorque(BodyId, b3MulSV(TorqueToBox3D, Box3D::ToBox3DAngular(Torque)), bWake);
}

void UBox3DBodyComponent::AddAngularImpulse(const FVector& AngularImpulse, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic || B3_IS_NULL(BodyId))
	{
		return;
	}
	// Same squared length factor as torque.
	constexpr float ImpulseToBox3D = static_cast<float>(Box3D::UnrealToMeters * Box3D::UnrealToMeters);
	b3Body_ApplyAngularImpulse(BodyId, b3MulSV(ImpulseToBox3D, Box3D::ToBox3DAngular(AngularImpulse)), bWake);
}

void UBox3DBodyComponent::SetLinearVelocity(const FVector& Velocity, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic || B3_IS_NULL(BodyId))
	{
		return;
	}
	b3Body_SetLinearVelocity(BodyId, Box3D::ToBox3DVector(Velocity));
	if (bWake)
	{
		b3Body_SetAwake(BodyId, true);
	}
}

void UBox3DBodyComponent::SetAngularVelocity(const FVector& AngularVelocity, bool bWake)
{
	if (BodyType != EBox3DBodyType::Dynamic)
	{
		return;
	}
	if (B3_IS_NULL(BodyId))
	{
		PendingAngularVelocity = AngularVelocity;
		bHasPendingAngularVelocity = true;
		return;
	}
	// Axial, and rad/s is scale-free - no cm<->m factor here.
	b3Body_SetAngularVelocity(BodyId, Box3D::ToBox3DAngular(AngularVelocity));
	if (bWake)
	{
		b3Body_SetAwake(BodyId, true);
	}
}

void UBox3DBodyComponent::SetGravityScale(float Scale)
{
	if (B3_IS_NON_NULL(BodyId))
	{
		b3Body_SetGravityScale(BodyId, Scale);
		b3Body_SetAwake(BodyId, true); // a resting body would ignore the change
	}
}

void UBox3DBodyComponent::SetSleepEnabled(bool bEnabled)
{
	if (B3_IS_NON_NULL(BodyId))
	{
		b3Body_EnableSleep(BodyId, bEnabled);
	}
}

void UBox3DBodyComponent::TeleportBody(const FVector& Location, const FRotator& Rotation)
{
	if (B3_IS_NULL(BodyId))
	{
		return;
	}

	const FQuat Quat = Rotation.Quaternion();
	b3Body_SetTransform(BodyId, Box3D::ToBox3DPosition(Location), Box3D::ToBox3DQuat(Quat));
	b3Body_SetLinearVelocity(BodyId, b3Vec3_zero);
	b3Body_SetAngularVelocity(BodyId, b3Vec3_zero);
	b3Body_SetAwake(BodyId, true);

	// Collapse both interpolation endpoints onto the new pose, or the next frame blends
	// from the old one and the actor slides across the level.
	PrevTransform = CurrTransform = FTransform(Quat, Location, SpawnScale);
	GetOwner()->SetActorLocationAndRotation(Location, Quat, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
}

FVector UBox3DBodyComponent::GetLinearVelocity() const
{
	if (B3_IS_NULL(BodyId))
	{
		return FVector::ZeroVector;
	}
	return Box3D::FromBox3DVector(b3Body_GetLinearVelocity(BodyId));
}

FVector UBox3DBodyComponent::GetAngularVelocity() const
{
	if (B3_IS_NULL(BodyId))
	{
		return FVector::ZeroVector;
	}
	return Box3D::FromBox3DAngular(b3Body_GetAngularVelocity(BodyId));
}

float UBox3DBodyComponent::GetBodyMass() const
{
	return B3_IS_NULL(BodyId) ? 0.0f : b3Body_GetMass(BodyId);
}

bool UBox3DBodyComponent::IsBodyAwake() const
{
	return B3_IS_NON_NULL(BodyId) && b3Body_IsAwake(BodyId);
}

void UBox3DBodyComponent::WakeBody()
{
	if (B3_IS_NON_NULL(BodyId))
	{
		b3Body_SetAwake(BodyId, true);
	}
}

void UBox3DBodyComponent::RebuildShapes()
{
	if (B3_IS_NULL(BodyId))
	{
		return;
	}

	const int32 Count = b3Body_GetShapeCount(BodyId);
	if (Count > 0)
	{
		TArray<b3ShapeId> Shapes;
		Shapes.SetNumUninitialized(Count);
		const int32 Fetched = b3Body_GetShapes(BodyId, Shapes.GetData(), Count);
		for (int32 i = 0; i < Fetched; ++i)
		{
			// Defer the mass update to the single ApplyMassFromShapes below.
			b3DestroyShape(Shapes[i], /*updateBodyMass=*/false);
		}
	}

	// Static bodies may hold tri-mesh data referenced by the shapes we just destroyed.
	for (b3MeshData* Mesh : OwnedMeshes)
	{
		if (Mesh != nullptr)
		{
			b3DestroyMesh(Mesh);
		}
	}
	OwnedMeshes.Reset();

	if (Shape == EBox3DShape::Auto || Shape == EBox3DShape::Convex)
	{
		ResolveAutoBoxBounds();
	}
	if (Shape == EBox3DShape::Convex)
	{
		BuildConvexDebugGeometry();
	}

	AddShape();
	b3Body_ApplyMassFromShapes(BodyId);
	b3Body_SetAwake(BodyId, true);
}

void UBox3DBodyComponent::TeardownSimulationBody()
{
	// Hand the actor back to its native Chaos physics so a runtime disable is a true
	// "without box3d", not a frozen pose. Only actors box3d actually took over (dynamic/
	// kinematic, and only those that were simulating) get restored.
	if (bRestoreChaosSimulation)
	{
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
		{
			Root->SetSimulatePhysics(true);
		}
		bRestoreChaosSimulation = false;
	}

	// Destroy the box3d body but stay registered (subsystem keeps us in AllBodies) so a
	// later 'box3d.Enabled 1' rebuilds. DestroyBody nulls BodyId and frees owned meshes.
	DestroyBody();
}

void UBox3DBodyComponent::EnableReplication()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || GetWorld()->GetNetMode() == NM_Standalone)
	{
		return; // single-player: nothing to replicate
	}

	// Replication is an actor-level flag (no component checkbox). We're on the authority
	// here, so enable it if the author didn't, then let UE stream the transform to clients.
	if (!Owner->GetIsReplicated())
	{
		Owner->SetReplicates(true);
		UE_LOG(LogBox3D, Log,
			TEXT("%s: enabling actor replication so clients receive box3d movement."),
			*GetNameSafe(Owner));
	}

	Owner->SetReplicateMovement(true);
}

void UBox3DBodyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Subsystem != nullptr)
	{
		Subsystem->UnregisterBody(this);
	}
	DestroyBody();

	Super::EndPlay(EndPlayReason);
}

void UBox3DBodyComponent::CreateBody()
{
	const FTransform ActorXform = GetOwner()->GetActorTransform();
	SpawnScale = ActorXform.GetScale3D();
	PrevTransform = CurrTransform = ActorXform;

	b3BodyDef Def = b3DefaultBodyDef();
	switch (BodyType)
	{
	case EBox3DBodyType::Static:    Def.type = b3_staticBody;    break;
	case EBox3DBodyType::Kinematic: Def.type = b3_kinematicBody; break;
	default:                        Def.type = b3_dynamicBody;   break;
	}
	Def.position = Box3D::ToBox3DPosition(ActorXform.GetLocation());
	Def.rotation = Box3D::ToBox3DQuat(ActorXform.GetRotation());
	Def.linearDamping = LinearDamping;
	Def.angularDamping = AngularDamping;

	// Lets a query hit resolve back to the actor. Safe because DestroyBody runs on EndPlay,
	// so the body never outlives the owner.
	Def.userData = GetOwner();

	BodyId = b3CreateBody(Subsystem->GetWorldId(), &Def);
	bWasAwake = true; // bodies are created awake
}

void UBox3DBodyComponent::AddShape()
{
	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	ShapeDef.density = Density;
	ShapeDef.baseMaterial.friction = Friction;
	ShapeDef.baseMaterial.restitution = Restitution;
	ShapeDef.baseMaterial.rollingResistance = RollingResistance;

	// Lets an event resolve its shape back to this component. Safe for the same reason the
	// body's actor pointer is: the shapes die with the body in DestroyBody.
	ShapeDef.userData = this;

	// A trigger with overlaps off would report nothing, so force it on there.
	ShapeDef.isSensor = bIsSensor;
	ShapeDef.enableSensorEvents = bIsSensor || bGenerateSensorEvents;
	ShapeDef.enableContactEvents = bGenerateContactEvents;
	ShapeDef.enableHitEvents = bGenerateHitEvents;

	// Opt-in collision filtering; 0/0/0 leaves the default (collide with everything).
	if (CollisionCategory != 0 || CollisionMask != 0 || CollisionGroup != 0)
	{
		b3Filter Filter = b3DefaultFilter();
		if (CollisionCategory != 0)
		{
			Filter.categoryBits = static_cast<uint64>(static_cast<uint32>(CollisionCategory));
		}
		if (CollisionMask != 0)
		{
			Filter.maskBits = static_cast<uint64>(static_cast<uint32>(CollisionMask));
		}
		Filter.groupIndex = CollisionGroup;
		ShapeDef.filter = Filter;
	}

	// Static bodies mirror the actor's cooked collision; fall through to a primitive
	// Shape only if extraction finds nothing.
	if (BodyType == EBox3DBodyType::Static)
	{
		const auto Source = static_cast<Box3D::StaticGeometry::ESource>(StaticSource);
		if (Box3D::StaticGeometry::AddStaticShapes(BodyId, ShapeDef, GetOwner(), Source, bInvertMeshWinding, OwnedMeshes))
		{
			return;
		}
	}

	const float M = static_cast<float>(Box3D::UnrealToMeters);

	switch (Shape)
	{
	case EBox3DShape::Sphere:
	{
		b3Sphere Sphere;
		Sphere.center = b3Vec3{ 0.0f, 0.0f, 0.0f };
		Sphere.radius = Radius * M;
		b3CreateSphereShape(BodyId, &ShapeDef, &Sphere);
		break;
	}
	case EBox3DShape::Capsule:
	{
		// Capsule along the actor's local Z. Z maps straight through (only Y is
		// negated for handedness), so it stands upright in Unreal.
		const float HalfH = HalfHeight * M;
		b3Capsule Capsule;
		Capsule.center1 = b3Vec3{ 0.0f, 0.0f, +HalfH };
		Capsule.center2 = b3Vec3{ 0.0f, 0.0f, -HalfH };
		Capsule.radius = Radius * M;
		b3CreateCapsuleShape(BodyId, &ShapeDef, &Capsule);
		break;
	}
	case EBox3DShape::Convex:
	{
		if (AddConvexShapes(ShapeDef))
		{
			break;
		}
		UE_LOG(LogBox3D, Warning,
			TEXT("%s: Convex shape resolved no usable hull from the root's simple collision; ")
			TEXT("falling back to a bounds box. Add convex simple collision to the mesh (a hull ")
			TEXT("needs 4+ non-coplanar verts), or use a different shape."),
			*GetNameSafe(GetOwner()));
		// Offset, not b3MakeBoxHull: ResolvedBoxCenter puts the box on the mesh rather
		// than on the component pivot, which they only share when the mesh is centred.
		const b3BoxHull FallbackHull = b3MakeOffsetBoxHull(
			ResolvedHalfExtent.X * M, ResolvedHalfExtent.Y * M, ResolvedHalfExtent.Z * M,
			Box3D::ToBox3DVector(ResolvedBoxCenter));
		b3CreateHullShape(BodyId, &ShapeDef, &FallbackHull.base);
		break;
	}
	default: // Auto / Box
	{
		// ResolvedHalfExtent / ResolvedBoxCenter were resolved in BeginPlay (also used
		// by debug draw). The centre is zero for an explicit Box.
		const b3BoxHull Hull = b3MakeOffsetBoxHull(
			ResolvedHalfExtent.X * M, ResolvedHalfExtent.Y * M, ResolvedHalfExtent.Z * M,
			Box3D::ToBox3DVector(ResolvedBoxCenter));
		b3CreateHullShape(BodyId, &ShapeDef, &Hull.base);
		break;
	}
	}
}

bool UBox3DBodyComponent::AddConvexShapes(const b3ShapeDef& ShapeDef)
{
	TArray<TArray<b3Vec3>> Clouds;
	GatherConvexPointClouds(GetOwner()->GetRootComponent(), bConvexIncludesAttachedChildren, Clouds);

	// One hull shape per element (a compound), matching the mesh's simple collision.
	int32 Created = 0;
	for (const TArray<b3Vec3>& Cloud : Clouds)
	{
		if (Cloud.Num() < 4)
		{
			continue;
		}
		b3HullData* Hull = b3CreateHull(Cloud.GetData(), Cloud.Num(), ConvexMaxHullVertices);
		if (Hull == nullptr)
		{
			continue;
		}
		b3CreateHullShape(BodyId, &ShapeDef, Hull); // box3d clones the hull; free ours after
		b3DestroyHull(Hull);
		++Created;
	}

	return Created > 0;
}

void UBox3DBodyComponent::BuildConvexDebugGeometry()
{
	ConvexDebugSegments.Reset();

	TArray<TArray<b3Vec3>> Clouds;
	GatherConvexPointClouds(GetOwner()->GetRootComponent(), bConvexIncludesAttachedChildren, Clouds);

	// Rebuild each hull just to read back its computed edges - b3CreateHull is a standalone
	// utility (no world), so this runs on clients too. Emit each undirected edge once by
	// only taking the half-edge whose index is below its twin.
	for (const TArray<b3Vec3>& Cloud : Clouds)
	{
		if (Cloud.Num() < 4)
		{
			continue;
		}
		b3HullData* Hull = b3CreateHull(Cloud.GetData(), Cloud.Num(), ConvexMaxHullVertices);
		if (Hull == nullptr)
		{
			continue;
		}

		const b3Vec3* Points = b3GetHullPoints(Hull);
		const b3HullHalfEdge* Edges = b3GetHullEdges(Hull);
		if (Points != nullptr && Edges != nullptr)
		{
			for (int32 E = 0; E < Hull->edgeCount; ++E)
			{
				if (E < Edges[E].twin)
				{
					// Points carry the baked scale; convert back to local Unreal cm.
					ConvexDebugSegments.Add(Box3D::FromBox3DVector(Points[Edges[E].origin]));
					ConvexDebugSegments.Add(Box3D::FromBox3DVector(Points[Edges[Edges[E].twin].origin]));
				}
			}
		}

		b3DestroyHull(Hull);
	}
}

void UBox3DBodyComponent::DestroyBody()
{
	// Only touch box3d while the world still exists (component EndPlay precedes
	// subsystem Deinitialize, but guard anyway).
	if (B3_IS_NON_NULL(BodyId) && Subsystem != nullptr && Subsystem->IsWorldValid())
	{
		b3DestroyBody(BodyId);
	}
	BodyId = b3_nullBodyId;

	// Free tri-mesh data after the body: its mesh shapes referenced this memory.
	for (b3MeshData* Mesh : OwnedMeshes)
	{
		if (Mesh != nullptr)
		{
			b3DestroyMesh(Mesh);
		}
	}
	OwnedMeshes.Reset();
}

void UBox3DBodyComponent::EnforceAuthorityContract()
{
	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (Root == nullptr)
	{
		return;
	}

	// box3d is the sole mover; make sure Chaos isn't also simulating this actor. Remember
	// the prior state so a runtime disable can hand the actor back (see TeardownSimulationBody).
	bRestoreChaosSimulation = Root->IsSimulatingPhysics();
	Root->SetSimulatePhysics(false);

	if (Root->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogBox3D, Warning,
			TEXT("%s: root is not Movable; its transform can't change at runtime. Set Mobility to Movable."),
			*GetNameSafe(GetOwner()));
	}
}

void UBox3DBodyComponent::ResolveAutoBoxBounds()
{
	ResolvedHalfExtent = FVector(50.0, 50.0, 50.0);
	ResolvedBoxCenter = FVector::ZeroVector;

	USceneComponent* Root = GetOwner()->GetRootComponent();
	if (Root != nullptr)
	{
		// Local-space bounds (Identity transform) times the component scale gives
		// axis-aligned half-extents in the actor's frame, independent of rotation.
		const FVector Scale = Root->GetComponentScale();
		const FTransform RootTM = Root->GetComponentTransform();

		FBox Local(ForceInit);
		if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Root))
		{
			Local += RootPrim->CalcBounds(FTransform::Identity).GetBox();
		}

		// A compound (or a bare scene root) gets its extent from the attached
		// primitives - without this the box would collapse to a point.
		if (bConvexIncludesAttachedChildren || !Root->IsA<UPrimitiveComponent>())
		{
			TArray<USceneComponent*> Children;
			Root->GetChildrenComponents(/*bIncludeAllDescendants=*/true, Children);
			for (USceneComponent* Child : Children)
			{
				if (const UPrimitiveComponent* ChildPrim = Cast<UPrimitiveComponent>(Child))
				{
					Local += ChildPrim->CalcBounds(ChildPrim->GetComponentTransform().GetRelativeTransform(RootTM)).GetBox();
				}
			}
		}

		if (Local.IsValid && !Local.GetExtent().IsNearlyZero())
		{
			ResolvedHalfExtent = Local.GetExtent() * Scale;
			// Geometry is not always centred on its own component origin - fracture
			// pieces keep their verts in the source actor's space, so the mesh sits
			// metres away from the origin. Carry that offset or the box lands on the
			// pivot instead of on the mesh.
			ResolvedBoxCenter = Local.GetCenter() * Scale;
			return;
		}
	}

	UE_LOG(LogBox3D, Warning, TEXT("%s: could not derive Auto bounds; using 50cm default."),
		*GetNameSafe(GetOwner()));
}

void UBox3DBodyComponent::CaptureStepTransform()
{
	if (B3_IS_NULL(BodyId))
	{
		return;
	}

	// box3d has no wake event, so poll it here - we already visit every body each step.
	if (bGenerateSleepEvents)
	{
		const bool bAwake = b3Body_IsAwake(BodyId);
		if (bAwake != bWasAwake)
		{
			bWasAwake = bAwake;
			Subsystem->QueueSleepEvent(this, bAwake);
		}
	}

	FTransform NewXform = Box3D::FromBox3DTransform(b3Body_GetTransform(BodyId));
	NewXform.SetScale3D(SpawnScale); // box3d has no scale; keep the actor's.

	PrevTransform = CurrTransform;
	CurrTransform = NewXform;
}

void UBox3DBodyComponent::ApplyInterpolatedTransform(float Alpha)
{
	if (BodyType != EBox3DBodyType::Dynamic)
	{
		return;
	}

	const FVector Location = FMath::Lerp(PrevTransform.GetLocation(), CurrTransform.GetLocation(), Alpha);
	FQuat Rotation = FQuat::Slerp(PrevTransform.GetRotation(), CurrTransform.GetRotation(), Alpha);
	Rotation.Normalize();

	GetOwner()->SetActorLocationAndRotation(Location, Rotation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
}

void UBox3DBodyComponent::PushKinematicTarget(float TimeStep)
{
	if (B3_IS_NULL(BodyId))
	{
		return;
	}

	// Gameplay owns the actor pose; set the velocity that reaches it so contacts carry
	// resting dynamics (a raw SetTransform teleport imparts no momentum).
	const FTransform T = GetOwner()->GetActorTransform();
	b3WorldTransform Target;
	Target.p = Box3D::ToBox3DPosition(T.GetLocation());
	Target.q = Box3D::ToBox3DQuat(T.GetRotation());
	b3Body_SetTargetTransform(BodyId, Target, TimeStep, /*wake=*/true);
}

void UBox3DBodyComponent::DrawDebug() const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (World == nullptr || Owner == nullptr)
	{
		return;
	}

	// Draw at the owning actor's exact current transform. On the server box3d wrote
	// it this frame; on a client it is the replicated pose. Either way the wireframe
	// sits on the mesh - and a client box that moves proves replication is working.
	const FTransform T = Owner->GetActorTransform();
	const FVector Location = T.GetLocation();
	const FQuat Rotation = T.GetRotation();

	// Colour by type: dynamic = green, static = cyan, kinematic = yellow.
	FColor Color = FColor::Green;
	switch (BodyType)
	{
	case EBox3DBodyType::Static:    Color = FColor::Cyan;   break;
	case EBox3DBodyType::Kinematic: Color = FColor::Yellow; break;
	default: break;
	}

	switch (Shape)
	{
	case EBox3DShape::Sphere:
		DrawDebugSphere(World, Location, Radius, 16, Color, false, -1.0f, 0, 1.0f);
		break;
	case EBox3DShape::Capsule:
		DrawDebugCapsule(World, Location, HalfHeight + Radius, Radius, Rotation, Color, false, -1.0f, 0, 1.0f);
		break;
	case EBox3DShape::Convex:
		if (ConvexDebugSegments.Num() >= 2)
		{
			// Segments are body-local (scale baked); the body carries no scale, so place
			// them with rotation + translation only.
			for (int32 i = 0; i + 1 < ConvexDebugSegments.Num(); i += 2)
			{
				DrawDebugLine(World,
					Location + Rotation.RotateVector(ConvexDebugSegments[i]),
					Location + Rotation.RotateVector(ConvexDebugSegments[i + 1]),
					Color, false, -1.0f, 0, 1.0f);
			}
		}
		else // no convex collision resolved; the shape fell back to a box
		{
			DrawDebugBox(World, Location + Rotation.RotateVector(ResolvedBoxCenter),
				ResolvedHalfExtent, Rotation, Color, false, -1.0f, 0, 1.0f);
		}
		break;
	default: // Auto / Box
		DrawDebugBox(World, Location + Rotation.RotateVector(ResolvedBoxCenter),
			ResolvedHalfExtent, Rotation, Color, false, -1.0f, 0, 1.0f);
		break;
	}
}
