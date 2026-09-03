// Author: Antonio Lattanzio - emptyvessel

#include "Box3DUnreal.h"
#include "Box3DLog.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogBox3D);

#define LOCTEXT_NAMESPACE "FBox3DUnrealModule"

/**
 * @brief Bootstraps the Box3D plugin for the current Unreal process.
 *
 * @details The module is intentionally minimal: it only applies the command-line disable flag and
 * ensures the global plugin log/category is available. The actual world simulation and body
 * integration are owned by the subsystem and component classes.
 */
void FBox3DUnrealModule::StartupModule()
{
	// -DisableBox3D on the launch command line seeds the master switch off. box3d.Enabled
	// is a static cvar constructed before this runs, so it's already registered here.
	if (FParse::Param(FCommandLine::Get(), TEXT("DisableBox3D")))
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("box3d.Enabled")))
		{
			CVar->Set(TEXT("0"), ECVF_SetByCommandline);
		}
		UE_LOG(LogBox3D, Log, TEXT("box3d: -DisableBox3D on command line; simulation off (box3d.Enabled=0)."));
	}
}

void FBox3DUnrealModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBox3DUnrealModule, Box3DUnreal)
