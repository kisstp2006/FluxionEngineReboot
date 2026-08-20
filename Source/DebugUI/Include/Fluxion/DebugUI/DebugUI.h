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

#ifndef FLUXION_DEBUG_UI_H
#define FLUXION_DEBUG_UI_H

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

// PANELS FOR WHOEVER IS BUILDING THE THING, not for whoever plays it.
//
// Sliders, checkboxes and readouts: the controls a developer needs to turn
// a number while watching what it does to the picture. A game's own
// interface is a different problem with different answers (layout that
// survives translation, art direction, input that is part of play) and it
// is not this.
//
// NOTHING HERE NAMES THE LIBRARY UNDERNEATH. What a caller writes is
// panels and controls; which toolkit draws them is this module's business
// and can be replaced without touching a line of the code above it. That
// is the whole reason this API exists rather than the toolkit's own.
//
// IMMEDIATE, WHICH IS WHAT MAKES IT SHORT TO WRITE. There is no widget to
// create, own and destroy: a control is a call, made every frame, that
// reads and writes the caller's own variable. A panel that is not called
// this frame simply is not there.

typedef struct FluxionDebugUIDesc
{
    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;

    // The format of what the panels are drawn INTO -- the screen's, since
    // a developer panel belongs on top of the finished picture rather
    // than inside the scene's light.
    FluxionRHIFormat colorFormat;
} FluxionDebugUIDesc;

// WHAT THE PERSON DID, in whatever way the caller learned it. Given as
// state rather than as events: an immediate-mode control asks "is the
// button down over me now", and a queue of events would have to be turned
// back into exactly this before it could answer.
typedef struct FluxionDebugUIInput
{
    f32 mouseX;
    f32 mouseY;
    bool mouseDown;      // the left button, the only one anything here uses
    f32 scroll;          // wheel notches this frame, positive away from the hand
} FluxionDebugUIInput;

bool Fluxion_DebugUI_Init(const FluxionDebugUIDesc* desc);
void Fluxion_DebugUI_Shutdown(void);
bool Fluxion_DebugUI_IsReady(void);

// Everything between these two is what the UI is this frame.
void Fluxion_DebugUI_BeginFrame(const FluxionDebugUIInput* input, u32 width, u32 height);
void Fluxion_DebugUI_EndFrame(void);

// A panel, at a place and a size given in pixels. False means it is
// collapsed or closed and its contents should be skipped -- which is why
// it is written as a condition rather than as a statement.
bool Fluxion_DebugUI_BeginPanel(const char* title, f32 x, f32 y, f32 width, f32 height);
void Fluxion_DebugUI_EndPanel(void);

// How the controls after it are laid out: one row of this many equal
// columns, each of this many pixels tall.
void Fluxion_DebugUI_Row(f32 height, u32 columns);

void Fluxion_DebugUI_Label(const char* text);

// Each returns whether the value CHANGED this frame, and writes through
// the pointer it was given -- so the caller's own variable is the state
// and this module keeps none of it.
bool Fluxion_DebugUI_Checkbox(const char* label, bool* value);
bool Fluxion_DebugUI_SliderFloat(const char* label, f32* value, f32 lowest, f32 highest, f32 step);
bool Fluxion_DebugUI_Button(const char* label);

// WHERE THE PANELS ACTUALLY LAND. Called after EndFrame, with the target
// the caller wants them on top of -- which is why this module draws
// nothing on its own and is never part of a render graph: a developer
// panel belongs over the finished frame, not in it.
void Fluxion_DebugUI_Render(FluxionRHICommandListHandle commandList, FluxionRHITextureViewHandle target, u32 width, u32 height);

#ifdef __cplusplus
}
#endif

#endif // FLUXION_DEBUG_UI_H
