# PBR materials
> Rules for glTF materials: image roles, tangent convention, GGX shading, and material GPU lifetime. For anyone touching material shaders, import or the material cache.
> Verified against code: 2026-10-30

## TL;DR
- Imported glTF meshes use a normal map, a metallic-roughness map, an occlusion map and a GGX sun specular.
- PMX models and primitives keep the older base-color shaders.
- Scene color stays linear HDR. Tone mapping does the display transform (see [Tone mapping](../reference/render-features/tone-mapping.md)).
- Only base color is sRGB. Every other image is linear.
- Nothing here throws. A bad image or material falls back to a default and logs once.

## Rules
1. Never flip the green channel of the normal map. Green is +B (OpenGL convention).
2. A vertex tangent is a `Vec4`: xyz is the unit tangent T, w is +1 or -1. The bitangent is `cross(N, T) * w`.
3. The shadow lookup uses the geometric normal, never the bump normal.
4. The `MaterialParams` C++ struct and `MaterialParams.glsl` must match. Both are 48 bytes.
5. Every material set is complete: all five bindings are written, also for PMX materials.
6. Keep radiance clamped to `kMaxSceneRadiance` (8192). Scene color is `RGBA16F`. Above 65504 it becomes `+inf`, then a black pixel.
7. Release materials only through the sweep on the main thread. Never free a GPU material by hand.

## Image roles
`MaterialImageRole` (`src/Renderer/MaterialImageRole.h`) names the four slots.

| Role | Set 0 binding | GPU format | Channels |
|---|---|---|---|
| `BaseColor` | 0 | `RGBA8_SRGB` | rgb color, a alpha |
| `Normal` | 2 | `RGBA8_UNorm` | rgb = tangent-space normal * 0.5 + 0.5 |
| `MetallicRoughness` | 3 | `RGBA8_UNorm` | G roughness, B metallic |
| `Occlusion` | 4 | `RGBA8_UNorm` | R occlusion (1 = unoccluded) |

- Binding 1 is the `MaterialParams` block: `baseColorFactor`, then alpha cutoff, metallic, roughness, normal scale, occlusion strength.
- `MakeMaterialParams` clamps colors, cutoff, metallic, roughness and occlusion to 0..1, and normal scale to 0..8. A non-finite value becomes its default.
- A missing image becomes a 1x1 default: white sRGB, flat normal (128,128,255), white metallic-roughness, white occlusion.
- Mip chains of data images are plain linear averages. The shader normalizes the sampled normal.

## Tangent convention
The normal map is decoded as `n = rgb * 2 - 1`, then `n.xy *= normalScale`, then `N' = normalize(T * n.x + B * n.y + N * n.z)`.

| Stage | What happens |
|---|---|
| Import | An authored `TANGENT` is moved to engine space (Z is mirrored). The sign w flips for the mirror and for a mirrored node. T is made orthogonal to N. |
| Generation | A primitive without `TANGENT` gets MikkTSpace tangents (`GenerateMeshTangents`). The sign comes from the triangle's own uv gradient. A vertex whose corners need two frames is split. |
| Fallback | `FallbackTangent` when no tangent can be derived. The import message counts them. |
| Storage | Optional tangent tail in the mesh payload: count, then 16 bytes per vertex. The decoder rejects a wrong count, a short block or trailing bytes. |
| Vertex shader | World T is `mat3(model) * T`. The normal uses the cofactor matrix. Both use the sign of `determinant`, so they are exact for any scale and for mirror matrices. |
| Back faces | `side = (gl_FrontFacing ? 1 : -1) * windingSign`. When `side < 0` the whole frame (T, B, N) is negated. |

## Vertex layout and pipelines
`VertexLayout::PositionNormalUvTangent` uses `MeshVertexTangent` (48 bytes). `MeshAssetPipelineKind` picks one pipeline per part.

| Kind | Layout | Shaders |
|---|---|---|
| `Untextured`, `Textured`, `TexturedAlphaTest` | Older layouts | `Mesh`, `TexturedMesh` |
| `Pbr`, `PbrAlphaTest` | With tangent | `PbrMesh.vert` with `PbrMesh.frag` or `PbrMeshAlphaTest.frag` |

- A mesh uses the tangent layout when it is not skinned, has one tangent per vertex, and every textured slice is a glTF material (`CanUseTangentLayout`). Otherwise all its parts use the older layout.
- A mesh without tangents still renders through the older path.
- Sun shadows need a depth-only pipeline per layout. See [Shadow](../reference/render-features/shadow.md).
- Set 0 is the material set. Set 1 is scene services. A pipeline with no material set gets an empty set 0.

## Shading model
Sun plus hemisphere ambient. There is no environment map. Code: `src/Shaders/PbrBrdf.glsl`, `PbrMaterial.glsl`. CPU reference: `src/Math/PbrBrdf.h`.

