// Author: Antonio Lattanzio - emptyvessel

#include "Box3DSubsystem.h"
#include "Box3DBodyComponent.h"
#include "Box3DConversion.h"
#include "Box3DLog.h"
#include "Engine/World.h"

bool UBox3DSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Simulation only runs in play worlds. Editor preview support can be added later.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UBox3DSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	CreateBox3DWorld();
}

void UBox3DSubsystem::Deinitialize()
{
	DestroyBox3DWorld();
	Super::Deinitialize();
}

void UBox3DSubsystem::CreateBox3DWorld()
{
	if (bWorldValid)
	{
		return;
	}

	b3WorldDef Def = b3DefaultWorldDef();
	Def.gravity = Box3D::ToBox3DVector(Gravity);

	WorldId = b3CreateWorld(&Def);
	bWorldValid = b3World_IsValid(WorldId);

	if (bWorldValid)
	{
		const b3Version V = b3GetVersion();
		UE_LOG(LogBox3D, Log, TEXT("box3d %d.%d.%d world created (gravity %s cm/s^2, %.0f Hz x%d substeps)."),
			V.major, V.minor, V.revision, *Gravity.ToCompactString(), 1.0f / FixedTimeStep, SubStepCount);
	}
	else
	{
		UE_LOG(LogBox3D, Error, TEXT("box3d world creation failed."));
	}
}

void UBox3DSubsystem::DestroyBox3DWorld()
{
	// Bodies are owned/destroyed by their components; just drop our references.
	DynamicBodies.Reset();

	if (bWorldValid)
	{
		b3DestroyWorld(WorldId);
	}

	WorldId = b3_nullWorldId;
	bWorldValid = false;
	Accumulator = 0.0;
}

void UBox3DSubsystem::RegisterDynamicBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		DynamicBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::UnregisterBody(UBox3DBodyComponent* Component)
{
	DynamicBodies.RemoveSingleSwap(Component);
}

bool UBox3DSubsystem::IsTickable() const
{
	return bWorldValid;
}

TStatId UBox3DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBox3DSubsystem, STATGROUP_Tickables);
}

void UBox3DSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bWorldValid)
	{
		return;
	}

	StepFixed(DeltaTime);
}

void UBox3DSubsystem::StepFixed(float DeltaTime)
{
	// Consume real time in whole fixed steps. After each step, capture every
	// dynamic body's transform so we always have the last two states to blend.
	Accumulator = FMath::Min(Accumulator + DeltaTime, static_cast<double>(MaxFrameTime));

	while (Accumulator >= FixedTimeStep)
	{
		b3World_Step(WorldId, FixedTimeStep, SubStepCount);
		Accumulator -= FixedTimeStep;

		for (int32 Index = DynamicBodies.Num() - 1; Index >= 0; --Index)
		{
			if (UBox3DBodyComponent* Body = DynamicBodies[Index].Get())
			{
				Body->CaptureStepTransform();
			}
			else
			{
				DynamicBodies.RemoveAtSwap(Index);
			}
		}
	}

	// Interpolate the render pose between the last two steps (leftover fraction).
	const float Alpha = static_cast<float>(Accumulator / FixedTimeStep);
	for (const TWeakObjectPtr<UBox3DBodyComponent>& WeakBody : DynamicBodies)
	{
		if (UBox3DBodyComponent* Body = WeakBody.Get())
		{
			Body->ApplyInterpolatedTransform(Alpha);
		}
	}
}
