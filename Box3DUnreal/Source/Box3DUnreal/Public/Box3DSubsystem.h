// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include <box3d/box3d.h>
#include "Box3DSubsystem.generated.h"

/**
 * Owns the single box3d world for a UWorld and advances it on a fixed timestep.
 *
 * One instance exists per Game/PIE world, so editor, each PIE session, and
 * standalone each get an isolated simulation with correct create/destroy on the
 * world lifecycle. Body components (later milestone) register here to be stepped
 * and synced.
 */
UCLASS()
class BOX3DUNREAL_API UBox3DSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem / UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** The box3d world id. Only valid while IsWorldValid(). */
	b3WorldId GetWorldId() const { return WorldId; }
	bool IsWorldValid() const { return bWorldValid; }

	/**
	 * Milestone-1 acceptance check: drop a dynamic 1m box from 5m up and log its
	 * Z each step. Exercises world + fixed step + gravity direction + conversion
	 * end to end. Invoked by the "box3d.SelfTest" console command.
	 */
	void RunGravitySelfTest();

protected:
	void CreateBox3DWorld();
	void DestroyBox3DWorld();
	void StepFixed(float DeltaTime);
	void TickSelfTest();

private:
	b3WorldId WorldId = b3_nullWorldId;
	bool bWorldValid = false;

	/** Real time carried between frames, consumed in fixed increments. */
	double Accumulator = 0.0;

	UPROPERTY(EditAnywhere, Category = "Box3D")
	float FixedTimeStep = 1.0f / 60.0f;

	UPROPERTY(EditAnywhere, Category = "Box3D")
	int32 SubStepCount = 4;

	/** Spiral-of-death guard: never simulate more than this much time per frame. */
	UPROPERTY(EditAnywhere, Category = "Box3D")
	float MaxFrameTime = 0.25f;

	/** Gravity in Unreal space (cm/s^2). Converted to box3d meters on world create. */
	UPROPERTY(EditAnywhere, Category = "Box3D")
	FVector Gravity = FVector(0.0, 0.0, -980.0);

	// --- self-test state ---
	b3BodyId TestBodyId = b3_nullBodyId;
	int32 TestStepsRemaining = 0;
};
