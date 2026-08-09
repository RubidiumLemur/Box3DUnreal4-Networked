// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include <box3d/box3d.h>

// Per-body state capture / restore / hash: the foundation for determinism verification
// (D0) and client-side rollback (D2). box3d exposes no live-world serialize, so a
// "snapshot" is the kinematic state of each body read back through the public getters.
//
// IMPORTANT (fidelity): this captures a body's transform + velocity + awake flag, NOT the
// solver's internal history - warm-start impulses, contact anchors, sleep timers. Restoring
// then re-simulating is therefore NOT guaranteed bit-identical to a run that never rolled
// back; warm starting is the main divergence source (measure with box3d.SnapshotTest, and
// see b3World_EnableWarmStarting). Everything crossing here stays in box3d space - no Unreal
// conversion - so a hash compares raw simulation state.
namespace Box3D
{
	/** One dynamic body's restorable state, in box3d space. */
	struct FBodyState
	{
		b3WorldTransform Transform{};
		b3Vec3 LinearVelocity{};
		b3Vec3 AngularVelocity{};
		bool bAwake = false;
	};

	FORCEINLINE FBodyState CaptureBodyState(b3BodyId Body)
	{
		FBodyState State;
		State.Transform = b3Body_GetTransform(Body);
		State.LinearVelocity = b3Body_GetLinearVelocity(Body);
		State.AngularVelocity = b3Body_GetAngularVelocity(Body);
		State.bAwake = b3Body_IsAwake(Body);
		return State;
	}

	FORCEINLINE void RestoreBodyState(b3BodyId Body, const FBodyState& State)
	{
		b3Body_SetTransform(Body, State.Transform.p, State.Transform.q);
		b3Body_SetLinearVelocity(Body, State.LinearVelocity);
		b3Body_SetAngularVelocity(Body, State.AngularVelocity);
		// Order matters: setting velocity wakes a body, so apply the sleep flag last to keep a
		// body that was asleep at capture time asleep (its sleep timer still resets - see header note).
		b3Body_SetAwake(Body, State.bAwake);
	}

	/** Fold one body's state into a running djb2 hash (b3Hash). Feed states in a stable order so
	 *  the same world produces the same digest across runs / peers. Start from B3_HASH_INIT. */
	FORCEINLINE uint32 HashBodyState(uint32 Hash, const FBodyState& State)
	{
		Hash = b3Hash(Hash, reinterpret_cast<const uint8_t*>(&State.Transform), sizeof(State.Transform));
		Hash = b3Hash(Hash, reinterpret_cast<const uint8_t*>(&State.LinearVelocity), sizeof(State.LinearVelocity));
		Hash = b3Hash(Hash, reinterpret_cast<const uint8_t*>(&State.AngularVelocity), sizeof(State.AngularVelocity));
		return Hash;
	}

	FORCEINLINE uint32 HashBody(uint32 Hash, b3BodyId Body)
	{
		const FBodyState State = CaptureBodyState(Body);
		return HashBodyState(Hash, State);
	}
}