- `roughness = clamp(perceptualRoughness, 0.045, 1)`, `alpha = roughness^2`, `F0 = mix(0.04, baseColor, metallic)`, `diffuseColor = baseColor * (1 - metallic)`.
- Sun term = `sunIlluminance * NdotL * sunVisibility * horizon`. `horizon` is `saturate(dot(Ng, L) * 8)`.
- Diffuse is scaled by `kDirectScale` (0.65). Specular is `D_GGX * V_SmithGGXCorrelated * F_Schlick`, scaled by `kDirectScale * pi`.
- Ambient = `diffuseColor * HemisphereAmbient(N) * occlusion + HemisphereAmbient(reflect(-V, N)) * EnvBrdfApprox * SpecularOcclusion`.
- Shadows remove only the sun term. Occlusion multiplies ambient only.
- `SceneLighting` writes the eye position for every view. With `sceneLightFollowsAtmosphere` on, sun and ambient are already multiplied by the atmosphere record. The shader does not know the atmosphere.

## Material debug views
`SceneLighting` has `materialDebugView` (0 to 8). The shader writes `SrgbToLinear(value)`. With tone mapping `Clamp`, exposure 1, saturation 1 and dither 0, a capture shows the value exactly. Turn off Atmosphere, Gtao and VolumetricLight first. They change scene color after the mesh pass.

| Value | View | Value | View |
|---|---|---|---|
| 0 | off | 5 | tangent T (xyz * 0.5 + 0.5) |
| 1 | shading normal | 6 | handedness w (green +1, red -1) |
| 2 | roughness | 7 | facing (green front, red back) |
| 3 | metallic | 8 | base color (unlit) |
| 4 | occlusion | | |

A value of 9 or more, a negative value or a non-integer gives `400`. Quick check (details in the [HTTP cheat sheet](../debugging/http-cheat-sheet.md)):

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=SceneLighting&materialDebugView=2"
curl.exe -o view.png "http://127.0.0.1:8080/get_game_view"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=SceneLighting&materialDebugView=0"
```

## Material lifetime
- **Upload.** Many images go up in few submits: one staging buffer and one command buffer per chunk (64 MiB or 256 images). Images are decoded in groups of 48 MiB. One summary line is logged.
- **Sets.** `MaterialSetPool` holds up to 4096 material sets. A `MaterialSetLease` frees its set when the material dies.
- **Cache.** One image per `(Guid, srgb)`. One material per `(four Guids, MaterialParams bytes)`.
- **Sweep.** `Core::BuildFrame` calls `Game::ReleaseUnusedGpuResources` first, on the main thread. When a sweep is pending it runs every 30 frames. Materials no entity and no cached part uses go first, then images only the cache holds, then unused meshes.
- The GPU is drained once, and only if something was freed. The log line is "Released N materials and M images".

## Failure policy
| Failure | Result |
|---|---|
| Base color image missing or undecodable | Default white image. One warning per Guid. |
| Normal, metallic-roughness or occlusion image missing | That slot uses its default image. |
| GPU creation of a material fails | `Resolve()` logs once and returns an invalid handle. The part draws untextured. |
| One upload chunk fails | Every image of that chunk is marked failed. |
| Bad tangent section in a mesh payload | Decode returns `nullopt`. The spawn fails with the existing error. |
| Bad section in the rig blob | Decode returns `nullopt`. The catalog logs once. |
| `TANGENT` accessor missing or wrong type | Tangents are generated for that primitive. |
| MikkTSpace fails or no uv | Every tangent of the primitive is `FallbackTangent`. |
| Draw needs a material set that does not resolve | The draw is skipped. One warning per handle. |
| Material still used | The sweep keeps it. |

## Checks
- `tests/Renderer/MaterialParamsTests.cpp` parses the GLSL block and compares it with the C++ struct.
- `tests/Features/SceneLighting/` covers the debug view range.
- Shader math has a CPU reference in `src/Math/PbrBrdf.h` with tests.

## Not supported
- Emissive. The slot is imported, never sampled.
- `KHR_materials_*` and `KHR_texture_transform`.
- Texture coordinate sets other than `TEXCOORD_0`. The importer reports them in its message.
- glTF sampler modes. All images use Repeat and linear filtering.
- Image-based lighting and alpha blending.
- GPU skinning of tangent meshes.

## Related
- [Asset pipeline](../architecture/asset-pipeline.md) · [Rendering](../architecture/rendering.md) · [Scene lighting](../reference/render-features/scene-lighting.md)
- [Frame debugger](../debugging/frame-debugger.md) (bound images of one draw) · [Animation and physics](../architecture/animation-physics.md)
