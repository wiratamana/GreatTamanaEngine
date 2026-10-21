# PBR materials (glTF import, GGX shading, material lifetime)

Imported glTF meshes draw with a tangent-space normal map, a metallic-roughness map, an occlusion map and a GGX sun
specular. PMX models and primitive shapes keep the old base-color shaders. Scene color stays linear HDR; ToneMapping does
the display transform (see `architecture/rendering.md`).

## Image roles and color spaces

`MaterialImageRole` (`src/Renderer/MaterialImageRole.h`) names the four material slots. Only base color is sRGB.

| Role | Set 0 binding | GPU format | Channels |
|---|---|---|---|
| `BaseColor` | 0 | RGBA8_SRGB | rgb color, a alpha |
| `Normal` | 2 | RGBA8_UNorm | rgb = tangent-space normal * 0.5 + 0.5 |
| `MetallicRoughness` | 3 | RGBA8_UNorm | G roughness, B metallic |
| `Occlusion` | 4 | RGBA8_UNorm | R occlusion (1 = unoccluded) |

Binding 1 is the 48-byte `MaterialParams` block (`src/Renderer/MaterialParams.h`, `src/Shaders/MaterialParams.glsl`):
`baseColorFactor`, then `params` (alpha cutoff, metallic, roughness, normal scale) and `pbr` (occlusion strength). A test
parses the GLSL block and compares it with the C++ struct. `MakeMaterialParams` clamps every value and maps NaN to the default.

A missing image is replaced by a 1x1 default image: white sRGB, flat normal (128,128,255), white metallic-roughness, white
occlusion. Every set is complete: all five bindings are written, also for PMX materials. Mip chains of data images are plain
linear averages. The shader normalizes the sampled normal.

## Tangent convention

A vertex tangent is a `Vec4`: xyz = unit tangent T, w = +1 or -1. The bitangent is `B = cross(N, T) * w`. The normal map
is decoded as `n = rgb * 2 - 1`, `n.xy *= normalScale`, `N' = normalize(T * n.x + B * n.y + N * n.z)`. Green is +B (OpenGL
convention). Never flip green.

- **Import.** The loader converts an authored `TANGENT` to engine space (Z is mirrored): `T = ToEngineSpace(A * T_gltf)`,
  `w = w_gltf * (node mirrored ? -1 : 1) * -1`, then orthogonalizes T against N.
- **Generation.** A primitive without `TANGENT` gets MikkTSpace tangents (`GenerateMeshTangents`, `src/Assets/MeshTangents.cpp`).
  The uv is passed as `(u, 1 - v)`. The sign w is computed from the triangle's own uv gradient, not taken from MikkTSpace,
  so authored and generated tangents mean the same thing. A vertex whose corners need different frames is split.
  `FallbackTangent` is used when no tangent can be derived.
- **Storage.** The mesh `*.gta` payload has an optional trailing tangent section (count + 16 bytes per vertex). The decoder
  rejects a wrong count, a truncated block and any trailing bytes.
- **Vertex shader.** World T = `mat3(model) * T`; `w` is multiplied by the sign of `determinant(mat3(model))`. The world
  normal uses the cofactor matrix times that sign, which is exact for any scale and for mirroring matrices.
- **Back faces.** `side = (gl_FrontFacing ? 1 : -1) * windingSign`. When `side < 0` the whole frame (T, B, N) is negated.

## Vertex layout and pipelines

`VertexLayout::PositionNormalUvTangent` uses `MeshVertexTangent` (48 bytes: position 0, normal 12, uv 24, tangent 32).
`MeshAssetPipelineKind` (`src/Game/Instantiation/MeshAssetPipelineKind.h`) picks one pipeline per part:
`Untextured`, `Textured`, `TexturedAlphaTest` (old layout) or `Pbr`, `PbrAlphaTest` (`PbrMesh.vert` + `PbrMesh.frag` /
`PbrMeshAlphaTest.frag`). A mesh uses the tangent layout for all textured parts when it is not skinned, carries one tangent
per vertex and every textured slice is a glTF material (`CanUseTangentLayout`). Otherwise the old layout is used for all of
them. A mesh `*.gta` without tangents still renders through the old path. Sun shadows need a depth-only pipeline per layout
(see `shadow-mapping.md`).

## Shading model

Sun plus hemisphere ambient; there is no environment map. Code: `src/Shaders/PbrBrdf.glsl`, `PbrMaterial.glsl`; CPU
reference and tests: `src/Math/PbrBrdf.h`.

- `roughness = clamp(perceptualRoughness, 0.045, 1)`, `alpha = roughness^2`, `F0 = mix(0.04, baseColor, metallic)`,
  `diffuseColor = baseColor * (1 - metallic)`.
- Sun term = `sunIlluminance * NdotL * (1 - strength * shadow) * horizon`, where `horizon = saturate(dot(Ng, L) * 8)`. The
  shadow lookup uses the geometric normal Ng, never the bump normal.
- Diffuse is scaled by `kDirectScale` (0.65). Specular is `D_GGX * V_SmithGGXCorrelated * F_Schlick` times
  `kDirectScale * pi` (the Lambert term has no 1/pi).
- Ambient = `diffuseColor * HemisphereAmbient(N) * occlusion + HemisphereAmbient(reflect(-V, N)) * EnvBrdfApprox * SpecularOcclusion`.
  Shadows remove only the sun term. Occlusion multiplies ambient only.
