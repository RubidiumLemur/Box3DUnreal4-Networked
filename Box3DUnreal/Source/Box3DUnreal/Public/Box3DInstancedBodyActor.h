// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include <box3d/box3d.h>
#include "Box3DInstancedBodyActor.generated.h"

class UBox3DSubsystem;

/**
 * @brief Replicated render state for one instanced Box3D body.
 */
USTRUCT(BlueprintType)
struct BOX3DUNREAL_API FBox3DInstancedBodyState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = INDEX_NONE;

	UPROPERTY()
	FTransform WorldTransform = FTransform::Identity;
};

/**
 * @brief Compact server-authoritative collection of Box3D dynamic bodies.
 *
 * @details This actor avoids one Unreal actor/component pair per debris item. The authoritative
 * server owns the Box3D bodies, while a HISM renders the collection and replicated state updates
 * the instance transforms on clients. SpawnBody returns a stable collection-local identifier.
 */
UCLASS()
class BOX3DUNREAL_API ABox3DInstancedBodyActor : public AActor
{
	GENERATED_BODY()

public:
	ABox3DInstancedBodyActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Mesh used to render every spawned body as a HISM instance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances")
	UStaticMesh* InstanceMesh = nullptr;

	/** Half-extents of each simulated box, in Unreal centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances", meta = (ClampMin = "0.0"))
	FVector BodyHalfExtent = FVector(25.0f, 25.0f, 25.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances", meta = (ClampMin = "0.0"))
	float Density = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances")
	bool bEnableSleep = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Instances", meta = (ClampMin = "0.0"))
	float SleepThreshold = 5.0f;

	/** Maximum number of body states sent in one replication chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network",
		meta = (ClampMin = "1", ClampMax = "512"))
	int32 ReplicationChunkSize = 128;

	/** Minimum time between replication chunks, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network",
		meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float ReplicationChunkIntervalSeconds = 0.01f;

	/** Smooths client HISM instances toward replicated transforms instead of snapping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network")
	bool bInterpolateReplicatedInstances = true;

	/** Whether this actor uses replicated multiplayer state instead of the local fast path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network")
	bool bMultiplayerCompatible = true;

	/** Interpolation speed used for client-side instance transform smoothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box3D|Network",
		meta = (ClampMin = "0.1", ClampMax = "1000.0"))
	float ReplicatedInstanceInterpolationSpeed = 20.0f;

	/** Spawns a simulated box and returns its stable ID, or INDEX_NONE when not authoritative. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Instances")
	int32 SpawnBody(const FTransform& WorldTransform);

	/** Applies a center-of-mass impulse to a server-owned body identified by SpawnBody. */
	UFUNCTION(BlueprintCallable, Category = "Box3D|Instances")
	bool AddImpulseToBody(int32 BodyId, const FVector& Impulse);

	/** Returns the HISM used to render the collection. */
	UHierarchicalInstancedStaticMeshComponent* GetInstanceComponent() const { return InstanceComponent; }

	/** Called by UBox3DSubsystem after each fixed Box3D step. */
	void UpdateSimulationInstances();

protected:
	UFUNCTION()
	void OnRep_ReplicatedBodyChunk();

private:
	struct FInstanceBody
	{
		int32 Id = INDEX_NONE;
		b3BodyId BodyId = b3_nullBodyId;
		int32 InstanceIndex = INDEX_NONE;
		FVector Scale = FVector::OneVector;
	};

	void ApplyReplicatedInstances();
	void DestroyBodies();
	void AddBoxShape(b3BodyId BodyId) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Box3D|Instances", meta = (AllowPrivateAccess = "true"))
	UHierarchicalInstancedStaticMeshComponent* InstanceComponent = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedBodyChunk)
	TArray<FBox3DInstancedBodyState> ReplicatedBodyChunk;

	UPROPERTY(Replicated)
	int32 ReplicatedChunkIndex = 0;

	UPROPERTY(Replicated)
	int32 ReplicatedChunkCount = 0;

	UPROPERTY(Replicated)
	int32 ReplicatedSnapshotId = 0;

	UPROPERTY(Transient)
	TWeakObjectPtr<UBox3DSubsystem> Subsystem;

	TArray<FInstanceBody> Bodies;
	int32 NextBodyId = 1;
	TArray<FBox3DInstancedBodyState> PendingSnapshot;
	TArray<uint8> ReceivedSnapshotChunks;
	int32 ReceivedSnapshotId = 0;
	int32 ReceivedSnapshotChunkCount = 0;
	double LastChunkReplicationTime = -1.0;
	int32 NextChunkToReplicate = 0;
	int32 SnapshotId = 1;
	TArray<FBox3DInstancedBodyState> ClientTargetSnapshot;
	TArray<FTransform> ClientCurrentTransforms;
	bool bClientInstancesReady = false;
	bool bUseNetworkPath = true;
};
