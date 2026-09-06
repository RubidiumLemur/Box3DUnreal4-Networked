// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Box3DBakeCommandlet.generated.h"

/**
 * Bakes a level's static box3d collision into a UBox3DCollisionData asset so the runtime
 * never cooks tri-meshes (GetPhysicsTriMeshData is editor/PIE-only). See doc milestone 5.
 *
 *   UnrealEditor-Cmd <Project>.uproject -run=Box3DBake -Map=/Game/Maps/Foo [-Out=/Game/Physics/BC_Foo] [-Tag=Box3DStatic]
 *
 * -Map  one or more (comma-separated) level package names to bake.
 * -Out  output asset package name (single map only; otherwise derived as <MapPath>/BC_<MapName>).
 * -Tag  also bake actors carrying this tag (in addition to Static UBox3DBodyComponent actors).
 *
 * Point the subsystem at the saved asset via BakedCollisionAssets in
 * [/Script/Box3DUnreal.Box3DSubsystem] to load it at runtime.
 */
UCLASS()
class UBox3DBakeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UBox3DBakeCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	// Bake one level into an asset. Returns true on success (asset written).
	bool BakeMap(const FString& MapPackageName, const FString& OutPackageName, FName StaticTag);
};
