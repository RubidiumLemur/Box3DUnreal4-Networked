// Author: Antonio Lattanzio - emptyvessel

#include "Box3DBodyComponent.h"
#include "Box3DConversion.h"
#include "Box3DSubsystem.h"
#include "GameFramework/Actor.h"

// Turns box3d's per-step event buffers into the Blueprint delegates on the body component.
//
// box3d has no callbacks (it's multithreaded), just arrays it overwrites every step - so the
// drain has to run inside the step loop, once per step. Dispatch waits until the loop is
// done, which is also what makes handlers safe: they land between frames, not between two
// fixed steps, so they can spawn and destroy freely.
//
// Server only. A client has no bodies, so nothing fires there.

namespace
{
	/** A box3d shape resolved back to the Unreal side. Either half can be null. */
	struct FShapeRef
	{
		UBox3DBodyComponent* Body = nullptr;
		AActor* Actor = nullptr;
	};

	/**
	 * Component from shape userData, actor from that component - or from the body for bulk
	 * and baked static geometry, which has no component. Pass bMayBeDestroyed for end events,
	 * whose shape ids can already be dead.
	 */
	FShapeRef ResolveShape(b3ShapeId ShapeId, bool bMayBeDestroyed)
	{
		FShapeRef Ref;
		if (bMayBeDestroyed && !b3Shape_IsValid(ShapeId))
		{
			return Ref;
		}

		Ref.Body = static_cast<UBox3DBodyComponent*>(b3Shape_GetUserData(ShapeId));
		if (!IsValid(Ref.Body))
		{
			Ref.Body = nullptr;
		}

		if (Ref.Body != nullptr)
		{
			Ref.Actor = Ref.Body->GetOwner();
		}
		else
		{
			const b3BodyId OwningBody = b3Shape_GetBody(ShapeId);
			if (B3_IS_NON_NULL(OwningBody))
			{
				Ref.Actor = static_cast<AActor*>(b3Body_GetUserData(OwningBody));
			}
		}

		if (!IsValid(Ref.Actor))
		{
			Ref.Actor = nullptr; // baked static bodies have no actor at runtime
		}
		return Ref;
	}

	FBox3DTouchEvent MakeTouchEvent(const FShapeRef& Self, const FShapeRef& Other, int64 Frame)
	{
		FBox3DTouchEvent Event;
		Event.Body = Self.Body;
		Event.Actor = Self.Actor;
		Event.OtherBody = Other.Body;
		Event.OtherActor = Other.Actor;
		Event.SimulationFrame = Frame;
		return Event;
	}

	/** box3d gives the normal A->B; each side wants it pointing at itself, so A's flips. */
	FBox3DHitEvent MakeHitEvent(const FShapeRef& Self, const FShapeRef& Other, const b3ContactHitEvent& In,
		bool bSelfIsA, int64 Frame)
	{
		FBox3DHitEvent Event;
		Event.Body = Self.Body;
		Event.Actor = Self.Actor;
		Event.OtherBody = Other.Body;
		Event.OtherActor = Other.Actor;
		Event.Location = Box3D::FromBox3DPosition(In.point);
		Event.Normal = Box3D::FromBox3DDirection(In.normal) * (bSelfIsA ? -1.0 : 1.0);
		Event.ApproachSpeed = In.approachSpeed * static_cast<float>(Box3D::MetersToUnreal);
		Event.SimulationFrame = Frame;
		return Event;
	}

	bool WantsContacts(const FShapeRef& Ref)
	{
		return Ref.Body != nullptr && Ref.Body->bGenerateContactEvents;
	}

	bool WantsHits(const FShapeRef& Ref)
	{
		return Ref.Body != nullptr && Ref.Body->bGenerateHitEvents;
	}
} // namespace

