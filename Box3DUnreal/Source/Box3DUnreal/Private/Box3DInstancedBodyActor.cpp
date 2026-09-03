// Author: Antonio Lattanzio - emptyvessel

#include "Box3DInstancedBodyActor.h"
#include "Box3DConversion.h"
#include "Box3DProjectSettings.h"
#include "Box3DSubsystem.h"
#include "Net/UnrealNetwork.h"

ABox3DInstancedBodyActor::ABox3DInstancedBodyActor()
{
	bReplicates = true;
	NetUpdateFrequency = 100.0f;
	MinNetUpdateFrequency = 30.0f;
	NetPriority = 2.0f;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	InstanceComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Box3DInstances"));
	SetRootComponent(InstanceComponent);
	InstanceComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InstanceComponent->SetMobility(EComponentMobility::Movable);
}

void ABox3DInstancedBodyActor::BeginPlay()
{
	Super::BeginPlay();
	InstanceComponent->SetStaticMesh(InstanceMesh);
	bUseNetworkPath = bMultiplayerCompatible && GetDefault<UBox3DProjectSettings>()->bMultiplayerCompatible;
	if (!bUseNetworkPath)
	{
		SetReplicates(false);
		NetUpdateFrequency = 0.0f;
	}
	Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UBox3DSubsystem>() : nullptr;
	if (HasAuthority() && Subsystem != nullptr && Subsystem->IsWorldValid())
	{
		Subsystem->RegisterInstancedBodyActor(this);
	}
}

void ABox3DInstancedBodyActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Subsystem != nullptr)
	{
		Subsystem->UnregisterInstancedBodyActor(this);
	}
	DestroyBodies();
	Super::EndPlay(EndPlayReason);
}

void ABox3DInstancedBodyActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() || !bUseNetworkPath || !bInterpolateReplicatedInstances ||
		!bClientInstancesReady || ClientCurrentTransforms.Num() != ClientTargetSnapshot.Num() ||
		InstanceComponent->GetInstanceCount() != ClientCurrentTransforms.Num())
	{
		return;
	}

	bool bChanged = false;
	for (int32 Index = 0; Index < ClientCurrentTransforms.Num(); ++Index)
	{
		FTransform& Current = ClientCurrentTransforms[Index];
		const FTransform& Target = ClientTargetSnapshot[Index].WorldTransform;
		const FVector Location = FMath::VInterpTo(Current.GetLocation(), Target.GetLocation(),
			DeltaSeconds, ReplicatedInstanceInterpolationSpeed);
		const FQuat Rotation = FMath::QInterpTo(Current.GetRotation(), Target.GetRotation(),
			DeltaSeconds, ReplicatedInstanceInterpolationSpeed);
		Current.SetLocation(Location);
		Current.SetRotation(Rotation);
		Current.SetScale3D(Target.GetScale3D());
		bChanged |= !Current.Equals(Target);
		if (Index < InstanceComponent->GetInstanceCount())
		{
			InstanceComponent->UpdateInstanceTransform(Index, Current, true, false, false);
		}
	}
	if (bChanged)
	{
		InstanceComponent->MarkRenderStateDirty();
	}
}

void ABox3DInstancedBodyActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABox3DInstancedBodyActor, ReplicatedBodyChunk);
	DOREPLIFETIME(ABox3DInstancedBodyActor, ReplicatedChunkIndex);
	DOREPLIFETIME(ABox3DInstancedBodyActor, ReplicatedChunkCount);
	DOREPLIFETIME(ABox3DInstancedBodyActor, ReplicatedSnapshotId);
}

