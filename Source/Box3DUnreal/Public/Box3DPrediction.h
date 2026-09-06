// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include "Box3DSnapshot.h"
#include <box3d/box3d.h>

// Client-side prediction + rollback core (doc §11b, D2). Built on the D0/D1 foundation:
// determinism (a client re-simulating from an authoritative state reaches the same result the
// server did) plus the per-body snapshot primitives in Box3DSnapshot.h.
//
// The client simulates opted-in bodies locally so they respond with no round-trip latency. The
// server periodically sends an authoritative, frame-tagged snapshot. Because that snapshot is
// stale by the network delay, the client rolls the world back to the snapshot's frame, overwrites
// the corrected bodies, and deterministically replays forward to the present - landing where the
// server's forward sim will be, not where it was when it sent the packet.
namespace Box3D
{
	/**
	 * Fixed-capacity ring of whole-world snapshots, one entry per simulated frame. Rollback needs
	 * the *entire* dynamic body set at the rollback frame (interacting bodies must all be restored
	 * before the replay re-steps), so this stores every body's FBodyState per frame.
	 *
	 * Assumes a stable body set and order across frames - true for persistent bodies; spawn/despawn
	 * mid-window is out of scope for this first cut (see doc §11b limitations).
	 */
	class BOX3DUNREAL_API FSnapshotRing
	{
	public:
		/** Size the ring for Capacity frames of BodyCount bodies. Clears any existing contents. */
		void Init(int32 InCapacity, int32 InBodyCount);

		/** Store every body's current state under Frame, evicting the oldest slot if full. */
		void Capture(int32 Frame, const TArray<b3BodyId>& Bodies);

		/** Restore all bodies to their stored state at Frame. False if Frame is no longer retained
		 *  (older than the ring window) or the body count differs. */
		bool Restore(int32 Frame, const TArray<b3BodyId>& Bodies) const;

		/** Read stored states for Frame without touching the world. False if not retained. */
		bool GetStates(int32 Frame, TArray<FBodyState>& OutStates) const;

		bool IsRetained(int32 Frame) const { return FindSlot(Frame) != INDEX_NONE; }
		int32 NewestFrame() const { return Newest; }
		int32 OldestRetainedFrame() const;
		bool IsEmpty() const { return Newest < 0; }

	private:
		int32 FindSlot(int32 Frame) const;

		int32 Capacity = 0;
		int32 BodyCount = 0;
		int32 Newest = -1;                 // highest frame captured, -1 when empty
		TArray<FBodyState> States;         // Capacity * BodyCount, row-major by slot
		TArray<int32> FrameAt;             // Capacity; frame in each slot, -1 = empty
	};

	/** Outcome of a reconcile, for logging / smoothing decisions. */
	struct FReconcileResult
	{
		bool bCorrected = false;     // the world was rolled back and replayed
		int32 ReplayedFrames = 0;    // how many frames were re-stepped
		double MaxCorrection = 0.0;  // largest per-body position change from the correction (metres)
	};

	/**
	 * Roll World back to AuthFrame, overwrite the bodies named in AuthIndices with AuthStates,
	 * keep every other body at its ring state for that frame, then re-step to PresentFrame -
	 * re-capturing the ring as it goes so it stays valid. Deterministic replay means the result
	 * matches the authority's forward sim from the same state.
	 *
	 * Skips the rollback when the authoritative state already agrees with the ring within
	 * PositionTolerance (metres) for every corrected body - the common case when prediction was
	 * right, so no visible pop. Returns what happened.
	 *
	 * @param AuthIndices indices into Bodies that AuthStates corresponds to (partial auth allowed)
	 */
	BOX3DUNREAL_API FReconcileResult ReconcileAndReplay(
		b3WorldId World,
		const TArray<b3BodyId>& Bodies,
		FSnapshotRing& Ring,
		int32 AuthFrame,
		const TArray<int32>& AuthIndices,
		const TArray<FBodyState>& AuthStates,
		int32 PresentFrame,
		float TimeStep,
		int32 SubStepCount,
		double PositionTolerance = 0.02); // 2 cm
}