void UBox3DSubsystem::DrainStepEvents()
{
	const int64 Frame = SimulationFrame;

	// Contacts and hits are raised when EITHER side asked for them, so a body can hear about
	// touching level geometry that knows nothing about events. Only the sides that asked get
	// an entry, though.
	const b3ContactEvents Contacts = b3World_GetContactEvents(WorldId);

	for (int32 Index = 0; Index < Contacts.beginCount; ++Index)
	{
		const b3ContactBeginTouchEvent& In = Contacts.beginEvents[Index];
		const FShapeRef A = ResolveShape(In.shapeIdA, /*bMayBeDestroyed=*/false);
		const FShapeRef B = ResolveShape(In.shapeIdB, /*bMayBeDestroyed=*/false);

		if (WantsContacts(A))
		{
			PendingBeginContact.Add(MakeTouchEvent(A, B, Frame));
		}
		if (WantsContacts(B))
		{
			PendingBeginContact.Add(MakeTouchEvent(B, A, Frame));
		}
	}

	for (int32 Index = 0; Index < Contacts.endCount; ++Index)
	{
		// Also fires when a shape is destroyed, teleported or refiltered, so these ids may
		// already be dead.
		const b3ContactEndTouchEvent& In = Contacts.endEvents[Index];
		const FShapeRef A = ResolveShape(In.shapeIdA, /*bMayBeDestroyed=*/true);
		const FShapeRef B = ResolveShape(In.shapeIdB, /*bMayBeDestroyed=*/true);

		if (WantsContacts(A))
		{
			PendingEndContact.Add(MakeTouchEvent(A, B, Frame));
		}
		if (WantsContacts(B))
		{
			PendingEndContact.Add(MakeTouchEvent(B, A, Frame));
		}
	}

	const bool bWorldHitListener = OnAnyBox3DHit.IsBound();
	for (int32 Index = 0; Index < Contacts.hitCount; ++Index)
	{
		const b3ContactHitEvent& In = Contacts.hitEvents[Index];
		const FShapeRef A = ResolveShape(In.shapeIdA, /*bMayBeDestroyed=*/false);
		const FShapeRef B = ResolveShape(In.shapeIdB, /*bMayBeDestroyed=*/false);

		if (WantsHits(A))
		{
			PendingHits.Add(MakeHitEvent(A, B, In, /*bSelfIsA=*/true, Frame));
		}
		if (WantsHits(B))
		{
			PendingHits.Add(MakeHitEvent(B, A, In, /*bSelfIsA=*/false, Frame));
		}

		// One world-level entry per collision, from whichever side has a component.
		if (bWorldHitListener)
		{
			if (A.Body != nullptr)
			{
				PendingWorldHits.Add(MakeHitEvent(A, B, In, /*bSelfIsA=*/true, Frame));
			}
			else if (B.Body != nullptr)
			{
				PendingWorldHits.Add(MakeHitEvent(B, A, In, /*bSelfIsA=*/false, Frame));
			}
		}
	}

	// Overlaps are the other way round: box3d needs the flag on the trigger AND the visitor,
	// so a trigger is blind to anything without Generate Overlap Events.
	const b3SensorEvents Sensors = b3World_GetSensorEvents(WorldId);

	for (int32 Index = 0; Index < Sensors.beginCount; ++Index)
	{
		const b3SensorBeginTouchEvent& In = Sensors.beginEvents[Index];
		const FShapeRef Trigger = ResolveShape(In.sensorShapeId, /*bMayBeDestroyed=*/false);
		if (Trigger.Body != nullptr)
		{
			PendingBeginOverlap.Add(MakeTouchEvent(Trigger, ResolveShape(In.visitorShapeId, false), Frame));
		}
	}

	for (int32 Index = 0; Index < Sensors.endCount; ++Index)
	{
		const b3SensorEndTouchEvent& In = Sensors.endEvents[Index];
		const FShapeRef Trigger = ResolveShape(In.sensorShapeId, /*bMayBeDestroyed=*/true);
		if (Trigger.Body != nullptr)
		{
			// The visitor is often gone by now - being destroyed is one way to leave.
			PendingEndOverlap.Add(MakeTouchEvent(Trigger, ResolveShape(In.visitorShapeId, true), Frame));
		}
	}
}

void UBox3DSubsystem::QueueSleepEvent(UBox3DBodyComponent* Body, bool bAwake)
{
	if (!IsValid(Body))
	{
		return;
	}

	FBox3DSleepEvent& Event = (bAwake ? PendingWake : PendingSleep).AddDefaulted_GetRef();
	Event.Body = Body;
	Event.Actor = Body->GetOwner();
	Event.SimulationFrame = SimulationFrame;
}

void UBox3DSubsystem::DispatchPendingEvents()
{
	// Targets are re-checked as we go: an earlier handler may have destroyed the actor a
	// later event points at.
	auto Broadcast = [](auto& Queue, auto Delegate)
	{
		for (const auto& Event : Queue)
		{
			if (UBox3DBodyComponent* Target = Event.Body; IsValid(Target))
			{
				(Target->*Delegate).Broadcast(Event);
			}
		}
		Queue.Reset();
	};

	// End before begin, so leaving one body and entering another reads in that order.
	Broadcast(PendingEndContact, &UBox3DBodyComponent::OnBox3DEndContact);
	Broadcast(PendingBeginContact, &UBox3DBodyComponent::OnBox3DBeginContact);
	Broadcast(PendingEndOverlap, &UBox3DBodyComponent::OnBox3DEndOverlap);
	Broadcast(PendingBeginOverlap, &UBox3DBodyComponent::OnBox3DBeginOverlap);
	Broadcast(PendingHits, &UBox3DBodyComponent::OnBox3DHit);
	Broadcast(PendingSleep, &UBox3DBodyComponent::OnBox3DSleep);
	Broadcast(PendingWake, &UBox3DBodyComponent::OnBox3DWake);

	for (const FBox3DHitEvent& Event : PendingWorldHits)
	{
		OnAnyBox3DHit.Broadcast(Event);
	}
	PendingWorldHits.Reset();
}