int32 ABox3DInstancedBodyActor::SpawnBody(const FTransform& WorldTransform)
{
	if (!HasAuthority() || Subsystem == nullptr || !Subsystem->IsWorldValid())
	{
		return INDEX_NONE;
	}

	b3BodyDef Def = b3DefaultBodyDef();
	Def.type = b3_dynamicBody;
	Def.position = Box3D::ToBox3DPosition(WorldTransform.GetLocation());
	Def.rotation = Box3D::ToBox3DQuat(WorldTransform.GetRotation());
	Def.linearDamping = LinearDamping;
	Def.angularDamping = AngularDamping;
	Def.enableSleep = bEnableSleep;
	Def.sleepThreshold = SleepThreshold * static_cast<float>(Box3D::UnrealToMeters);
	Def.userData = this;

	const b3BodyId BodyId = b3CreateBody(Subsystem->GetWorldId(), &Def);
	if (B3_IS_NULL(BodyId))
	{
		return INDEX_NONE;
	}

	AddBoxShape(BodyId);
	FInstanceBody& Instance = Bodies.AddDefaulted_GetRef();
	Instance.Id = NextBodyId++;
	Instance.BodyId = BodyId;
	Instance.Scale = WorldTransform.GetScale3D();
	Instance.InstanceIndex = InstanceComponent->AddInstanceWorldSpace(WorldTransform);

	FBox3DInstancedBodyState& State = PendingSnapshot.AddDefaulted_GetRef();
	State.Id = Instance.Id;
	State.WorldTransform = WorldTransform;
	NextChunkToReplicate = 0;
	return Instance.Id;
}

bool ABox3DInstancedBodyActor::AddImpulseToBody(int32 BodyId, const FVector& Impulse)
{
	if (!HasAuthority() || Subsystem == nullptr || !Subsystem->IsWorldValid() || Impulse.ContainsNaN())
	{
		return false;
	}

	for (const FInstanceBody& Instance : Bodies)
	{
		if (Instance.Id == BodyId && B3_IS_NON_NULL(Instance.BodyId))
		{
			b3Body_ApplyLinearImpulseToCenter(Instance.BodyId, Box3D::ToBox3DVector(Impulse), true);
			return true;
		}
	}

	return false;
}

void ABox3DInstancedBodyActor::UpdateSimulationInstances()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bUseNetworkPath)
	{
		PendingSnapshot.Reset(Bodies.Num());
	}
	for (const FInstanceBody& Instance : Bodies)
	{
		if (B3_IS_NULL(Instance.BodyId))
		{
			continue;
		}

		FTransform Transform = Box3D::FromBox3DTransform(b3Body_GetTransform(Instance.BodyId));
		Transform.SetScale3D(Instance.Scale);
		InstanceComponent->UpdateInstanceTransform(Instance.InstanceIndex, Transform, true, false, true);

		if (bUseNetworkPath)
		{
			FBox3DInstancedBodyState& State = PendingSnapshot.AddDefaulted_GetRef();
			State.Id = Instance.Id;
			State.WorldTransform = Transform;
		}
	}
	InstanceComponent->MarkRenderStateDirty();

	if (!bUseNetworkPath)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 ChunkSize = FMath::Max(1, ReplicationChunkSize);
	const double ChunkInterval = FMath::Max(0.001f, ReplicationChunkIntervalSeconds);
	if (PendingSnapshot.Num() > 0 &&
		(LastChunkReplicationTime < 0.0 || Now - LastChunkReplicationTime >= ChunkInterval))
	{
		const int32 ChunkCount = FMath::DivideAndRoundUp(PendingSnapshot.Num(), ChunkSize);
		NextChunkToReplicate = FMath::Clamp(NextChunkToReplicate, 0, ChunkCount - 1);
		const int32 FirstBody = NextChunkToReplicate * ChunkSize;
		const int32 BodyCount = FMath::Min(ChunkSize, PendingSnapshot.Num() - FirstBody);

		ReplicatedBodyChunk.Reset(BodyCount);
		for (int32 Index = 0; Index < BodyCount; ++Index)
		{
			ReplicatedBodyChunk.Add(PendingSnapshot[FirstBody + Index]);
		}
		ReplicatedChunkIndex = NextChunkToReplicate;
		ReplicatedChunkCount = ChunkCount;
		ReplicatedSnapshotId = SnapshotId;
		LastChunkReplicationTime = Now;

		++NextChunkToReplicate;
		if (NextChunkToReplicate >= ChunkCount)
		{
			NextChunkToReplicate = 0;
			++SnapshotId;
		}
	}
}

