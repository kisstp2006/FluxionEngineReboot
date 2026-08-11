#pragma once

#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>

#include <type_traits>

// Thin operator forwards over the existing C Vec3/Mat4/Quat functions --
// no new math logic, same POD layout/ABI as the C types (checked below).
// Declared in the global namespace, matching where FluxionVec3 etc.
// themselves live (Math.h has no C++ namespace) -- operators need to sit
// in an associated namespace of their operand type to be found by
// argument-dependent lookup in ordinary `a + b` expressions.
//
// Vec2/Vec4 aren't wrapped: Math.h itself doesn't operate on them yet
// either, so there's nothing to forward to.

inline FluxionVec3 operator+(FluxionVec3 a, FluxionVec3 b) { return Fluxion_Vec3_Add(a, b); }
inline FluxionVec3 operator-(FluxionVec3 a, FluxionVec3 b) { return Fluxion_Vec3_Sub(a, b); }
inline FluxionVec3 operator*(FluxionVec3 a, f32 scalar) { return Fluxion_Vec3_Scale(a, scalar); }
inline FluxionVec3 operator*(f32 scalar, FluxionVec3 a) { return Fluxion_Vec3_Scale(a, scalar); }

inline f32 Dot(FluxionVec3 a, FluxionVec3 b) { return Fluxion_Vec3_Dot(a, b); }
inline FluxionVec3 Cross(FluxionVec3 a, FluxionVec3 b) { return Fluxion_Vec3_Cross(a, b); }
inline f32 Length(FluxionVec3 a) { return Fluxion_Vec3_Length(a); }
inline FluxionVec3 Normalize(FluxionVec3 a) { return Fluxion_Vec3_Normalize(a); }

inline FluxionMat4 operator*(FluxionMat4 a, FluxionMat4 b) { return Fluxion_Mat4_Multiply(a, b); }

inline FluxionQuat operator*(FluxionQuat a, FluxionQuat b) { return Fluxion_Quat_Multiply(a, b); }

// Mandatory layout checks (sizeof/standard-layout/trivially-copyable) --
// the operators above only make sense if these POD types truly keep the
// C ABI's layout, so verify it once here instead of assuming it.
static_assert(sizeof(FluxionVec2) == sizeof(f32) * 2);
static_assert(sizeof(FluxionVec3) == sizeof(f32) * 3);
static_assert(sizeof(FluxionVec4) == sizeof(f32) * 4);
static_assert(sizeof(FluxionQuat) == sizeof(f32) * 4);
static_assert(sizeof(FluxionMat4) == sizeof(f32) * 16);

static_assert(std::is_standard_layout_v<FluxionVec2>);
static_assert(std::is_standard_layout_v<FluxionVec3>);
static_assert(std::is_standard_layout_v<FluxionVec4>);
static_assert(std::is_standard_layout_v<FluxionQuat>);
static_assert(std::is_standard_layout_v<FluxionMat4>);

static_assert(std::is_trivially_copyable_v<FluxionVec2>);
static_assert(std::is_trivially_copyable_v<FluxionVec3>);
static_assert(std::is_trivially_copyable_v<FluxionVec4>);
static_assert(std::is_trivially_copyable_v<FluxionQuat>);
static_assert(std::is_trivially_copyable_v<FluxionMat4>);
