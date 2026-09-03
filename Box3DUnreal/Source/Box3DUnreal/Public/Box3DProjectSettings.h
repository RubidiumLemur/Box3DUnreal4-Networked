// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Box3DProjectSettings.generated.h"

/**
 * @brief Project-wide Box3D networking policy.
 *
 * @details Disabling multiplayer compatibility selects the local high-performance path for
 * instanced bodies and avoids replication bookkeeping when the project is single-player.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Box3D"))
class BOX3DUNREAL_API UBox3DProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Enables replicated state and multiplayer-safe networking bookkeeping for Box3D instances. */
	UPROPERTY(config, EditAnywhere, Category = "Networking")
	bool bMultiplayerCompatible = true;
};
