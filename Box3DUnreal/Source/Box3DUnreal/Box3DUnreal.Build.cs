/**
 * @file
 * @brief Builds and links the Box3D native library into the Unreal module.
 *
 * @details The runtime plugin depends on a bundled box3d library under ThirdParty. This build file
 * resolves the native library, configures the correct include directories, and ensures the library is
 * built once for the active platform before linking into the module.
 */
using System;
using System.Diagnostics;
using System.IO;
using UnrealBuildTool;

/**
 * @brief Unreal module rules for the Box3DUnreal integration.
 *
 * @details This module is responsible for locating the box3d native library, building it when
 * necessary, and exposing the public include paths and linker settings needed by the rest of the
 * plugin. The build is intentionally explicit so the engine uses the same box3d ABI consistently
 * across editor and runtime builds.
 */
public class Box3DUnreal : ModuleRules
{
	public Box3DUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[] { "Core" });
		// PhysicsCore: FTriMeshCollisionData / FTriIndices for static tri-mesh extraction.
		PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Engine", "PhysicsCore", "DeveloperSettings" });

		// ThirdParty/ holds the wrapper CMakeLists.txt and the box3d submodule.
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty"));
		string Box3DPath = Path.Combine(ThirdPartyPath, "box3d");

		PublicIncludePaths.Add(Path.Combine(Box3DPath, "include"));

		// Must match the box3d library ABI (ThirdParty/CMakeLists.txt builds box3d
		// with BOX3D_DOUBLE_PRECISION). This makes b3Pos double and routes
		// b3CreateWorld to the double-precision entry point in the box3d headers.
		// A mismatch trips box3d's intentional link-error guard.
		PublicDefinitions.Add("BOX3D_DOUBLE_PRECISION=1");

		// box3d is always built with the release CRT (/MD). This matches Unreal's
		// default for every configuration (Unreal only uses the debug CRT when
		// bDebugBuildsActuallyUseDebugCRT is set), so a single Release build is safe
		// to link everywhere and keeps the artifact name stable (no "d" postfix).
		string Platform = GetPlatformFolder(Target);
		string BuildDir = Path.Combine(ThirdPartyPath, "Intermediate", Platform, "build");
		string InstallDir = Path.Combine(ThirdPartyPath, "Intermediate", Platform, "install");

		string LibFileName = (Target.Platform == UnrealTargetPlatform.Win64) ? "box3d.lib" : "libbox3d.a";
		string LibPath = Path.Combine(InstallDir, "lib", LibFileName);

		if (!File.Exists(LibPath))
		{
			BuildBox3D(ThirdPartyPath, BuildDir, InstallDir);
		}

		if (!File.Exists(LibPath))
		{
			throw new BuildException("Box3DUnreal: box3d build did not produce the expected library at " + LibPath);
		}

		PublicAdditionalLibraries.Add(LibPath);
	}

	private static string GetPlatformFolder(ReadOnlyTargetRules Target)
	{
		if (Target.Platform == UnrealTargetPlatform.Win64) return "Win64";
		if (Target.Platform == UnrealTargetPlatform.Mac)   return "Mac";
		if (Target.Platform == UnrealTargetPlatform.Linux) return "Linux";
		throw new BuildException("Box3DUnreal: unsupported platform " + Target.Platform);
	}

	private void BuildBox3D(string SourceDir, string BuildDir, string InstallDir)
	{
		string CMake = FindCMake();

		Console.WriteLine("Box3DUnreal: building box3d via CMake ({0})", CMake);

		// Configure. No -G so CMake picks the platform default generator (the
		// Visual Studio generator on Windows). CMAKE_BUILD_TYPE covers
		// single-config generators; --config below covers multi-config ones.
		string ConfigureArgs = string.Format(
			"-S \"{0}\" -B \"{1}\" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=\"{2}\"",
			SourceDir, BuildDir, InstallDir);

		// The Visual Studio generator intermittently fails to detect the C
		// compiler on a fresh cache ("No CMAKE_C_COMPILER could be found"). A
		// clean retry reliably fixes it, so allow one.
		Directory.CreateDirectory(BuildDir);
		if (!RunCMake(CMake, ConfigureArgs, SourceDir, bAllowFailure: true))
		{
			Console.WriteLine("Box3DUnreal: CMake configure failed; retrying with a clean build tree.");
			SafeDeleteDirectory(BuildDir);
			Directory.CreateDirectory(BuildDir);
			RunCMake(CMake, ConfigureArgs, SourceDir);
		}

		// Build the box3d library and run its install rules so the archive lands
		// at a generator-independent location (<InstallDir>/lib).
		string BuildArgs = string.Format("--build \"{0}\" --config Release --target install", BuildDir);
		RunCMake(CMake, BuildArgs, SourceDir);
	}

	private static void SafeDeleteDirectory(string Dir)
	{
		try
		{
			if (Directory.Exists(Dir))
			{
				Directory.Delete(Dir, true);
			}
		}
		catch (Exception Ex)
		{
			Console.WriteLine("Box3DUnreal: could not clean {0}: {1}", Dir, Ex.Message);
		}
	}

	private bool RunCMake(string CMake, string Arguments, string WorkingDir, bool bAllowFailure = false)
	{
		ProcessStartInfo Info = new ProcessStartInfo(CMake, Arguments)
		{
			WorkingDirectory = WorkingDir,
			UseShellExecute = false,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			CreateNoWindow = true,
		};

		using (Process Proc = new Process())
		{
			Proc.StartInfo = Info;
			Proc.OutputDataReceived += (s, e) => { if (e.Data != null) Console.WriteLine("[box3d] " + e.Data); };
			Proc.ErrorDataReceived  += (s, e) => { if (e.Data != null) Console.WriteLine("[box3d] " + e.Data); };
			Proc.Start();
			Proc.BeginOutputReadLine();
			Proc.BeginErrorReadLine();
			Proc.WaitForExit();

			if (Proc.ExitCode != 0)
			{
				if (bAllowFailure)
				{
					return false;
				}
				throw new BuildException("Box3DUnreal: CMake failed (exit {0}) for: cmake {1}", Proc.ExitCode, Arguments);
			}
		}

		return true;
	}

	// UnrealBuildTool does not expose CMake, so locate it ourselves: PATH first,
	// then the copy bundled with Visual Studio, then a standalone install.
	private string FindCMake()
	{
		string OnPath;
		if (TryResolveFromPath("cmake", out OnPath))
		{
			return OnPath;
		}

		if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
		{
			string[] ProgramFilesRoots =
			{
				Environment.GetEnvironmentVariable("ProgramFiles") ?? @"C:\Program Files",
				Environment.GetEnvironmentVariable("ProgramFiles(x86)") ?? @"C:\Program Files (x86)",
			};
			string[] VsYears = { "2026", "2022" };
			string[] VsEditions = { "Community", "Professional", "Enterprise", "BuildTools" };

			foreach (string Root in ProgramFilesRoots)
			{
				foreach (string Year in VsYears)
				{
					foreach (string Edition in VsEditions)
					{
						string Candidate = Path.Combine(Root, "Microsoft Visual Studio", Year, Edition,
							"Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "cmake.exe");
						if (File.Exists(Candidate))
						{
							return Candidate;
						}
					}
				}

				string Standalone = Path.Combine(Root, "CMake", "bin", "cmake.exe");
				if (File.Exists(Standalone))
				{
					return Standalone;
				}
			}
		}

		throw new BuildException(
			"Box3DUnreal: could not find CMake. Install CMake and add it to PATH, " +
			"or install the \"C++ CMake tools\" component in Visual Studio.");
	}

	private static bool TryResolveFromPath(string Executable, out string ResolvedPath)
	{
		ResolvedPath = null;
		bool bWindows = BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64;
		string[] Extensions = bWindows ? new[] { ".exe", ".cmd", ".bat", "" } : new[] { "" };

		string PathEnv = Environment.GetEnvironmentVariable("PATH");
		if (string.IsNullOrEmpty(PathEnv))
		{
			return false;
		}

		foreach (string Dir in PathEnv.Split(Path.PathSeparator))
		{
			if (string.IsNullOrWhiteSpace(Dir))
			{
				continue;
			}

			foreach (string Ext in Extensions)
			{
				string Candidate = Path.Combine(Dir.Trim(), Executable + Ext);
				if (File.Exists(Candidate))
				{
					ResolvedPath = Candidate;
					return true;
				}
			}
		}

		return false;
	}
}
