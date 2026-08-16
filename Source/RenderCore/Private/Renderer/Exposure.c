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

#include <Fluxion/RenderCore/Renderer/Exposure.h>

#include <math.h>

f32 Fluxion_Exposure_EV100(f32 aperture, f32 shutterSeconds, f32 sensitivity)
{
    // None of the three can be zero or negative: an aperture of zero
    // admits no light, a shutter of zero is never open, and a sensitivity
    // of zero records nothing. Zero back rather than an infinity that
    // would travel silently into every pixel of the frame.
    if (!(aperture > 0.0f) || !(shutterSeconds > 0.0f) || !(sensitivity > 0.0f)) return 0.0f;

    // The photographic relation: the square of the f-number over the
    // shutter time, scaled by how far the sensitivity is from a hundred.
    //
    // Squared because the f-number is a RATIO of focal length to aperture
    // DIAMETER, and it is the area that admits light.
    const f32 ratio = (aperture * aperture) / shutterSeconds;
    return log2f(ratio * 100.0f / sensitivity);
}

f32 Fluxion_Exposure_FromEV100(f32 ev100)
{
    // The light that this exposure value calls a middle grey, inverted --
    // so that multiplying a scene by it puts that much light at middle
    // grey. The 1.2 is the standard calibration between an incident
    // reading and the grey it should produce.
    const f32 maxLuminance = 1.2f * powf(2.0f, ev100);
    return 1.0f / maxLuminance;
}

f32 Fluxion_Exposure_FromCamera(f32 aperture, f32 shutterSeconds, f32 sensitivity)
{
    // A setting that was not a positive number gives an exposure value of
    // zero above, which is a real one -- a dim interior -- rather than a
    // refusal. So the refusal is made here instead, where it can be one:
    // an exposure of zero is a black frame, and a caller that asked with
    // a nonsense camera is better told by a black frame it can see than
    // by a plausible picture it cannot question.
    if (!(aperture > 0.0f) || !(shutterSeconds > 0.0f) || !(sensitivity > 0.0f)) return 0.0f;

    return Fluxion_Exposure_FromEV100(Fluxion_Exposure_EV100(aperture, shutterSeconds, sensitivity));
}
