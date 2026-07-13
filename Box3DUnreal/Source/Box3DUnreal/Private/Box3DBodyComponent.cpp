// Author: Antonio Lattanzio - emptyvessel

#include "Box3DBodyComponent.h"
#include "Box3DSubsystem.h"
#include "Box3DConversion.h"
#include "Box3DLog.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

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
	// no body but still show the replicated actor pose.
	if (Shape == EBox3DShape::Box)
	{
		ResolvedHalfExtent = BoxHalfExtent.GetAbs();
	}
	else if (Shape == EBox3DShape::Auto)
	{
		ResolvedHalfExtent = ComputeAutoBoxHalfExtent();
	}

	// Register for debug draw in every world (server and client).
	Subsystem->RegisterBody(this);

	// Only the network authority simulates. A replicated actor on a client is a
	// SimulatedProxy (HasAuthority()==false) and must NOT create a local body, or it
	// would fight the server's replicated movement (client sim moves the actor while
	// replication yanks it back to the authoritative pose). HasAuthority() is the
	// reliable per-actor gate; the world NetMode can still read Standalone during a
	// PIE client's OnWorldBeginPlay. In Standalone every actor is authority.
	AActor* Owner = GetOwner();
	if (!Subsystem->IsWorldValid() || Owner == nullptr || !Owner->HasAuthority())
	{
		// Client / non-authority: no local body. The actor follows replicated
		// movement; debug draw still shows it via the registration above.
		return;
	}

	CreateBody();
	if (B3_IS_NULL(BodyId))
	{
		return;
	}

	AddShape();

	if (BodyType == EBox3DBodyType::Dynamic)
	{
		EnforceAuthorityContract();
		Subsystem->RegisterDynamicBody(this); // dynamic only, for step sync
		EnableReplication();                  // stream the server transform to clients
	}
}

void UBox3DBodyComponent::EnableReplication()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || GetWorld()->GetNetMode() == NM_Standalone)
	{
		return; // single-player: nothing to replicate
	}

	// Actor replication is an Actor-level flag - there is no component checkbox for
	// it. We are on the authority here (EnableReplication is only reached after the
	// HasAuthority gate in BeginPlay), so enable it ourselves if the actor author
	// hasn't, then let UE stream the box3d-driven transform to clients. Clients move
	// the actor via ReplicatedMovement without simulating locally.
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

	BodyId = b3CreateBody(Subsystem->GetWorldId(), &Def);
}

void UBox3DBodyComponent::AddShape()
{
	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	ShapeDef.density = Density;
	ShapeDef.baseMaterial.friction = Friction;
	ShapeDef.baseMaterial.restitution = Restitution;

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
	default: // Auto / Box
	{
		// ResolvedHalfExtent was computed in BeginPlay (also used by debug draw).
		const b3BoxHull Hull = b3MakeBoxHull(
			ResolvedHalfExtent.X * M, ResolvedHalfExtent.Y * M, ResolvedHalfExtent.Z * M);
		b3CreateHullShape(BodyId, &ShapeDef, &Hull.base);
		break;
	}
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
}

void UBox3DBodyComponent::EnforceAuthorityContract()
{
	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (Root == nullptr)
	{
		return;
	}

	// box3d is the sole mover; make sure Chaos isn't also simulating this actor.
	Root->SetSimulatePhysics(false);

	if (Root->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogBox3D, Warning,
			TEXT("%s: root is not Movable; box3d cannot drive its transform. Set Mobility to Movable."),
			*GetNameSafe(GetOwner()));
	}
}

FVector UBox3DBodyComponent::ComputeAutoBoxHalfExtent() const
{
	if (const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
	{
		// Local-space bounds (Identity transform) times the component scale gives
		// axis-aligned half-extents in the actor's frame, independent of rotation.
		const FBoxSphereBounds LocalBounds = Root->CalcBounds(FTransform::Identity);
		const FVector Extent = LocalBounds.BoxExtent * Root->GetComponentScale();
		if (!Extent.IsNearlyZero())
		{
			return Extent;
		}
	}

	UE_LOG(LogBox3D, Warning, TEXT("%s: could not derive Auto bounds; using 50cm default."),
		*GetNameSafe(GetOwner()));
	return FVector(50.0, 50.0, 50.0);
}

void UBox3DBodyComponent::CaptureStepTransform()
{
	if (B3_IS_NULL(BodyId))
	{
		return;
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
	default: // Auto / Box
		DrawDebugBox(World, Location, ResolvedHalfExtent, Rotation, Color, false, -1.0f, 0, 1.0f);
		break;
	}
}
