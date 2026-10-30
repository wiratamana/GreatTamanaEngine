# Scene lighting

> Feature that writes sun, ambient and shadow data for every lit material. For anyone debugging lit shading.
> Verified against code: 2026-10-30

## TL;DR
- Feature `SceneLighting` in `src/Features/SceneLighting/`. It has no shader and no render pass.
- Every frame, for every view, it writes one uniform block: scene-services binding 8.
- The block holds the sun, hemisphere ambient, camera eye and the shadow cascade data.
- It has settings for ambient color and a material debug view.
- The sun is the first active ECS `DirectionalLight`. With none, a fixed placeholder sun is used.

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `SceneLighting.Uniforms` | PreOpaque, 20 | Packs and writes the block | none (`Always`) |

- It must run after `Atmosphere.SunLight` (5) and `Shadow.Cascades` (10). A `static_assert` guards the sun-light order.
- Inputs: ECS `DirectionalLight`, channel `Atmosphere.SunLight`, channel `Shadow.Receiver`.
- The sun tint scales the sun illuminance. The ambient scale scales the ambient intensity.
- Active in both views.

## The uniform block

| Fact | Value |
|---|---|
| Location | scene-services binding 8, `std140` |
| Struct | `SceneLightingUniformData` in `SceneLightingTypes.h`, 448 bytes |
| Limit | 512 bytes (`kSceneGlobalUniformBlockSize`) |
| Shader side | `SceneGlobalUniformBlock` in `src/Shaders/DirectionalLighting.glsl` |
| Same layout | Member names and order match the GLSL block. A `static_assert` checks the size |

- No receiver, or `materialStrength == 0`: all cascade fields are zero. The eye is still written.
- A non-finite sun direction becomes up. A non-finite eye becomes zero.
- If the block was never written, `DirectionalLighting.glsl` uses a default sun and ambient. Turning Shadow off never makes materials black.

## Settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `skyAmbientR`, `G`, `B` | 1.26, 1.31, 1.40 | 0..4 | Ambient from above |
| `groundAmbientR`, `G`, `B` | 0.66, 0.57, 0.46 | 0..4 | Ambient from below |
| `ambientIntensity` | 0.35 | 0..8 | Ambient multiplier. Added to the sun term |
| `materialDebugView` | 0 | 0..8 (integer) | Shows one material value. 9 or more is HTTP 400 |

| `materialDebugView` | Shows |
|---|---|
| 0 | Off |
| 1, 2, 3, 4 | Shading normal, roughness, metallic, occlusion |
| 5, 6, 7, 8 | Tangent, handedness (green +, red -), facing (green front), base color |

- Values are written as `SrgbToLinear(value)`. ToneMapping encodes them back.
- To read exact values: ToneMapping `mode=Clamp exposure=1 saturation=1 dither=0`, and turn off Atmosphere, Gtao, VolumetricLight.

## Debug keys
- Writable: the 7 float keys and `materialDebugView`. Action: `reset_defaults` (no arguments).
- Read-only, per view with prefix `game.` or `scene.`: `atmosphereLight`, `sunTintR/G/B`, `ambientScale`, `frameIndex`. They show the sun light record applied last frame.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=SceneLighting"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=SceneLighting&materialDebugView=2"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
```

## Limits
- Only one sun. The first active `DirectionalLight` wins.
- A sun below the horizon with `sceneLightFollowsAtmosphere` gives black light. A `DirectionalLight` pointing up gives a black scene.
- Settings are not saved in scene files.

## Related
- [Render features](README.md) · [Atmosphere](atmosphere.md) · [Shadow](shadow.md) · [Tone mapping](tone-mapping.md)
- [PBR materials](../../conventions/pbr-materials.md) · [HTTP cheat sheet](../../debugging/http-cheat-sheet.md)
