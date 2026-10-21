#ifndef INTERLEAVED_GRADIENT_NOISE_GLSL
#define INTERLEAVED_GRADIENT_NOISE_GLSL

// Cheap screen-space noise in [0,1).
float InterleavedGradientNoise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Same noise, shifted per frame over a 64-frame cycle.
float InterleavedGradientNoise(vec2 p, uint frame)
{
    return InterleavedGradientNoise(p + 5.588238 * float(frame & 63u));
}

#endif
