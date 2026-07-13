// Author: Antonio Lattanzio - emptyvessel

#include "Box3DUnreal.h"
#include "Box3DLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogBox3D);

#define LOCTEXT_NAMESPACE "FBox3DUnrealModule"

void FBox3DUnrealModule::StartupModule()
{
}

void FBox3DUnrealModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBox3DUnrealModule, Box3DUnreal)
