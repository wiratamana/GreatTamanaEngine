// Shared blade curve, twist and width profile evaluation for Grass.vert and GrassShadow.vert.
#ifndef GRASS_BLADE_GLSL
#define GRASS_BLADE_GLSL

#include "GrassCommon.glsl"

// Width along the blade at t in [0, 1], blending from simple taper (0) to a base-heavy profile (1).
float GrassBladeWidth(float width, float taper, float t, float shapeBlend)
{
    float tc = max(t, 0.0);
    float flatProfile = 1.0 - taper * (tc > 0.0 ? pow(tc, 1.5) : 0.0);
    float baseHeavy = (0.7 + 0.3 * smoothstep(0.0, 0.2, tc)) * (1.0 - taper * (tc > 0.0 ? pow(tc, 1.6) : 0.0));
    return width * mix(flatProfile, baseHeavy, shapeBlend);
}

// Evaluates the quadratic blade spine, axial twist (+-0.6 rad at tip), tip flutter and tangent at t in [0, 1].
vec3 GrassBladeSpine(GrassInstance inst, float height, vec4 ground, float t, float rnd, float twistStrength,
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

    float twist = (rnd - 0.5) * 1.2 * t * twistStrength;
    if (abs(twist) > 1e-5) {
        float c = cos(twist);
        float s = sin(twist);
        outSideAxis = outSideAxis * c + cross(outTangent, outSideAxis) * s
            + outTangent * (dot(outTangent, outSideAxis) * (1.0 - c));
    }

    curve += (outSideAxis * flutter.x + bendAxis * flutter.y) * (t * t);
    return inst.root + curve;
}

// Evaluates the blade vertex position, twisted side axis and spine tangent at parameter t in [0, 1].
vec3 GrassBladeSample(GrassInstance inst, float height, vec4 ground, float t, float side, float widthAtT,
    float rnd, float twistStrength, out vec3 outSideAxis, out vec3 outTangent)
{
    vec3 spine = GrassBladeSpine(inst, height, ground, t, rnd, twistStrength, outSideAxis, outTangent);
    return spine + outSideAxis * (side * 0.5 * widthAtT);
}

// Eye-relative blade point at parameter t in [0, 1], side in [-1, +1] and width widthAtT (m).
vec3 GrassBladePosition(GrassInstance inst, float t, float side, float widthAtT)
{
    float height = unpackHalf2x16(inst.heightWidth).x;
    vec4 ground = unpackUnorm4x8(inst.ground);
    float rnd = float(((inst.colorRnd & 255u) >> 4u) & 15u) * (1.0 / 15.0);
    vec3 sideAxis;
    vec3 tangent;
    return GrassBladeSample(inst, height, ground, t, side, widthAtT, rnd, params.shapeWind.y, sideAxis, tangent);
}

#endif