void ABox3DInstancedBodyActor::OnRep_ReplicatedBodyChunk()
{
	if (HasAuthority() || ReplicatedChunkCount <= 0 || ReplicatedChunkIndex < 0 ||
		ReplicatedChunkIndex >= ReplicatedChunkCount)
	{
		return;
	}

	if (ReceivedSnapshotId != ReplicatedSnapshotId || ReceivedSnapshotChunkCount != ReplicatedChunkCount)
	{
		ReceivedSnapshotId = ReplicatedSnapshotId;
		ReceivedSnapshotChunkCount = ReplicatedChunkCount;
		ReceivedSnapshotChunks.Init(0, ReplicatedChunkCount);
		PendingSnapshot.SetNum(ReplicatedChunkCount * FMath::Max(1, ReplicationChunkSize));
	}

	const int32 FirstBody = ReplicatedChunkIndex * FMath::Max(1, ReplicationChunkSize);
	for (int32 Index = 0; Index < ReplicatedBodyChunk.Num(); ++Index)
	{
		PendingSnapshot[FirstBody + Index] = ReplicatedBodyChunk[Index];
	}
	ReceivedSnapshotChunks[ReplicatedChunkIndex] = 1;

	for (const uint8 Received : ReceivedSnapshotChunks)
	{
		if (Received == 0)
		{
			return;
		}
	}

	bClientInstancesReady = false;
	InstanceComponent->ClearInstances();
	ClientTargetSnapshot.Reset();
	for (const FBox3DInstancedBodyState& State : PendingSnapshot)
	{
		if (State.Id != INDEX_NONE)
		{
			ClientTargetSnapshot.Add(State);
		}
	}
	if (!bInterpolateReplicatedInstances || ClientCurrentTransforms.Num() != ClientTargetSnapshot.Num())
	{
		ClientCurrentTransforms.Reset();
		for (const FBox3DInstancedBodyState& State : ClientTargetSnapshot)
		{
			ClientCurrentTransforms.Add(State.WorldTransform);
		}
	}
	for (const FTransform& Transform : ClientCurrentTransforms)
	{
		InstanceComponent->AddInstanceWorldSpace(Transform);
	}
	InstanceComponent->MarkRenderStateDirty();
	bClientInstancesReady = InstanceComponent->GetInstanceCount() == ClientCurrentTransforms.Num();
}

void ABox3DInstancedBodyActor::ApplyReplicatedInstances()
{
	if (HasAuthority())
	{
		return;
	}

	OnRep_ReplicatedBodyChunk();
}

void ABox3DInstancedBodyActor::DestroyBodies()
{
	if (Subsystem != nullptr && Subsystem->IsWorldValid())
	{
		for (const FInstanceBody& Instance : Bodies)
		{
			if (B3_IS_NON_NULL(Instance.BodyId))
			{
				b3DestroyBody(Instance.BodyId);
			}
		}
	}
	Bodies.Reset();
	ReplicatedBodyChunk.Reset();
	PendingSnapshot.Reset();
	ClientTargetSnapshot.Reset();
	ClientCurrentTransforms.Reset();
	bClientInstancesReady = false;
}

void ABox3DInstancedBodyActor::AddBoxShape(b3BodyId BodyId) const
{
	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	ShapeDef.density = Density;
	const float M = static_cast<float>(Box3D::UnrealToMeters);
	const b3BoxHull Hull = b3MakeBoxHull(BodyHalfExtent.X * M, BodyHalfExtent.Y * M, BodyHalfExtent.Z * M);
	b3CreateHullShape(BodyId, &ShapeDef, &Hull.base);
}
