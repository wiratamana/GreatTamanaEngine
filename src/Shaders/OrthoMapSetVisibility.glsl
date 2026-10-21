#ifndef ORTHO_MAP_SET_VISIBILITY_GLSL
#define ORTHO_MAP_SET_VISIBILITY_GLSL

// Visibility of a world point against up to 4 orthographic depth maps (a map set). Before the include, define
// ORTHO_* (see ORTHO_VIEW_PROJECTION .. ORTHO_INV_MAP_SIZE in the shaders that use this) and
// `float OrthoMapDepthAt(int k, vec2 uv)`.

// Lit fraction in [0,1], or -1 when the point lies outside map k.
float OrthoMapLit(int k, vec3 world, float rotation)
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
    float bias = ORTHO_TEXEL_WORLD(k) * ORTHO_BIAS_TEXELS / max(ORTHO_DEPTH_RANGE(k), 1e-6);
    int taps = clamp(int(ORTHO_TAPS), 1, 8);
    float radiusUv = ORTHO_FILTER_TEXELS * ORTHO_INV_MAP_SIZE;
    float lit = 0.0;
    for (int i = 0; i < taps; ++i) {
        vec2 offset = vec2(0.0);
        if (taps > 1) {
            float r = sqrt((float(i) + 0.5) / float(taps));
            float theta = float(i) * 2.39996323 + rotation;
            offset = r * vec2(cos(theta), sin(theta)) * radiusUv;
        }
        lit += (ndc.z - bias > OrthoMapDepthAt(k, uv + offset)) ? 0.0 : 1.0;
    }
    return lit / float(taps);
}

// Sun visibility in [0,1]. Without maps, or outside all of them, the point is lit.
float OrthoMapSetVisibility(vec3 world, float rotation)
{
    int count = clamp(int(ORTHO_COUNT), 0, 4);
    if (count == 0) {
        return 1.0;
    }
    float planar = dot(world - ORTHO_EYE, ORTHO_FORWARD);
    int c = count - 1;
    for (int i = 0; i < count; ++i) {
        if (planar < ORTHO_SPLIT_FAR(i)) {
            c = i;
            break;
        }
    }
    float lit = OrthoMapLit(c, world, rotation);
    if (lit < 0.0) {
        lit = c + 1 < count ? OrthoMapLit(c + 1, world, rotation) : -1.0;
        if (lit < 0.0) {
            return 1.0;
        }
    }
    if (c + 1 < count) {
        float width = ORTHO_BLEND_WIDTH(c);
        float start = ORTHO_SPLIT_FAR(c) - width;
        if (width > 0.0 && planar > start) {
            float next = OrthoMapLit(c + 1, world, rotation);
            if (next >= 0.0) {
                lit = mix(lit, next, clamp((planar - start) / width, 0.0, 1.0));
            }
        }
    }
    return lit;
}

#endif
