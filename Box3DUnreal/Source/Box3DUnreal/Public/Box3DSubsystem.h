// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include <box3d/box3d.h>
#include "Box3DSubsystem.generated.h"

class UBox3DBodyComponent;

/**
 * Owns the single box3d world for a UWorld and advances it on a fixed timestep.
 *
 * One instance exists per Game/PIE world, so editor, each PIE session, and
 * standalone each get an isolated simulation with correct create/destroy on the
 * world lifecycle. Dynamic body components register here to be stepped and to have
 * their owning actors driven with render-frame interpolation.
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

	/** True where box3d simulates: Standalone and servers, never a pure client. */
	bool IsSimulationAuthority() const { return bIsAuthority; }

	/** All bodies register for debug draw; dynamic ones also register for sync. */
	void RegisterBody(UBox3DBodyComponent* Component);
	void RegisterDynamicBody(UBox3DBodyComponent* Component);
	void RegisterKinematicBody(UBox3DBodyComponent* Component);
	void UnregisterBody(UBox3DBodyComponent* Component);

protected:
	void CreateBox3DWorld();
	void DestroyBox3DWorld();
	void StepFixed(float DeltaTime);
	void DebugDraw();

private:
	b3WorldId WorldId = b3_nullWorldId;
	bool bWorldValid = false;
	bool bIsAuthority = false;

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

	/** Dynamic bodies driven each frame. Weak so a destroyed actor drops out safely. */
	TArray<TWeakObjectPtr<UBox3DBodyComponent>> DynamicBodies;

	/** Kinematic bodies pushed from their actor transform before each step. */
	TArray<TWeakObjectPtr<UBox3DBodyComponent>> KinematicBodies;

	/** Every registered body (all types), used only for debug draw. */
	TArray<TWeakObjectPtr<UBox3DBodyComponent>> AllBodies;
};
