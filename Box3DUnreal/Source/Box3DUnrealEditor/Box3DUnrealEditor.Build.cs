// Author: Antonio Lattanzio - emptyvessel

using UnrealBuildTool;

public class Box3DUnrealEditor : ModuleRules
{
	public Box3DUnrealEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[] { "Core" });

		// Box3DUnreal: the runtime module's extraction (ExtractStaticCollision) and the
		//   baked-collision data model / asset. The commandlet calls only the exported
		//   functions, so it never touches box3d symbols directly (no box3d link here).
		// UnrealEd: commandlet base + map/package loading & saving.
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetRegistry",
			"Box3DUnreal",
		});
	}
}
