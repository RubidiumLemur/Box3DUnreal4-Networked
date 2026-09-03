/**
 * @file
 * @brief Module entry point for the Box3DUnreal runtime plugin.
 *
 * @details The module is loaded by Unreal when the plugin is active. It ensures the Box3D system
 * is initialized with the project's expected boot behavior and supports the runtime enable/disable
 * switch used to compare Box3D against the default UE physics path.
 */

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/**
 * @brief Unreal module wrapper for the Box3D runtime integration.
 *
 * @details Startup/Shutdown is intentionally lightweight because the actual simulation setup is
 * done by the world subsystem and the Box3D component lifecycle. The module bootstrap mainly
 * handles the command-line disable switch and ensures the plugin loads cleanly in the editor and game.
 */
class FBox3DUnrealModule : public IModuleInterface
{
public:

	/** @brief Called when the plugin is loaded into the Unreal process. */
	virtual void StartupModule() override;
	/** @brief Called when the plugin is unloaded from the process. */
	virtual void ShutdownModule() override;
};
