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
