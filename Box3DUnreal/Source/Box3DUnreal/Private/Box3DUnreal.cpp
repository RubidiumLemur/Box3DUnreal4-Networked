#include "Box3DUnreal.h"
#include <box3d/box3d.h>

#define LOCTEXT_NAMESPACE "FBox3DUnrealModule"

void FBox3DUnrealModule::StartupModule()
{
}

void FBox3DUnrealModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBox3DUnrealModule, Box3DUnreal)