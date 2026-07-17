// Author: Antonio Lattanzio - emptyvessel

#include "Modules/ModuleManager.h"

// Editor-only module. It carries the bake commandlet (UBox3DBakeCommandlet); loading the
// module registers the commandlet class so `-run=Box3DBake` resolves. No custom startup.
IMPLEMENT_MODULE(FDefaultModuleImpl, Box3DUnrealEditor);
