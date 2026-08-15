#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Working out an exposure the way a camera does.
//
// The renderer wants one number: what to multiply the light by so that
// the scene lands in the range a screen can show. A person setting that
// number by hand is guessing, and the guess has to change every time the
// lighting does.
//
// A camera has three settings that decide it between them -- how wide the
// aperture is open, how long the shutter is open, and how sensitive the
// film is -- and those are settings people already understand and already
// have intuitions about. So the renderer takes the one number, and this
// is where the three become it.
//
// The lighting has to be in real units for any of this to mean anything.
// It is: a light's colour IS its intensity in this engine, and there is
// no separate brightness slider anywhere to undo that.

// The exposure value for these camera settings, at a sensitivity of one
// hundred.
//
// Zero is a dim interior, fifteen is bright daylight, and each step of
// one halves the light that reaches the sensor.
//
// aperture is the f-number (1.4, 2.8, 16), shutterSeconds is how long the
// shutter is open (1/60 is 0.0167), sensitivity is the ISO (100, 400,
// 3200). Returns zero for a setting that is not a positive number, since
// none of the three can be.
f32 Fluxion_Exposure_EV100(f32 aperture, f32 shutterSeconds, f32 sensitivity);

// And the multiplier itself, which is what a render view is given.
//
// The 1.2 in it is the standard calibration between what a meter reads
// and the middle grey it should produce -- not a taste setting, and not
// somewhere to put one.
f32 Fluxion_Exposure_FromEV100(f32 ev100);

// The two above, together, which is what a caller with a camera actually
// wants.
f32 Fluxion_Exposure_FromCamera(f32 aperture, f32 shutterSeconds, f32 sensitivity);

#ifdef __cplusplus
}
#endif
