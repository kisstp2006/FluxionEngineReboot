// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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
