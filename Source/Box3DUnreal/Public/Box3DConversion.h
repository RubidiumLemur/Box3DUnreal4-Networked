// Author: Antonio Lattanzio - emptyvessel

#pragma once

#include "CoreMinimal.h"
#include <box3d/box3d.h>

// The single place transforms cross the Unreal <-> box3d boundary.
//
// Unreal:  left-handed, Z-up, centimeters, LWC (FVector/FQuat are double).
// box3d:   right-handed, no intrinsic up-axis, meters. Built with
//          BOX3D_DOUBLE_PRECISION, so b3Pos translation is double, rotation float.
//
// Convention chosen here: keep Z as "up" in both spaces and convert handedness by
// negating Y. That reflection (det = -1) maps left-handed <-> right-handed while
// leaving gravity on Z. The quaternion transform for a Y-negation reflection is
// (x, y, z, w) -> (-x, y, -z, w), which is its own inverse.
namespace Box3D
{
	static constexpr double UnrealToMeters = 0.01;  // cm -> m
	static constexpr double MetersToUnreal = 100.0; // m  -> cm

	/** World position (double translation). Use for body positions. */
	FORCEINLINE b3Pos ToBox3DPosition(const FVector& V)
	{
		return b3Pos{ V.X * UnrealToMeters, -V.Y * UnrealToMeters, V.Z * UnrealToMeters };
	}

	FORCEINLINE FVector FromBox3DPosition(const b3Pos& P)
	{
		return FVector(P.x * MetersToUnreal, -P.y * MetersToUnreal, P.z * MetersToUnreal);
	}

	/** Direction / velocity / gravity (float). No origin offset, same linear map. */
	FORCEINLINE b3Vec3 ToBox3DVector(const FVector& V)
	{
		return b3Vec3{
			static_cast<float>(V.X * UnrealToMeters),
			static_cast<float>(-V.Y * UnrealToMeters),
			static_cast<float>(V.Z * UnrealToMeters) };
	}

	FORCEINLINE FVector FromBox3DVector(const b3Vec3& V)
	{
		return FVector(V.x * MetersToUnreal, -V.y * MetersToUnreal, V.z * MetersToUnreal);
	}

	/** Unit direction / surface normal: handedness only, never the cm<->m scale (scaling a
	 *  normal by 100 is the easy mistake - it stays unit length here). */
	FORCEINLINE b3Vec3 ToBox3DDirection(const FVector& V)
	{
		return b3Vec3{ static_cast<float>(V.X), static_cast<float>(-V.Y), static_cast<float>(V.Z) };
	}

	FORCEINLINE FVector FromBox3DDirection(const b3Vec3& V)
	{
		return FVector(V.x, -V.y, V.z);
	}

	FORCEINLINE b3Quat ToBox3DQuat(const FQuat& Q)
	{
		return b3Quat{
			b3Vec3{ static_cast<float>(-Q.X), static_cast<float>(Q.Y), static_cast<float>(-Q.Z) },
			static_cast<float>(Q.W) };
	}

	FORCEINLINE FQuat FromBox3DQuat(const b3Quat& Q)
	{
		return FQuat(-Q.v.x, Q.v.y, -Q.v.z, Q.s);
	}

	/** box3d carries no scale, so this yields a unit-scale transform. */
	FORCEINLINE FTransform FromBox3DTransform(const b3WorldTransform& T)
	{
		return FTransform(FromBox3DQuat(T.q), FromBox3DPosition(T.p));
	}
}