- V comes from the eye position in the scene uniform block. `SceneLighting` writes it for every view, with or without shadow data.
- The scene block holds the sun and ambient intensity already multiplied by the atmosphere record when
  `AtmosphereSettings::sceneLightFollowsAtmosphere` is on. The shader does not know about the atmosphere.
- The radiance is clamped to `kMaxSceneRadiance` = 8192, because scene color is RGBA16F and a smooth highlight would
  otherwise become +inf and then a black pixel.

## Material debug views

`SceneLighting` has `materialDebugView` (0..8), packed in `groundAmbientAndMaterialDebugView.w` of the scene block. The
shader writes the value as `SrgbToLinear(value)`, so with ToneMapping `Clamp`, exposure 1, saturation 1, dither 0 the
captured image shows the value exactly. Switch off features that change scene color after the mesh pass (Atmosphere, Gtao,
VolumetricLight) before comparing colors.

| Value | View | Value | View |
|---|---|---|---|
| 0 | off | 5 | tangent T (xyz * 0.5 + 0.5) |
| 1 | shading normal | 6 | handedness w (green +1, red -1) |
| 2 | roughness (grey) | 7 | facing (green front, red back) |
| 3 | metallic (grey) | 8 | base color (unlit) |
| 4 | occlusion (grey) | | |

Live check over HTTP (plain `GET`; the Game tab must be visible for `/get_game_view`). The capture is the game view image
only, no screen grab:

```
GET /activate_tab?name=Game
GET /feature_debug/state?name=SceneLighting
GET /feature_debug/set?name=ToneMapping&mode=Clamp&exposure=1&saturation=1&dither=0
GET /feature_debug/set?name=SceneLighting&materialDebugView=2      then GET /get_game_view
GET /feature_debug/set?name=SceneLighting&materialDebugView=0
GET /feature_debug/invoke?name=ToneMapping&action=reset_defaults
GET /get_logs?min_level=warning
```

`materialDebugView` is an integer key. A value of 9 or more, a negative value or a non-integer is rejected with a 400 that
names the key. The same setting is the "Material debug view" combo of the **SceneLighting** editor window. For one draw's
bound images use `/frame_debugger/open`, `/enable?value=true`, `/capture`, `/select_event?index=<n>` (see `frame-debugger.md`).

## Failure policy

Nothing here throws out of a render or load call.

| Failure | Result |
|---|---|
| Base color image missing or undecodable | the part uses the default white image (flat color); one warning per Guid |
| Normal, metallic-roughness or occlusion image missing | that slot uses its default image; one warning per Guid |
| GPU creation of a material fails | `Resolve()` logs one warning and returns an invalid handle; the part draws untextured |
| One upload chunk fails | every image of that chunk is marked failed (one warning per Guid) |
| Upload fence wait fails | the factory logs, waits for the device once and throws; the chunk is treated as failed |
| Bad tangent section in a mesh payload | `DecodeMeshDataFromBytes` returns `nullopt`; the spawn fails with the existing error |
| Bad count or section in the rig blob | `DecodeRigDataFromBytes` returns `nullopt`; the catalog logs one warning |
| `TANGENT` accessor missing or wrong type | tangents are generated for that primitive |
| MikkTSpace fails or no uv | every tangent of the primitive is `FallbackTangent`; the import message counts it |
| Draw whose pipeline needs a material set but the handle does not resolve | the draw is skipped, one warning per handle |
| Material still used by an entity or cached asset | the release sweep keeps it |

## Material lifetime

- **Upload.** `CreateMaterialImages` uploads many images with a few submits: one staging buffer, one command buffer, two
  `vkCmdPipelineBarrier2` calls and one fence wait per chunk (64 MiB or 256 images). `MaterialTextureGpuCache::PrepareImages`
  decodes in groups of 48 MiB and logs one summary line ("n images uploaded in m submits"). `Renderer::GetUploadStats()` holds
  the counters.
- **Sets.** A set belongs to one material and is owned by `MaterialSetLease` (`src/Renderer/MaterialSetPool.h`), which
  frees it into the pool (capacity 4096, 75% warning) when the `MaterialTexture` is destroyed. The images live as long as the
  material or the cache that loaded them.
- **Cache.** One image per `(Guid, srgb)`; one material per `(four Guids, MaterialParams bytes)`.
- **Release sweep.** `Core::BuildFrame()` calls `Game::ReleaseUnusedGpuMaterials()` first, on the main thread, before any pass
  records. When a sweep is pending it runs every 30 frames (`ShouldRunMaterialSweep`): materials that no `MeshRenderer` and no
  cached asset part uses are unregistered (`RenderSystem::UnregisterTexture`), then images only the cache still holds are freed.
  The GPU is drained once (`Renderer::DrainGpuForResourceRelease()`) and only when something is freed. Log line:
  "Released N materials and M images (K images, L materials stay)".
- Vertex and index buffers of a replaced mesh asset, and `GpuDrivenBatchCache` entries, are not released by the sweep.

## Not supported

Emissive, `KHR_materials_*`, texture coordinate sets other than `TEXCOORD_0`, `KHR_texture_transform`, glTF sampler modes (all
images use Repeat and linear filtering), image-based lighting, alpha blending, GPU skinning of tangent meshes. The importer
reports unsupported texcoord sets and shading models in its success message.
