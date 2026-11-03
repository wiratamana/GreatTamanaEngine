# ToonCharacter

> Cel-shaded look with an outline for marked characters. For anyone who tunes the toon look or debugs a toon character.
> Verified against code: 2026-11-03

## TL;DR
- Feature `ToonCharacter` in `src/Features/ToonCharacter/`. It redraws marked characters with a toon shader and an inverted-hull outline.
- A character is marked by a `ToonShading` component on its model root. Scenes save it.
- Unmarked meshes are never touched. With the feature off, the normal look returns in the next frame.
- The sun, the cascade shadows and the sun colour still drive the look. Toon parts receive and cast shadows.
- Class data comes from PMX materials (toon ramp, sphere mode, edge flag). No re-import is needed.
- Eyes stay open (no morph blending). The face has no shadow SDF.

## How to mark a character
Add `ToonShading { enabled = true }` to the root entity of the model.
- The feature walks each `MeshRenderer` up its parent chain (at most 64 steps). The nearest `ToonShading` decides. `enabled = false` turns it off for that subtree.
- Only parts with a material texture count. A part with no texture stays on the main draw.
- Scene with no `ToonShading` anywhere: one size check per frame, nothing else.
- The `furina_showcase` preset adds the component for you (see [Preset](#preset-furina_showcase)).

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `ToonCharacter.Look` | PreOpaque, 15 | Collects the toon parts. Publishes the `ToonLook` record | none (`Always`) |
| `ToonCharacter.Surface` | Opaque, 100 | Cel shading. Claims the parts from the main draw | `ToonCharacter.Surface.Draw` |
| `ToonCharacter.Outline` | Opaque, 110 | Inverted hull outline | `ToonCharacter.Outline.Draw` |
| `ToonCharacter.Overlay` | Opaque, 120 | Hair ring band layer | `ToonCharacter.Overlay.Draw` |

- Look runs before `SceneLighting.Uniforms` (20). SceneLighting copies the record into the last 64 bytes of the scene uniform block (`toonLook0..3`, binding 8).
- `ToonLook` (`src/Game/Lighting/ToonLook.h`) is 4 x vec4 = 64 bytes. Channel `ToonCharacter` / `Look`. Zero `v0.x` means "never published": shaders use built-in defaults.
- One `SelectEntities` per view and frame feeds all three draws. Only Surface calls `Claim()`.
- All three draws use `Target(SceneTarget())`, `SceneServices()` and `Only(selection)`. See [Feature kit](../feature-kit.md).
- Shaders: `ToonSurface`, `ToonOutline`, `ToonOverlay` (`.vert` and `.frag`) plus `ToonCommon.glsl` in `src/Features/ToonCharacter/Shaders/`. They draw the position+normal+uv layout only.

| Draw | Cull | Depth | Blend |
|---|---|---|---|
| Surface | None (most PMX parts are double sided; the shader flips the normal on back faces) | Less, write on | Opaque |
| Outline | Front | LessEqual, write on | Opaque |
| Overlay | Back | LessEqual, write off | Alpha blend (`hairHighlight` is the opacity; scene alpha is kept) |

## Toon classes
Each PMX material gets one class id when the mesh is built. The shader reads it from `MaterialParams`.
Code: `ClassifyToonMaterial` in `src/Game/Instantiation/ToonMaterialClass.cpp`.

| Id | Class | Look |
|---|---|---|
| 0 | `Cloth` | Two bands plus a deep band. Blue-ish shade tint (0.65, 0.62, 0.83) |
| 1 | `Skin` | Warm pink shade. No deep band |
| 2 | `FaceSkin` | Like skin, but the normal is flattened toward the head forward axis |
| 3 | `FaceLine` | Unlit. Albedo only |
| 4 | `Eye` | Unlit. Albedo only |
| 5 | `Hair` | Cool shade. Stronger rim (x1.5) |
| 6 | `HairOverlay` | Skipped by Surface. Drawn by Overlay (alpha-blended bluish-white band on the upper hair, masked by the camera-up normal, shadowed by the cascade shadow) |
| 7 | `Gem` | Emissive (`emissiveBoost`) plus the sphere map |
| 8 | `ClothBack` | Like cloth, no outline |

Rules. The first match wins:

| # | Rule | Class |
|---|---|---|
| 1 | No toon ramp (toon index below 0, not shared toon) | `Eye` |
| 2 | Name ends with `+` and sphere mode is Add | `HairOverlay` |
| 3 | Name ends with `+` | `ClothBack` |
| 4 | Sphere mode is Add | `Gem` |
| 5 | English name `Mat_Face` and ramp texture is `hair` | `FaceLine` |
| 6 | English name `Mat_Face` | `FaceSkin` |
| 7 | English name `Mat_Hair` or `Mat_EffectHair` and ramp texture is `hair` | `Hair` |
| 8 | Ramp texture is `skin` | `Skin` |
| 9 | Everything else | `Cloth` |

- The ramp is found by the file name (without extension) of the toon texture: `skin` or `hair`, any case. A shared toon counts as another ramp.
- The rules fit the miHoYo MMD naming. Other models fall back to `Cloth`. That is accepted.
- The outline draws a material when its PMX draw-edge flag is set. `Eye`, `FaceLine`, `HairOverlay` and `ClothBack` never get one (`ToonClassDrawsEdge`).

## Material data
Toon data is carried by the normal material set. No new binding is needed for it, except the sphere image.

| Where | What |
|---|---|
| `MaterialParams.toonClass` (`pbr.y` in GLSL) | Class id as a float |
| `MaterialParams.edgeWidthScale` (`pbr.z`) | PMX edge size / 0.54, clamped 0..8 |
| `MaterialParams.drawEdge` (`pbr.w`) | 1 = the outline draws this material |
| Set 0, binding 5, `MaterialImageRole::Sphere` | PMX sphere map. `RGBA8_SRGB`, already linear in the shader. Default is a black 1x1 image |

- `MaterialParams` stays 48 bytes. The params block is visible to the vertex stage, so the outline vertex shader reads the edge data.
- The existing shaders (`TexturedMesh`, `PbrMesh`) ignore these floats. Other PMX models look the same.
- Materials with the same class and edge data share one material set (the params bytes are part of the cache key).

## Light and shadow rules
- The look follows the first active `DirectionalLight` (or the Atmosphere-tinted sun). Sun direction, colour and intensity come from the scene uniform block.
- Lit colour = `albedo * whiteLit * tone`. `whiteLit` is what a surface facing the sun gets, so exposure matches other objects.
- Receive: the shader reads the cascade maps with `SunShadowAmount`. A shadow pushes the half-Lambert value toward the shade band (`shadowStrength`).
- Cast: the shadow cascades draw every `MeshRenderer`, so toon parts cast shadows. A claim only changes the main opaque draw. The toon selection is never given to a shadow draw.
- Eyes and face lines are unlit. They do not change with the sun direction.
- Shaded pixels keep at least 35% of the lit luminance. A tone below the floor is lifted toward white, never scaled above 1.
- Output radiance is clamped to 8192.

## Outline
- Method: inverted hull. The outline vertex shader moves each vertex along the clip-space normal. The width is in pixels, so it does not grow with distance.
- Width in pixels = `outlineWidthPx * viewHeight / 720`. The record carries `outlineNdcY = widthPx * 2 / viewHeight` and the view aspect.
- Per part width = `outlineWidthPx` x `edgeWidthScale`.
- Parts without an edge are moved outside the clip volume in the vertex shader. They are not discarded.
- Colour = albedo x class tint, never black. Tints: cloth (0.30, 0.36, 0.62), hair (0.03, 0.05, 0.14, dark blue so it reads on white hair), skin and face (0.55, 0.36, 0.34). `outlineDarkness` mixes the tint toward half strength.
- Texels with alpha below 0.5 are discarded in all three fragment shaders.

## Settings and debug keys
Route: `/feature_debug/{state,set,invoke}?name=ToonCharacter`. Panel: `ToonCharacter` (bottom tab).
`Set` applies all keys or none. A bad key gives HTTP 400 and applies nothing. Values are clamped.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off: no parts, no record, no draws |
| `bandThreshold` | 0.50 | 0.05..0.95 | Light / shade border (half Lambert) |
| `bandSoftness` | 0.02 | 0.001..0.25 | Half width of the border smoothstep |
| `deepThreshold` | 0.22 | 0..0.95 (not above `bandThreshold`) | Shade / deep shade border (cloth, hair) |
| `shadowStrength` | 0.60 | 0..1 | How much a cascade shadow pushes toward shade |
| `shadeBlend` | 1.0 | 0..1 | Strength of the coloured shade tint |
| `rimStrength` | 0.35 | 0..2 | Rim light on the sun side |
| `rimWidth` | 0.60 | 0..1 | Where the rim starts (fresnel) |
| `hairHighlight` | 0.8 | 0..1 | Hair ring band opacity |
| `emissiveBoost` | 3.0 | 0..16 | Gem glow multiplier. Bloom turns this into a halo |
| `gemHeuristic` | true | bool | Bright cyan-blue cloth glows (x2) |
| `goldGlint` | true | bool | Gold trim gets a narrow highlight |
| `faceFlatten` | 0.8 | 0..1 | Face normal blend toward the head forward axis |
| `outlineWidthPx` | 1.6 | 0..8 | Outline width at 720 p height |
| `outlineDarkness` | 0.35 | 0..1 | Outline colour darkness |
| `debugView` | 0 | 0..5 | See below. Clamped, not rejected |

Action: `reset_defaults` (no arguments).
Read-only, per view with prefix `game.` or `scene.`: `status`, `frameIndex`, `candidatePartCount`, `claimedEntityCount` (parts the surface pass can draw and took from the main draw).
`status`: `NotRendered`, `Ok`, `FeatureDisabled`, `NoToonParts`, `PassDisabled`, `CaptureArmed`, `DrawRefused`. `FeatureDisabled` wins when `enabled` is false. `DrawRefused` is reported when parts exist but the chain claimed none.

| `debugView` | Writes |
|---|---|
| 0 | Normal toon look |
| 1 | Class colours |
| 2 | Band mask: lit 1, shade 0.5, deep 0 |
| 3 | Shadow amount |
| 4 | Outline only (surface black, outline white) |
| 5 | Albedo only |

- Debug views write plain values. For exact numbers set ToneMapping `mode=Clamp exposure=1 saturation=1 dither=0`.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=ToonCharacter"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=ToonCharacter&debugView=1"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=ToonCharacter&outlineWidthPx=2.4"
curl.exe "http://127.0.0.1:8080/render_graph/set_pass_enabled?name=ToonCharacter.Outline.Draw&enabled=false"
curl.exe http://127.0.0.1:8080/get_game_view -o toon.png
```

## Frame Debugger note
`Claim()` is refused while a Frame Debugger capture is armed for the Game view. Then all three toon draws are skipped, `status` is `CaptureArmed`, and the main draw shows the old look. This is expected. It keeps every part drawn once.

## Honest toggles
- Feature off (`set_feature_enabled`): no toon draw, no claim. The normal look returns.
- `ToonCharacter.Surface.Draw` off: no claim. Outline and Overlay still draw on top of the normal look.
- `ToonCharacter.Outline.Draw` or `.Overlay.Draw` off: only that layer disappears.
- `ToonCharacter.Look` is not a pass. It cannot be toggled. It publishes nothing when the feature is disabled or no toon part exists.

## Preset: furina_showcase
`POST /open_preset_scene {"preset":"furina_showcase"}` builds a clean scene: the model `Furina` (with `ToonShading`), one sun, one portrait camera.
- The model is read from `<exe dir>/Project/Furina.gta`. A missing file gives an error with the text "Furina.gta not found in Project/. Import Furina.pmx first." and the scene stays untouched.
- Camera: vertical FOV 28 degrees, pitched 6.5 degrees up. Distance and height are solved from the bind-pose height so the hat top sits 3% from the top and the hips 85% from the top of a 9:16 frame.
- Sun: pitch 40 degrees, yaw 35 degrees, colour (1.0, 0.96, 0.90). The sun is on the camera-left side.
- Features switched off: `AtmosphereAerialVolume`, `AtmosphereComposite`, `Gtao.*`, `Ssgi.*`, `VolumetricLight.*`. Switched on if they were off: Shadow, SceneLighting, ToonCharacter, Bloom, ToneMapping.
- ToneMapping is set to `PbrNeutral`, exposure 1.4, saturation 1.2. ToonCharacter is set to `enabled=true`, `debugView=0`. Bloom is set to `enabled=true`, `threshold=2.5`, `intensity=0.5` (the white hair and the face stay below the threshold; only the gem glows). The old values of these keys come back on close.
- If `ChatanyaraKuushanku_bassui260717a.gta` is next to the model, it is played. If not, the model stands in T-pose.
- `POST /close_preset_scene` removes the preset entities and restores the feature toggles and the saved settings above. It cannot bring back the scene that was open before.
- Preset code: `src/Editor/FurinaShowcaseSceneOpener.*`, layout maths in `FurinaShowcaseLayout.*`.

## Known gaps
- No face shadow SDF. The face uses a flattened normal.
- No morph blending in the engine, so the eyes cannot close.
- Hard-edge normals (split vertices) can leave small gaps in the outline on clothes. Hair and skin are smooth.
- A thin one-sided sheet (skirt, cape) can show a wrong or no hull, because the outline culls front faces.
- A toon-marked part whose pipeline is not position+normal+uv (for example a glTF part with tangents and a texture) is not claimed. It stays on the main draw and keeps its normal look.
- The front-cull winding was written for PMX data. Check it with a picture on a new model.
- Materials 28..30 of Furina (the `+` skirt layers) draw like cloth with no outline.
- The cyan and gold tricks are colour heuristics, not material data.
- The sun disc must stay out of the frame when Bloom is on. Bloom clamps its input to 64.

## Cost
Measured live at 1264x629 (Release, `furina_showcase`, 32 parts, 39037 triangles). The 1080p cost was not measured and is higher.

| Pass | GPU ms |
|---|---|
| `ToonCharacter.Surface` | 1.60 |
| `ToonCharacter.Outline` | 0.09 |
| `ToonCharacter.Overlay` | 0.11 |
| `Bloom` total | 0.73 (CopyToPrivateTarget 0.22, Prefilter 0.25, Down/Up 0.26) |

- Total 2.53 ms. For comparison the main opaque draw needed 1.08 ms for the same 32 parts.
- Likely reasons for Surface (not profiled per instruction): per pixel cascade shadow reads, ramp and sphere map reads, and double-sided parts. No cheap fix was found.
- Bloom is a fixed cost of 5 down and 5 up compute passes plus one full-resolution copy to the feature target. The prefilter already does only 4 fetches per half-resolution texel. No cheap win is left without cutting quality or adding a new Bloom structure.
- If the budget matters, turn off `Bloom` first (it is a global post effect), then `ToonCharacter.Overlay.Draw`.

## Related
- [Render features](README.md) · [Bloom](bloom.md) · [Scene lighting](scene-lighting.md) · [Shadow](shadow.md) · [Tone mapping](tone-mapping.md)
- [Feature kit](../feature-kit.md) · [PBR materials](../../conventions/pbr-materials.md) · [Asset pipeline](../../architecture/asset-pipeline.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [Render honesty](../../conventions/render-honesty.md)
