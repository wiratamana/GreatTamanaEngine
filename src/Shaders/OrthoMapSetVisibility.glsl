#ifndef ORTHO_MAP_SET_VISIBILITY_GLSL
#define ORTHO_MAP_SET_VISIBILITY_GLSL

// Sun visibility of a world point against up to 4 orthographic depth maps.
// Define the ORTHO_* macros and `float OrthoMapDepthAt(int k, vec2 uv)` before including.

// Unit directions of the golden-angle spiral, cos/sin of i * 2.39996323.
const vec2 kOrthoSpiralDirection[8] = vec2[8](vec2(1.0, 0.0), vec2(-0.73736882, 0.67549036),
    vec2(0.08742555, -0.99617106), vec2(0.60843907, 0.79360059), vec2(-0.98471355, -0.17418160),
    vec2(0.84375506, -0.53672843), vec2(-0.25960379, 0.96571521), vec2(-0.46090758, -0.88744814));

// Filter pattern shared by every map of one lookup. rotationCosSin = (cos, sin) of the pattern rotation.
struct OrthoTapSetup {
    vec2 rotationCosSin;
    float radiusUv;
    int taps;
};

// Lit fraction in [0,1], or -1 when the point lies outside map k.
float OrthoMapLit(int k, vec3 world, OrthoTapSetup setup)
{
    vec4 clip = ORTHO_VIEW_PROJECTION(k) * vec4(world, 1.0);
    if (!(clip.w > 1e-6)) {
        return -1.0;
    }
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z < 0.0 || ndc.z > 1.0) {
        return -1.0;
    }
    float biasedDepth = ndc.z - ORTHO_TEXEL_WORLD(k) * ORTHO_BIAS_TEXELS / max(ORTHO_DEPTH_RANGE(k), 1e-6);
    if (setup.taps == 1) {
        return biasedDepth > OrthoMapDepthAt(k, uv) ? 0.0 : 1.0;
    }
    float lit = 0.0;
    for (int i = 0; i < setup.taps; ++i) {
        vec2 d = kOrthoSpiralDirection[i];
        vec2 rotated = vec2(d.x * setup.rotationCosSin.x - d.y * setup.rotationCosSin.y,
            d.x * setup.rotationCosSin.y + d.y * setup.rotationCosSin.x);
        vec2 offset = rotated * (sqrt((float(i) + 0.5) / float(setup.taps)) * setup.radiusUv);
        lit += biasedDepth > OrthoMapDepthAt(k, uv + offset) ? 0.0 : 1.0;
    }
    return lit / float(setup.taps);
}

// Sun visibility in [0,1]. Without maps, or outside all of them, the point is lit.
float OrthoMapSetVisibility(vec3 world, float rotation)
{
    int count = clamp(int(ORTHO_COUNT), 0, 4);
    if (count == 0) {
        return 1.0;
    }
    OrthoTapSetup setup;
    setup.taps = clamp(int(ORTHO_TAPS), 1, 8);
    setup.radiusUv = ORTHO_FILTER_TEXELS * ORTHO_INV_MAP_SIZE;
    setup.rotationCosSin = setup.taps > 1 ? vec2(cos(rotation), sin(rotation)) : vec2(1.0, 0.0);

    float planar = dot(world - ORTHO_EYE, ORTHO_FORWARD);
    int c = count - 1;
    for (int i = 0; i < count; ++i) {
        if (planar < ORTHO_SPLIT_FAR(i)) {
            c = i;
            break;
        }
    }
    float lit = OrthoMapLit(c, world, setup);
    if (lit < 0.0) {
        // Outside map c: the next map alone decides, so it needs no blend.
        lit = c + 1 < count ? OrthoMapLit(c + 1, world, setup) : -1.0;
        return lit < 0.0 ? 1.0 : lit;
    }
    if (c + 1 < count) {
        float width = ORTHO_BLEND_WIDTH(c);
        float start = ORTHO_SPLIT_FAR(c) - width;
        if (width > 0.0 && planar > start) {
            float next = OrthoMapLit(c + 1, world, setup);
            if (next >= 0.0) {
                lit = mix(lit, next, clamp((planar - start) / width, 0.0, 1.0));
            }
        }
    }
    return lit;
}

#endif
