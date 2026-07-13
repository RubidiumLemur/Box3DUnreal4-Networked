// Author: Antonio Lattanzio - emptyvessel

#include "Box3DSubsystem.h"
#include "Box3DConversion.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBox3D, Log, All);

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
	if (B3_IS_NON_NULL(TestBodyId))
	{
		b3DestroyBody(TestBodyId);
		TestBodyId = b3_nullBodyId;
	}

	if (bWorldValid)
	{
		b3DestroyWorld(WorldId);
	}

	WorldId = b3_nullWorldId;
	bWorldValid = false;
	Accumulator = 0.0;
	TestStepsRemaining = 0;
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
	// Consume real time in whole fixed steps. Render-frame interpolation of body
	// transforms lands with the body component in the next milestone.
	Accumulator = FMath::Min(Accumulator + DeltaTime, static_cast<double>(MaxFrameTime));

	while (Accumulator >= FixedTimeStep)
	{
		b3World_Step(WorldId, FixedTimeStep, SubStepCount);
		Accumulator -= FixedTimeStep;

		TickSelfTest();
	}
}

void UBox3DSubsystem::RunGravitySelfTest()
{
	if (!bWorldValid)
	{
		UE_LOG(LogBox3D, Warning, TEXT("box3d.SelfTest: no valid world (are you in play mode?)."));
		return;
	}

	if (B3_IS_NON_NULL(TestBodyId))
	{
		b3DestroyBody(TestBodyId);
		TestBodyId = b3_nullBodyId;
	}

	b3BodyDef BodyDef = b3DefaultBodyDef();
	BodyDef.type = b3_dynamicBody;
	BodyDef.position = Box3D::ToBox3DPosition(FVector(0.0, 0.0, 500.0)); // 5 m up

	TestBodyId = b3CreateBody(WorldId, &BodyDef);

	const b3BoxHull Hull = b3MakeBoxHull(0.5f, 0.5f, 0.5f); // 1 m cube (half-extents)
	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	b3CreateHullShape(TestBodyId, &ShapeDef, &Hull.base);

	TestStepsRemaining = 180; // ~3 s at 60 Hz

	UE_LOG(LogBox3D, Log, TEXT("box3d.SelfTest: dropped a 1m box from Z=500cm; expect Z to fall."));
}

void UBox3DSubsystem::TickSelfTest()
{
	if (TestStepsRemaining <= 0 || B3_IS_NULL(TestBodyId))
	{
		return;
	}

	const b3WorldTransform T = b3Body_GetTransform(TestBodyId);
	const FVector P = Box3D::FromBox3DPosition(T.p);

	// Log a few times a second rather than every step.
	if (TestStepsRemaining % 15 == 0)
	{
		UE_LOG(LogBox3D, Log, TEXT("box3d.SelfTest: body Z = %.1f cm"), P.Z);
	}

	if (--TestStepsRemaining <= 0)
	{
		b3DestroyBody(TestBodyId);
		TestBodyId = b3_nullBodyId;
		UE_LOG(LogBox3D, Log, TEXT("box3d.SelfTest: complete (final Z = %.1f cm)."), P.Z);
	}
}

static FAutoConsoleCommandWithWorld GBox3DSelfTestCommand(
	TEXT("box3d.SelfTest"),
	TEXT("Drop a box3d dynamic body and log its Z to verify world/step/gravity/conversion."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (World != nullptr)
		{
			if (UBox3DSubsystem* Subsystem = World->GetSubsystem<UBox3DSubsystem>())
			{
				Subsystem->RunGravitySelfTest();
			}
		}
	}));
