// Shared toon constants and helpers. Include after DirectionalLighting.glsl.
#ifndef TOON_COMMON_GLSL
#define TOON_COMMON_GLSL

// Class ids; pinned against ToonClass in Game/Lighting/ToonClass.h.
const int kToonCloth = 0;
const int kToonSkin = 1;
const int kToonFaceSkin = 2;
const int kToonFaceLine = 3;
const int kToonEye = 4;
const int kToonHair = 5;
const int kToonHairOverlay = 6;
const int kToonGem = 7;
const int kToonClothBack = 8;

const float kToonAlphaCutoff = 0.5;
const float kToonMinShadeLuma = 0.35;   // A shaded pixel keeps at least this share of the lit luminance.
const float kToonMaxRadiance = 8192.0;
const vec3 kToonLuma = vec3(0.2126, 0.7152, 0.0722);
const vec3 kToonRimTint = vec3(0.75, 0.90, 1.00);

// Per class: shade multiplier, deep-shade multiplier, outline tint, debug colour.
const vec3 kToonShadeTint[9] = vec3[9](
    vec3(0.42, 0.48, 0.82), vec3(0.92, 0.74, 0.72), vec3(0.97, 0.86, 0.84),
    vec3(1.0), vec3(1.0), vec3(0.62, 0.70, 0.93),
    vec3(1.0), vec3(1.0), vec3(0.42, 0.48, 0.82));

const vec3 kToonDeepTint[9] = vec3[9](
    vec3(0.80, 0.82, 0.92), vec3(1.0), vec3(1.0),
    vec3(1.0), vec3(1.0), vec3(0.85, 0.88, 0.95),
    vec3(1.0), vec3(1.0), vec3(0.80, 0.82, 0.92));

const vec3 kToonOutlineTint[9] = vec3[9](
    vec3(0.30, 0.36, 0.62), vec3(0.55, 0.36, 0.34), vec3(0.55, 0.36, 0.34),
    vec3(0.30, 0.36, 0.62), vec3(0.30, 0.36, 0.62), vec3(0.45, 0.55, 0.78),
    vec3(0.30, 0.36, 0.62), vec3(0.30, 0.36, 0.62), vec3(0.30, 0.36, 0.62));

const vec3 kToonClassDebugColor[9] = vec3[9](
    vec3(0.0, 0.0, 1.0), vec3(1.0, 0.8, 0.6), vec3(1.0, 0.5, 0.5),
    vec3(0.5, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 1.0),
    vec3(1.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 0.0, 0.5));

// Rim strength multiplier per class (0 = no rim).
const float kToonRimScale[9] = float[9](1.0, 0.5, 0.5, 0.0, 0.0, 1.5, 0.0, 1.0, 1.0);

// Out-of-range ids fall back to Cloth.
int ToonClassOf(float rawId)
{
    int id = int(rawId + 0.5);
    return (id < 0 || id > 8) ? kToonCloth : id;
}

// Toon look values from the scene uniform block tail; defaults while the tail is all zeros (never written).
struct ToonLookParams
{
    float bandThreshold;
    float bandSoftness;
    float deepThreshold;
    float shadowStrength;
    float rimStrength;
    float rimWidth;
    float hairHighlight;
    float emissiveBoost;
    float faceFlatten;
    float outlineNdcY;     // Outline width in NDC units of the viewport height.
    float outlineDarkness;
    int debugView;
    float shadeBlend;
    float gemHeuristic;
    float goldGlint;
    float viewportAspect;  // Width / height.
};

ToonLookParams ReadToonLook()
{
    if (uSceneLighting.toonLook0.x <= 0.0) {
        return ToonLookParams(0.50, 0.02, 0.22, 0.60, 0.35, 0.60, 0.80, 3.0,
                              0.80, 0.0044444, 0.35, 0, 1.0, 1.0, 1.0, 0.5625);
    }
    return ToonLookParams(uSceneLighting.toonLook0.x, uSceneLighting.toonLook0.y,
                          uSceneLighting.toonLook0.z, uSceneLighting.toonLook0.w,
                          uSceneLighting.toonLook1.x, uSceneLighting.toonLook1.y,
                          uSceneLighting.toonLook1.z, uSceneLighting.toonLook1.w,
                          uSceneLighting.toonLook2.x, uSceneLighting.toonLook2.y,
                          uSceneLighting.toonLook2.z, int(uSceneLighting.toonLook2.w + 0.5),
                          uSceneLighting.toonLook3.x, uSceneLighting.toonLook3.y,
                          uSceneLighting.toonLook3.z, max(uSceneLighting.toonLook3.w, 0.01));
}

// Matcap uv of a world normal in the camera basis (v grows downward).
vec2 ToonMatcapUv(vec3 worldNormal)
{
    vec3 forward = uSceneLighting.cameraForwardAndFadeEnd.xyz;
    forward = (dot(forward, forward) > 1e-6) ? normalize(forward) : vec3(0.0, 0.0, 1.0);
    vec3 right = cross(vec3(0.0, 1.0, 0.0), forward);
    right = (dot(right, right) > 1e-6) ? normalize(right) : vec3(1.0, 0.0, 0.0);
    vec3 up = cross(forward, right);
    return vec2(dot(worldNormal, right) * 0.5 + 0.5, 0.5 - dot(worldNormal, up) * 0.5);
}

// x hue in degrees, y saturation, z value.
vec3 ToonRgbToHsv(vec3 c)
{
    float mx = max(c.r, max(c.g, c.b));
    float d = mx - min(c.r, min(c.g, c.b));
    float hue = 0.0;
    if (d > 1e-5) {
        if (mx == c.r) { hue = mod((c.g - c.b) / d, 6.0); }
        else if (mx == c.g) { hue = (c.b - c.r) / d + 2.0; }
        else { hue = (c.r - c.g) / d + 4.0; }
    }
    return vec3(hue * 60.0, mx > 1e-5 ? d / mx : 0.0, mx);
}

#endif // TOON_COMMON_GLSL
