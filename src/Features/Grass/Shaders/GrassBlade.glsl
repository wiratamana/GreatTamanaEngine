// Shared blade curve evaluation for Grass.vert and GrassShadow.vert.
#ifndef GRASS_BLADE_GLSL
#define GRASS_BLADE_GLSL

#include "GrassCommon.glsl"

// Evaluates the quadratic blade spine, tip flutter, width offset and tangent at parameter t in [0, 1].
vec3 GrassBladeSample(GrassInstance inst, float height, vec4 ground, float t, float side, float widthAtT,
    out vec3 outSideAxis, out vec3 outTangent)
{
    vec4 frame = unpackSnorm4x8(inst.frame);
    vec2 flutter = unpackHalf2x16(inst.flutter);
    vec2 faceXz = normalize(frame.xy);
    vec2 bendDir = normalize(frame.zw);

    outSideAxis = vec3(faceXz.y, 0.0, -faceXz.x);
    vec3 bendAxis = vec3(bendDir.x, 0.0, bendDir.y);

    float sinA = ground.z;
    float cosA = sqrt(max(1.0 - sinA * sinA, 0.0));
    vec3 control = vec3(0.0, 0.5 * height, 0.0);
    vec3 tip = vec3(0.0, height * cosA, 0.0) + bendAxis * (height * sinA);
    float omt = 1.0 - t;
    vec3 curve = 2.0 * omt * t * control + t * t * tip;
    outTangent = normalize((2.0 - 4.0 * t) * control + 2.0 * t * tip + vec3(0.0, 1e-4, 0.0));

    curve += (outSideAxis * flutter.x + bendAxis * flutter.y) * (t * t);
    return inst.root + curve + outSideAxis * (side * 0.5 * widthAtT);
}

// Eye-relative blade point at parameter t in [0, 1], side in {-1, +1} and width widthAtT (m).
vec3 GrassBladePosition(GrassInstance inst, float t, float side, float widthAtT)
{
    float height = unpackHalf2x16(inst.heightWidth).x;
    vec4 ground = unpackUnorm4x8(inst.ground);
    vec3 sideAxis;
    vec3 tangent;
    return GrassBladeSample(inst, height, ground, t, side, widthAtT, sideAxis, tangent);
}

#endif
