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

#ifdef __cplusplus
extern "C" {
#endif

// Portable key identifiers — every backend (Win32 VK_*, X11 KeySym)
// translates into this set. FLUXION_KEY_COUNT sizes the fixed keyboard
// state arrays in Input.c; add new keys before it, never after.
typedef enum FluxionKeyCode
{
    FLUXION_KEY_UNKNOWN = 0,

    FLUXION_KEY_A, FLUXION_KEY_B, FLUXION_KEY_C, FLUXION_KEY_D, FLUXION_KEY_E,
    FLUXION_KEY_F, FLUXION_KEY_G, FLUXION_KEY_H, FLUXION_KEY_I, FLUXION_KEY_J,
    FLUXION_KEY_K, FLUXION_KEY_L, FLUXION_KEY_M, FLUXION_KEY_N, FLUXION_KEY_O,
    FLUXION_KEY_P, FLUXION_KEY_Q, FLUXION_KEY_R, FLUXION_KEY_S, FLUXION_KEY_T,
    FLUXION_KEY_U, FLUXION_KEY_V, FLUXION_KEY_W, FLUXION_KEY_X, FLUXION_KEY_Y,
    FLUXION_KEY_Z,

    FLUXION_KEY_0, FLUXION_KEY_1, FLUXION_KEY_2, FLUXION_KEY_3, FLUXION_KEY_4,
    FLUXION_KEY_5, FLUXION_KEY_6, FLUXION_KEY_7, FLUXION_KEY_8, FLUXION_KEY_9,

    FLUXION_KEY_F1, FLUXION_KEY_F2, FLUXION_KEY_F3, FLUXION_KEY_F4,
    FLUXION_KEY_F5, FLUXION_KEY_F6, FLUXION_KEY_F7, FLUXION_KEY_F8,
    FLUXION_KEY_F9, FLUXION_KEY_F10, FLUXION_KEY_F11, FLUXION_KEY_F12,

    FLUXION_KEY_ESCAPE,
    FLUXION_KEY_TAB,
    FLUXION_KEY_CAPS_LOCK,
    FLUXION_KEY_LEFT_SHIFT,
    FLUXION_KEY_RIGHT_SHIFT,
    FLUXION_KEY_LEFT_CONTROL,
    FLUXION_KEY_RIGHT_CONTROL,
    FLUXION_KEY_LEFT_ALT,
    FLUXION_KEY_RIGHT_ALT,
    FLUXION_KEY_SPACE,
    FLUXION_KEY_ENTER,
    FLUXION_KEY_BACKSPACE,
    FLUXION_KEY_DELETE,
    FLUXION_KEY_INSERT,
    FLUXION_KEY_HOME,
    FLUXION_KEY_END,
    FLUXION_KEY_PAGE_UP,
    FLUXION_KEY_PAGE_DOWN,

    FLUXION_KEY_UP,
    FLUXION_KEY_DOWN,
    FLUXION_KEY_LEFT,
    FLUXION_KEY_RIGHT,

    FLUXION_KEY_NUMPAD_0, FLUXION_KEY_NUMPAD_1, FLUXION_KEY_NUMPAD_2,
    FLUXION_KEY_NUMPAD_3, FLUXION_KEY_NUMPAD_4, FLUXION_KEY_NUMPAD_5,
    FLUXION_KEY_NUMPAD_6, FLUXION_KEY_NUMPAD_7, FLUXION_KEY_NUMPAD_8,
    FLUXION_KEY_NUMPAD_9,

    FLUXION_KEY_COUNT,
} FluxionKeyCode;

#ifdef __cplusplus
}
#endif
