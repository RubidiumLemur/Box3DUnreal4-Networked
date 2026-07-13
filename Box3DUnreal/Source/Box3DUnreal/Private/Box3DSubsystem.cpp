// Author: Antonio Lattanzio - emptyvessel

#include "Box3DSubsystem.h"
#include "Box3DBodyComponent.h"
#include "Box3DConversion.h"
#include "Box3DLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

static TAutoConsoleVariable<int32> CVarBox3DDebugDraw(
	TEXT("box3d.DebugDraw"),
	0,
	TEXT("Draw box3d dynamic bodies at their actual simulation transform (1 = on)."),
	ECVF_Cheat);

bool UBox3DSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Simulation only runs in play worlds. Editor preview support can be added later.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UBox3DSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// box3d is server-authoritative: only Standalone and servers simulate. A pure
	// client leaves its actors to UE's replicated movement, so it never spins up a
	// second (diverging) world.
	bIsAuthority = InWorld.GetNetMode() != NM_Client;
	if (bIsAuthority)
	{
		CreateBox3DWorld();
	}
	else
	{
		UE_LOG(LogBox3D, Log,
			TEXT("box3d: client world - simulation disabled; actors follow replicated movement."));
	}
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
	KinematicBodies.Reset();
	AllBodies.Reset();

	if (bWorldValid)
	{
		b3DestroyWorld(WorldId);
	}

	WorldId = b3_nullWorldId;
	bWorldValid = false;
	Accumulator = 0.0;
}

void UBox3DSubsystem::RegisterBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		AllBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::RegisterDynamicBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		DynamicBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::RegisterKinematicBody(UBox3DBodyComponent* Component)
{
	if (Component != nullptr)
	{
		KinematicBodies.AddUnique(Component);
	}
}

void UBox3DSubsystem::UnregisterBody(UBox3DBodyComponent* Component)
{
	DynamicBodies.RemoveSingleSwap(Component);
	KinematicBodies.RemoveSingleSwap(Component);
	AllBodies.RemoveSingleSwap(Component);
}

bool UBox3DSubsystem::IsTickable() const
{
	// Authority worlds tick to step; client worlds tick only to debug-draw the
	// replicated bodies they registered.
	return bWorldValid || AllBodies.Num() > 0;
}

TStatId UBox3DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBox3DSubsystem, STATGROUP_Tickables);
}

void UBox3DSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only authority worlds step; but debug draw runs everywhere so clients can see
	// their replicated bodies.
	if (bWorldValid)
	{
		StepFixed(DeltaTime);
	}

	if (CVarBox3DDebugDraw.GetValueOnGameThread() != 0)
	{
		DebugDraw();
	}
}

void UBox3DSubsystem::DebugDraw()
{
	// One-shot confirmation the *new* debug path is running (settles stale-build
	// questions: if you don't see this line after enabling the cvar, the binary
	// wasn't rebuilt).
	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		bLoggedOnce = true;
		UE_LOG(LogBox3D, Log, TEXT("box3d.DebugDraw active: drawing bodies at actor pose."));
	}

	if (GEngine != nullptr)
	{
		// AUTH = this world simulates; CLIENT = it only displays replicated poses.
		const TCHAR* RoleTag = bIsAuthority ? TEXT("AUTH") : TEXT("CLIENT");
		GEngine->AddOnScreenDebugMessage(
			static_cast<uint64>(reinterpret_cast<UPTRINT>(this)), 0.0f, FColor::Green,
			FString::Printf(TEXT("[box3d|%s] %d bodies (%d dynamic) @ %.0f Hz x%d"),
				RoleTag, AllBodies.Num(), DynamicBodies.Num(), 1.0f / FixedTimeStep, SubStepCount));
	}

	for (int32 Index = AllBodies.Num() - 1; Index >= 0; --Index)
	{
		if (const UBox3DBodyComponent* Body = AllBodies[Index].Get())
		{
			Body->DrawDebug();
		}
		else
		{
			AllBodies.RemoveAtSwap(Index);
		}
	}
}

void UBox3DSubsystem::StepFixed(float DeltaTime)
{
	// Consume real time in whole fixed steps. After each step, capture every
	// dynamic body's transform so we always have the last two states to blend.
	Accumulator = FMath::Min(Accumulator + DeltaTime, static_cast<double>(MaxFrameTime));

	while (Accumulator >= FixedTimeStep)
	{
		// Drive kinematic bodies from their (gameplay-moved) actor transform so they
		// carry resting dynamics.
		for (int32 Index = KinematicBodies.Num() - 1; Index >= 0; --Index)
		{
			if (UBox3DBodyComponent* Body = KinematicBodies[Index].Get())
			{
				Body->PushKinematicTarget(FixedTimeStep);
			}
			else
			{
				KinematicBodies.RemoveAtSwap(Index);
			}
		}

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
