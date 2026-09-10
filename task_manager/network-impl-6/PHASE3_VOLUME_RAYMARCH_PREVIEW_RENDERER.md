# PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER

Parent: `PHASE0_MASTER_STRATEGY.md` — **READ THAT FILE FIRST.**
Previous: `PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md` — read its own
`PHASE2_COMPLETION_REPORT.md` before starting.

**This is the heaviest, highest-risk phase in the whole campaign** — it is
the only phase that writes new GPU shader code and a new, self-contained
Vulkan compute driver class from scratch. Take the time this phase's own
detail below asks for; do not compress steps.

> **v2 Revision Notes (quality double-check pass, cross-checked directly
> against the real source tree):** an earlier revision of this document
> contained several concrete inaccuracies that would have produced either a
> silent no-op bug or a build-breaking wrong path if followed literally.
> Fixed in this revision:
> 1. **`src/Editor/ComputeBlurValidation.h/.cpp` was wrongly cited as the
>    precedent for "driven outside the RenderGraph entirely, read back via
>    `Renderer::CaptureImagePixels()`."** It is neither: `ComputeBlurValidation`
>    is a real, every-frame `gte::rg::RenderGraphBuilder::AddComputePass()`
>    pass (fully driven BY the render graph, with automatic barrier synthesis),
>    and it never reads pixels back to the CPU at all (its output stays a
>    `RenderTexture` sampled directly by Dear ImGui). `src/Editor/GpuSkinningValidation.cpp`'s
>    `ValidateGpuSkinningAgainstCpuOracle()` is the actually-matching precedent
>    for the dispatch/barrier shape this phase needs — see the corrected Step 2
>    and 3.4 below.
> 2. **`renderer.Dispatch()`/"`Renderer::DispatchCompute()`" must NEVER be
>    called from this class.** `Renderer::Dispatch()` (the real method name —
>    `DispatchCompute()` does not exist anywhere in this codebase) asserts (debug)
>    and silently no-ops (release) when called outside an active
>    `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket — see
>    `Renderer.cpp`. Since this class's dispatch runs inside a plain
>    `Renderer::ImmediateSubmit()` callback (never a render-graph pass), it MUST
>    issue raw `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/
>    `vkCmdDispatch` calls directly, exactly mirroring `GpuSkinningValidation.cpp`'s
>    own dispatch block. This was the single most important fix in this revision.
> 3. **`Vec3` has no member `.Length()` method.** `Length()`/`Dot()`/`Cross()`/
>    `Normalize()` are all free functions taking `const Vec3&` (`src/Math/Vec3.h`)
>    — every `boxHalfExtents.Length()` reference below is corrected to
>    `Length(boxHalfExtents)`.
> 4. **Wrong shader source directory.** This engine's real GLSL source folder
>    is `src/Shaders/` (see `cmake/CompileShaders.cmake`'s own doc comment and
>    every existing `gte_add_shader(GreatTamanaEngine src/Shaders/....comp)`
>    call in `CMakeLists.txt`) — there is no top-level `Shaders/` directory.
>    The new file is `src/Shaders/VolumeTexturePreview.comp`, not
>    `Shaders/VolumeTexturePreview.comp`. (The RUNTIME staged SPIR-V path used
>    at `Renderer::CreateComputePipeline()` call sites, `"shaders/....comp.spv"`
>    — lowercase, no `src/` prefix — was already correct in the original
>    revision and is unchanged.)
> 5. **The output texture's restore-to-`ComputeShaderWrite` `ResourceState` should
>    reuse `rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false)`**
>    (`VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT`) rather than a hand-rolled
>    `VK_ACCESS_2_SHADER_WRITE_BIT` literal, matching every other precedent that
>    performs this exact transition (`ComputeBlurValidation::FinalizeForSampling()`,
>    `AtmosphereLutRenderer::FinalizeAerialPerspectiveCompositeForSampling()`).
> 6. **`m_descriptorSet`'s declared type was inconsistent** between the class
>    declaration (a raw `VkDescriptorSet`) and the prose (which assumed a
>    `ComputeDescriptorSet::Rewrite()` call) — now consistently
>    `ComputeDescriptorSet`, matching `ComputeBlurValidation`'s own member shape.
> 7. Added an explicit, concrete way to smoke-test this phase manually (Phase
>    1/2, already landed by the time this phase starts, expose exactly the
>    accessor needed — `RenderGraph::DebugVolumeTextureSnapshotFor()`).
>
> Everything else in this document (the math, the raymarch algorithm, the
> push-constant packing convention, the binding convention, the overall class
> shape) was cross-checked against the real source and found accurate — it is
> unchanged below except where called out above.

## Step 1 — The Goal

Build a fully self-contained, reusable renderer —
`gte::VolumeTexturePreviewRenderer` — that, given nothing but a live 3D
Vulkan image (a `VolumeTarget`: image/imageView/extent/format) and its
current `rg::ResourceState`, produces a single 256x256 RGBA8 raymarched
"Volume mode" thumbnail (Unity-Texture3D-inspector style: front-to-back
alpha-composited raymarch, fixed default camera angle, dark-gray
background) as plain CPU-side pixels, ready to be PNG-encoded by the
EXISTING `Encoding::EncodeRgba8ToPng()` helper. By the end of this phase,
the class compiles, is unit-testable at its pure-math core, and can be
exercised manually (e.g. a throwaway call from a debug menu or a scratch
`main.cpp` flag) — but it is **not yet wired to the network endpoint**
(that is Phase 4's job). This class lives in core `src/Renderer/` (not
`src/Editor/`), per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 4,
and is driven by a single `Renderer::ImmediateSubmit()` call with hand-rolled
Vulkan barriers/dispatch (see Step 2/3.4 below) — it never touches
`gte::rg::RenderGraph` at all.

## Step 2 — The Situation / The Problem

- Nothing in this engine renders anything by sampling a 3D image outside
  the Atmosphere Scattering campaign's own compute shaders. Read
  `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` (specifically its
  `sampler3D aerialPerspectiveVolume` binding and its `texture(...)` call)
  for the one existing example of GLSL code that samples a
  `VK_IMAGE_TYPE_3D` image in this codebase — this phase's own shader will
  look structurally similar for the sampling call itself, just built
  around a raymarch loop instead of a single lookup.
- The engine's compute-pipeline plumbing (`Renderer::CreateComputePipeline()`,
  `Renderer::AllocateComputeDescriptorSet()`, `ComputeDescriptorSet`
  (`src/Renderer/ComputeDescriptorSet.h/.cpp`), `Vulkan/DescriptorSetLayoutBuilder.h`)
  is already generic and battle-tested (GPU Vertex Skinning, Atmosphere
  Scattering both build on it) — this phase does not need to invent any
  new Renderer-level primitive, only USE the existing ones.
- **Read `src/Editor/GpuSkinningValidation.h/.cpp` (specifically
  `ValidateGpuSkinningAgainstCpuOracle()`) in full before writing this
  phase's own class — this is the actual, correct existing precedent** for
  "a small, self-contained, ON-DEMAND (never per-frame) compute dispatch,
  driven entirely by ONE `Renderer::ImmediateSubmit()` call, that issues its
  own raw `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/
  `vkCmdDispatch` calls directly against that command buffer (never
  `Renderer::Dispatch()` — see below), with a hand-written pipeline barrier
  between the dispatch and the readback copy, all inside that SAME command
  buffer." Note `GpuSkinningValidation` reads its result back via a mapped
  `Buffer` (its output is a `StructuredBuffer`, not an image) — this phase's
  own readback is an IMAGE, so use `Renderer::CaptureImagePixels()` for that
  half instead (see below), everything else about the dispatch/barrier shape
  transfers directly.
  - **`src/Editor/ComputeBlurValidation.h/.cpp` is NOT a matching precedent
    for this shape, despite looking superficially similar at a glance — do
    not model this class on it for either of the following two properties.**
    `ComputeBlurValidation::AddPass()` is a real, per-frame
    `gte::rg::RenderGraphBuilder::AddComputePass()` pass: its own `execute`
    lambda calls `renderer.Dispatch(...)` *inside* a
    `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket, and every
    barrier around it is synthesized automatically by the render graph's own
    compiler — this is the exact OPPOSITE of "driven outside the RenderGraph
    entirely." It also never reads any pixels back to the CPU at all — its
    output stays a `RenderTexture`, sampled directly by Dear ImGui via
    `ImGui::Image()` — so "read back via `Renderer::CaptureImagePixels()`-style
    plumbing" was never an accurate description of it either.
  - **`renderer.Dispatch()` must never be called by this class, full stop.**
    `Renderer::Dispatch()` (`Renderer.h`/`.cpp`) is deliberately gated to
    render-graph-pass recording only: it `assert()`s in a debug build, and
    silently does nothing at all in a release build, whenever
    `BeginGraphPassRecording()` has not been called on the SAME `Renderer`
    first (see `Renderer::Dispatch()`'s own doc comment and its
    `m_currentGraphPassCmd != VK_NULL_HANDLE` assert in `Renderer.cpp`). This
    class's dispatch happens inside a plain `Renderer::ImmediateSubmit()`
    callback, which never sets that state — so this class MUST issue its own
    raw `vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...)`/
    `vkCmdBindDescriptorSets(...)`/`vkCmdPushConstants(...)`/`vkCmdDispatch(...)`
    calls, exactly like `GpuSkinningValidation.cpp` does for its own
    self-contained dispatch. `ComputeDispatch.h`'s `ComputeGroupCount()`
    helper (ceiling-division work-group math) is still exactly the right
    thing to reuse for computing the `vkCmdDispatch` group counts — only the
    actual dispatch CALL must be a raw Vulkan one, never `Renderer::Dispatch()`.
  - For the CPU-readback half of an already-computed 2D IMAGE with a known
    `rg::ResourceState` specifically, `src/Editor/AtmosphereTransmittanceLutValidation.cpp`'s
    `ValidateAtmosphereTransmittanceLut()` is the closest existing precedent
    (it calls `Renderer::CaptureImagePixels()` against a texture with a
    documented, assumed current `rg::ResourceState`) — though note it reads
    back a texture the PERMANENT, already-running Atmosphere pass sequence
    wrote on an EARLIER, already-GPU-completed frame, whereas this phase's
    renderer captures an image it JUST wrote itself, moments earlier, inside
    the SAME `RenderPreview()` call — so this class tracks its own image's
    real current state explicitly in a member (`m_outputTextureState`, see
    3.4 below) rather than assuming a graph-wide invariant.
- There is no existing "fixed default camera auto-framed to a box" helper
  anywhere in this engine (`AssetPreviewMesh`'s own "auto-framed to the
  mesh's own bounding SPHERE" is the closest analog, but that is a
  different shape/formula — do not copy it verbatim, it is for a sphere,
  this phase needs a BOX).
- `Renderer::CreateTexture2D()` (`Renderer.h`, `allowStorageImageAccess`
  parameter) requires initial CPU-side pixel data at construction time — it
  has no "just allocate empty, uninitialized" overload. A zero-filled
  `std::vector<std::uint8_t>` of the right size, passed once at this
  renderer's lazy-init time, is the correct, simplest way to satisfy this
  (the very first compute dispatch overwrites every pixel anyway via
  `imageStore`, so the initial content is irrelevant and only needs to be
  a valid, zeroed buffer of the right byte count). Confirmed directly from
  `Texture2D.h`'s own doc comment: a freshly-created `Texture2D` is left in
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` once `GpuResourceFactory::CreateTexture2D()`
  finishes uploading into it — this is the correct initial value for this
  class's own `m_outputTextureState` member (see 3.4).

## Step 3 — The Plan

### 3.1 — New file: `src/Renderer/VolumeTexturePreviewMath.h` + `.cpp`

The **pure, CPU-only, Tier-1-testable "oracle"** for this feature's camera/
box setup — mirrors `src/Renderer/Atmosphere/AtmosphereMath.h`'s own role
exactly (see `AGENTS.md`, "Atmosphere Scattering": "every `.comp` shader is
a faithful GLSL transcription of this file's own math... if they ever
disagree, the CPU oracle is right by definition"). This file has **zero**
Vulkan dependency at all (no `<volk.h>`, no `VkFormat`/`VkImage` anywhere)
— just plain floats/`gte::Vec3`/`gte::Mat4`.

```cpp
#pragma once

#include "../Math/Vec3.h"

namespace gte {

// Deterministic, fixed default camera + proxy-box setup for the Volume
// Texture Preview feature (network-impl-6 campaign) - given ONLY a
// volume's own texel dimensions (no other input, no query parameters -
// see PHASE0_MASTER_STRATEGY.md's Locked Design Decision 3/7), computes
// everything Shaders/VolumeTexturePreview.comp needs to build camera rays
// and the proxy box's local-space extents.
struct VolumeCameraSetup {
    Vec3 eyePosition;      // World-space (== the box's own local space; no separate world transform exists for this feature).
    Vec3 forward;          // Normalized, points from eye toward the box's center.
    Vec3 right;             // Normalized, camera-local +X.
    Vec3 up;                // Normalized, camera-local +Y.
    float tanHalfFovY = 0.0f; // Vertical half-field-of-view tangent, for ray direction construction.
    Vec3 boxHalfExtents;    // The raymarch proxy box's own local-space half-extents (box is centered at the ORIGIN, i.e. spans [-boxHalfExtents, +boxHalfExtents] - NOT [0,1] - see ComputeVolumeCameraSetup()'s own doc comment for why).
};

// boxHalfExtents = 0.5 * (width, height, depth) / max(width, height, depth)
// - i.e. the LONGEST texel dimension always maps to a half-extent of
// exactly 0.5 (a full extent of 1.0), and the other two are scaled down
// proportionally - see PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision 7. `width`/`height`/`depth` must all be > 0.
//
// The camera is placed at a FIXED yaw/pitch (an isometric-like angle:
// 45 degrees azimuth, ~35.264 degrees elevation - atan(1/sqrt(2)), the
// same "true isometric" angle many 3D modeling tools default an
// orbit-camera icon view to) around the box's center (the origin), at a
// distance computed to fit the box's own bounding sphere (radius =
// Length(boxHalfExtents)) inside a fixed, generous field of view (45
// degrees vertical) with a small margin - deterministic and depends only
// on `width`/`height`/`depth`.
VolumeCameraSetup ComputeVolumeCameraSetup(int width, int height, int depth);

// Ray-box intersection against a box centered at the origin with the
// given half-extents (Vec3, one entry per axis) - a straight C++ port of
// the how-to reference material's own `RayBox()` HLSL function (see
// task_manager/network-impl-6/how-to-instruction.txt), generalized from a
// unit [0,1] box to an arbitrary-half-extent, origin-centered box. Returns
// false (tEnter/tExit left untouched) if the ray never enters the box, or
// only enters it entirely behind the ray's origin.
bool IntersectRayBox(const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& boxHalfExtents,
    float& outTEnter, float& outTExit);

} // namespace gte
```

Implementation notes for `.cpp`:

- Compute `right`/`up`/`forward` via a standard look-at-style orthonormal
  basis: `forward = Normalize(boxCenter - eyePosition)` (boxCenter is the
  origin, so `forward = Normalize(-eyePosition)`), `right =
  Normalize(Cross(worldUp, forward))` (`worldUp = Vec3(0, 1, 0)`), `up =
  Cross(forward, right)` — this is the exact same left-handed convention
  `Mat4::LookAtLH` already uses elsewhere in this engine
  (`src/Math/Mat4.cpp`'s real implementation: `zaxis(forward) =
  Normalize(target - eye)`, `xaxis(right) = Normalize(Cross(up, zaxis))`,
  `yaxis(up) = Cross(zaxis, xaxis)` — confirmed byte-for-byte identical to
  the formula above) — reuse `Mat4::LookAtLH`'s own internal
  formula/handedness rather than inventing a different one, so this
  feature's camera convention is consistent with the rest of the engine
  even though it never actually builds a `Mat4` (the shader wants raw basis
  vectors for per-pixel ray construction, not a view/projection matrix to
  multiply — simpler and cheaper for a compute shader that isn't
  rasterizing any real geometry).
- All of `Dot()`/`Cross()`/`Normalize()`/`Length()`/`LengthSquared()` are
  plain FREE functions taking a `const Vec3&` (`src/Math/Vec3.h`) — `Vec3`
  itself has none of these as member methods. Write `Length(boxHalfExtents)`,
  never `boxHalfExtents.Length()` (the latter does not compile).
- `IntersectRayBox()` is a direct transcription of the how-to reference
  material's slab method, adjusted for an origin-centered box: `boxMin =
  -boxHalfExtents`, `boxMax = +boxHalfExtents`.
- Guard against `rayDirection`'s components being exactly `0.0f` the same
  way the reference GLSL does (relying on IEEE-754 `1.0/0.0 == +inf`,
  `-1.0/0.0 == -inf`, both of which behave correctly in the existing
  `min`/`max` slab logic) — do not add a defensive epsilon that would
  silently change the intersection result; C++ float division by zero is
  well-defined on this project's actual toolchain (MSVC/GCC, default IEEE-754
  floating point mode) — produces `±inf`, not UB, as long as it's not `0.0/0.0`
  — exactly like GLSL's.

### 3.2 — New test file: `tests/Renderer/VolumeTexturePreviewMathTests.cpp`

Tier-1, no Vulkan/VkDevice needed at all. Cover:

- `ComputeVolumeCameraSetup()` for a perfect cube (e.g. `128x128x128`)
  produces `boxHalfExtents == Vec3(0.5, 0.5, 0.5)` exactly.
- `ComputeVolumeCameraSetup()` for a non-cubic volume (e.g. the real
  Atmosphere shape, `128x128x32`) produces a `boxHalfExtents` whose
  longest axis is exactly `0.5` and whose other two axes are scaled down
  by the correct ratio (`32/128 = 0.25` -> `0.125` half-extent on that
  axis) — this is the single most important regression this test file
  guards, since it is this campaign's own Locked Design Decision 7.
- `right`/`up`/`forward` are mutually orthogonal (dot products ~0 within a
  small epsilon) and each unit length.
- `eyePosition`'s distance from the origin (`Length(setup.eyePosition)`) is
  strictly greater than `Length(setup.boxHalfExtents)` (the camera must sit
  fully outside the box, never inside/touching it).
- **The real invariant that matters**: cast a ray from `eyePosition`
  toward EACH of the box's 8 corners (`±boxHalfExtents.x, ±boxHalfExtents.y,
  ±boxHalfExtents.z`) and confirm `IntersectRayBox()` reports a valid hit
  for every one of them (`tExit >= tEnter >= 0`) — this proves the whole
  box is actually within view/reachable by SOME ray the camera could cast,
  catching a camera-placement bug (e.g. sitting too close, or facing the
  wrong way) that would otherwise only be visible by eyeballing a rendered
  PNG.
- `IntersectRayBox()` on its own, independent of the camera setup: a ray
  starting outside the box and pointed directly at its center hits it
  (`tEnter > 0`); a ray starting INSIDE the box has `tEnter <= 0 <= tExit`;
  a ray that misses the box entirely (e.g. parallel to one face, offset
  outside its extent) returns `false`.

Add this new file to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` — always
built, no conditional gating (matches Locked Design Decision 4).

### 3.3 — New shader: `src/Shaders/VolumeTexturePreview.comp`

**Real file location note**: this engine's GLSL sources live under
`src/Shaders/` (see `cmake/CompileShaders.cmake`'s own doc comment and every
existing `gte_add_shader(GreatTamanaEngine src/Shaders/....comp)` call in
`CMakeLists.txt` — there is no top-level `Shaders/` directory in this repo).
`AGENTS.md`/`README.md` refer to shader files with an informal `Shaders/X.comp`
shorthand in prose, but the actual path on disk — and the path this new file
must be created at and registered with — is `src/Shaders/VolumeTexturePreview.comp`.
The RUNTIME staged `.spv` path used at `Renderer::CreateComputePipeline()` call
sites (`"shaders/VolumeTexturePreview.comp.spv"` — lowercase `shaders/`, no
`src/` prefix, copied there by `gte_add_shader()`'s own POST_BUILD step next to
the built `.exe`) is a SEPARATE path and is correct as shown in 3.4 below —
don't confuse the two.

A single compute shader, one invocation per output pixel (local size e.g.
`16x16x1`, matching `src/Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp`'s
own convention — check that file for the exact local-size-and-dispatch
idiom this codebase already uses and mirror it). Bindings (descriptor set
0):

- `binding = 0`: `uniform sampler3D volumeTex` — the live volume texture,
  bound via a combined image sampler (the C++ driver creates and owns the
  `VkSampler`, trilinear, clamp-to-edge in all 3 axes — mirroring
  `VolumeTexture`'s own sampler description exactly, see `VolumeTexture.h`'s
  class comment — `VolumeTarget` itself, unlike `VolumeTexture`, carries no
  sampler of its own, so this class must build its own; do not assume one
  is available on `VolumeTarget`).
- `binding = 1`: `uniform writeonly image2D outputImage` (format
  `rgba8`) — the persistent 256x256 output.

This binding order (read-only `Texture`/`sampler3D` first, `RWTexture`/
storage-image output last) matches `Vulkan/DescriptorSetLayoutBuilder.h`'s
own documented binding-number convention, and is the exact same order
`AtmosphereAerialPerspectiveVolumeDebugSlice.comp`'s own binding 0/1 use for
an almost-identical "sample a volume, write a 2D image" shape.

Push constants (must exactly match the C++ struct the driver class builds
— see 3.4 below):

```glsl
layout(push_constant) uniform PushConstants {
    vec3 eyePosition;
    float tanHalfFovY;
    vec3 forward;
    float densityScale;
    vec3 right;
    int stepCount;
    vec3 up;
    float _padding0;
    vec3 boxHalfExtents;
    float _padding1;
} pc;
```

(Lay this out so every `vec3` starts at a 16-byte-aligned offset,
std140-style, matching this codebase's own existing push-constant
discipline elsewhere — confirmed directly against
`AtmosphereAerialPerspectiveComposite.comp`'s own push-constant block
(`vec4 cameraWorldPositionAndScale` packing a `vec3` position + a scalar
`.w` into one 16-byte-aligned `vec4` group) and its matching C++
`AerialPerspectiveCompositePushConstants` struct in `AtmosphereLutRenderer.cpp`
— follow that exact convention rather than guessing at packing rules
independently.)

Body, transcribing the how-to reference material's own recommended
algorithm (`task_manager/network-impl-6/how-to-instruction.txt`) almost
verbatim, adapted to an origin-centered box and a fixed analytic camera
instead of a `viewProj` matrix:

```glsl
ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
ivec2 outSize = imageSize(outputImage);
if (pixel.x >= outSize.x || pixel.y >= outSize.y) {
    return;
}

vec2 ndc = (vec2(pixel) + 0.5) / vec2(outSize) * 2.0 - 1.0; // [-1, 1], y NOT flipped since this is a compute image write, not a rasterized clip-space position.
float aspect = float(outSize.x) / float(outSize.y);

vec3 rayDir = normalize(
    pc.forward
    + pc.right * (ndc.x * pc.tanHalfFovY * aspect)
    + pc.up    * (-ndc.y * pc.tanHalfFovY)); // -ndc.y so pixel row 0 (top) maps to +up, matching normal image-row-0-is-top convention.

float tEnter, tExit;
if (!IntersectRayBox(pc.eyePosition, rayDir, pc.boxHalfExtents, tEnter, tExit)) {
    imageStore(outputImage, pixel, vec4(kBackgroundColor, 1.0));
    return;
}
tEnter = max(tEnter, 0.0);

float rayLength = tExit - tEnter;
float stepLength = rayLength / float(pc.stepCount);
float t = tEnter + 0.5 * stepLength;

vec4 accum = vec4(0.0);
for (int i = 0; i < pc.stepCount; ++i) {
    vec3 localPos = pc.eyePosition + rayDir * t;               // Box-local space, box spans [-boxHalfExtents, +boxHalfExtents].
    vec3 uvw = (localPos / pc.boxHalfExtents) * 0.5 + 0.5;      // -> [0, 1]^3 texture space.

    vec4 s = texture(volumeTex, uvw);
    float density = s.a;
    vec3 color = s.rgb;
    float alpha = 1.0 - exp(-density * pc.densityScale * stepLength);

    accum.rgb += (1.0 - accum.a) * color * alpha;
    accum.a   += (1.0 - accum.a) * alpha;

    if (accum.a > 0.995) {
        break;
    }
    t += stepLength;
}

vec3 finalColor = accum.rgb + (1.0 - accum.a) * kBackgroundColor;
imageStore(outputImage, pixel, vec4(finalColor, 1.0));
```

Where `kBackgroundColor` is a small file-local `const vec3` — a dark gray,
e.g. `vec3(0.2, 0.2, 0.2)` (the how-to reference material's own "dark gray
background, so white smoke is readable" recommendation) — and
`IntersectRayBox()` is a GLSL transcription of 3.1's own C++
`IntersectRayBox()` (same slab-method formula, `boxMin = -pc.boxHalfExtents`,
`boxMax = +pc.boxHalfExtents`) written directly into this shader file (GLSL
has no way to share a header with C++ — this is an ACCEPTED, DOCUMENTED
duplication, exactly like `AtmosphereMath.h`'s own relationship to its
GLSL shaders per `AGENTS.md`'s "Atmosphere Scattering" rule: the CPU
version is the oracle, this GLSL version must be checked BY HAND against
it, and if they ever disagree the GLSL is what needs fixing).

Register the new shader in `CMakeLists.txt`'s existing `gte_add_shader()`
list (find the exact line registering
`src/Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp` and add a
sibling line right after it):

```cmake
gte_add_shader(GreatTamanaEngine src/Shaders/VolumeTexturePreview.comp)
```

This shader is ALWAYS compiled (no `GTE_ENABLE_EDITOR` guard), matching
Locked Design Decision 4.

### 3.4 — New file: `src/Renderer/VolumeTexturePreviewRenderer.h` + `.cpp`

```cpp
#pragma once

#include "ComputeDescriptorSet.h"
#include "ComputePipeline.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h" // rg::ResourceState
#include "Texture2D.h"
#include "VolumeTarget.h"

#include <optional>
#include <volk.h>

namespace gte {

class Renderer;

// network-impl-6 campaign, Phase 3. A small, self-contained, ON-DEMAND
// (never per-frame) GPU compute renderer that raymarches an arbitrary live
// VolumeTexture into a fixed-size 2D RGBA8 thumbnail - the "Volume mode"
// preview Unity's own Texture3D inspector uses, built for an LLM/AI agent
// fetching it over HTTP (GET /get_texture, see network-impl-6's
// PHASE0_MASTER_STRATEGY.md) rather than for a human-facing Editor panel.
//
// Driven ENTIRELY by ONE Renderer::ImmediateSubmit() call per RenderPreview()
// (plus a second, internal one inside Renderer::CaptureImagePixels() for the
// readback) - mirrors GpuSkinningValidation.cpp's own "self-contained,
// hand-rolled dispatch + barrier, no gte::rg::RenderGraph dependency" shape,
// NOT ComputeBlurValidation's (that class is a real, per-frame RenderGraph
// compute PASS - see this phase's own Step 2 for the full correction). This
// class NEVER calls Renderer::Dispatch() (gated to render-graph-pass
// recording only - see Step 2) - it issues its own raw vkCmdBindPipeline/
// vkCmdBindDescriptorSets/vkCmdPushConstants/vkCmdDispatch calls instead.
// Lives in core src/Renderer/ (NOT src/Editor/) since it must compile and
// work with GTE_ENABLE_EDITOR=OFF (this feature follows GET /get_texture's
// own gating exactly - see PHASE0's Locked Design Decision 4).
class VolumeTexturePreviewRenderer {
public:
    VolumeTexturePreviewRenderer() = default;

    // Not copyable/movable - owns live Vulkan objects with no move-
    // plumbing written for them yet (this class is a singleton-per-
    // Renderer-lifetime member, exactly like AtmosphereLutRenderer -
    // never needs to be copied or moved).
    VolumeTexturePreviewRenderer(const VolumeTexturePreviewRenderer&) = delete;
    VolumeTexturePreviewRenderer& operator=(const VolumeTexturePreviewRenderer&) = delete;

    ~VolumeTexturePreviewRenderer();

    struct CapturedRawPixels {
        std::vector<std::uint8_t> pixels; // Tightly packed width*height*4 RGBA8 bytes.
        int width = 0;
        int height = 0;
    };

    // Synchronously renders ONE fixed-camera "Volume mode" raymarch
    // thumbnail of `volume` (its CURRENT contents, at `previousState`'s
    // real, caller-supplied current ResourceState) and reads it back to
    // the CPU. Blocking (uses Renderer::ImmediateSubmit() internally,
    // more than once) - acceptable ONLY because this is invoked at most
    // once per network request, exactly like Renderer::CaptureImagePixels()
    // itself (see that method's own doc comment). `volume`'s own image
    // layout is restored to `previousState` before this method returns,
    // exactly mirroring CaptureImagePixels()'s existing "restore
    // afterward" discipline (a later graph-recorded frame touching the
    // SAME volume texture must see it in the state it expects).
    CapturedRawPixels RenderPreview(Renderer& renderer, const VolumeTarget& volume, const rg::ResourceState& previousState);

private:
    void EnsureInitialized(Renderer& renderer);

    static constexpr int kOutputWidth = 256;
    static constexpr int kOutputHeight = 256;
    static constexpr int kStepCount = 64;
    static constexpr float kDensityScale = 4.0f; // Tunable - see how-to reference material's own "2-12, tune per asset" note; a single fixed default is enough for this campaign's generic debug-visualization goal (Locked Design Decision 9).

    // MUST match src/Shaders/VolumeTexturePreview.comp's own
    // `layout(local_size_x = 16, local_size_y = 16) in;` exactly.
    static constexpr std::uint32_t kLocalSizeX = 16;
    static constexpr std::uint32_t kLocalSizeY = 16;

    bool m_initialized = false;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_volumeSampler = VK_NULL_HANDLE; // Trilinear, clamp-to-edge x3 - created once, owned by this class.
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_pipeline;
    // A ComputeDescriptorSet (NOT a raw VkDescriptorSet) so this class can
    // call its own .Rewrite() directly - matches ComputeBlurValidation's
    // identical member shape. Allocated once; Rewrite()'s binding-update
    // happens EVERY call (unlike GpuSkinningRigCache's "once per model" -
    // this class serves a DIFFERENT volume on every call, so binding 0 must
    // be refreshed every time - see .cpp). Binding 1 (the output image) never
    // actually changes across calls, but is simplest to rewrite alongside
    // binding 0 in the same Rewrite() call every time.
    ComputeDescriptorSet m_descriptorSet;
    std::optional<Texture2D> m_outputTexture; // Persistent, created once, reused/overwritten across every call (Locked Design Decision 8).
    // Tracks m_outputTexture's REAL current ResourceState across calls -
    // initialized to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL in
    // EnsureInitialized() (the real, confirmed post-construction layout
    // Texture2D.h's own doc comment documents), then GENERAL after every
    // RenderPreview() call (see .cpp).
    rg::ResourceState m_outputTextureState;
};

} // namespace gte
```

`.cpp` implementation outline:

1. `EnsureInitialized()` (called at the top of every `RenderPreview()`
   call, mirroring `AtmosphereLutRenderer::EnsureAerialPerspectiveVolumeInitialized()`'s
   own "lazy, once" pattern): build the trilinear/clamp sampler directly
   via `vkCreateSampler` (this class owns raw Vulkan objects the same way
   `AtmosphereLutRenderer` does — it is not itself a `RenderTexture`/
   `Buffer` wrapper, so there is no `GpuMemoryTracker` registration
   expected for the sampler/descriptor-set-layout specifically, matching
   `ComputeBlurValidation`'s/`AtmosphereLutRenderer`'s own precedent of
   owning a few small raw Vulkan objects directly — `m_outputTexture`
   itself, being a real `Texture2D`, DOES register with `GpuMemoryTracker`
   automatically via its own constructor, same as any other GPU resource);
   build the descriptor set layout (binding 0 = combined image sampler,
   binding 1 = storage image) via `Vulkan/DescriptorSetLayoutBuilder.h`;
   build a `VkPushConstantRange` explicitly:

   ```cpp
   VkPushConstantRange pushConstantRange{};
   pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
   pushConstantRange.offset = 0;
   pushConstantRange.size = sizeof(PushConstants); // the C++ mirror of 3.3's GLSL PushConstants block - same field order/padding.
   ```

   then create the compute pipeline via
   `renderer.CreateComputePipeline("shaders/VolumeTexturePreview.comp.spv",
   {m_descriptorSetLayout}, pushConstantRange)` (note: this is the RUNTIME
   staged path, lowercase `shaders/` — see 3.3's own note on this vs. the
   `src/Shaders/` GLSL source path); allocate the descriptor set via
   `renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout)` and wrap it
   in `m_descriptorSet = ComputeDescriptorSet(rawSet)`; create
   `m_outputTexture` via `renderer.CreateTexture2D(zeroFilledPixels.data(),
   kOutputWidth, kOutputHeight, "VolumeTexturePreviewOutput",
   /*allowStorageImageAccess=*/true)` (see Step 2's own note on why a
   zero-filled initial buffer is correct and sufficient); initialize
   `m_outputTextureState` to
   `rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false)` (the real,
   documented post-construction state of a freshly-created `Texture2D` — see
   `Texture2D.h`'s own doc comment).
2. `RenderPreview()`:
   a. `EnsureInitialized(renderer)`.
   b. Compute `VolumeCameraSetup` via
      `ComputeVolumeCameraSetup(static_cast<int>(volume.extent.width),
      static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth))`
      (Phase 3.1).
   c. Rewrite `m_descriptorSet`'s binding 0 to point at `volume.imageView`
      + `m_volumeSampler` (every call — this class serves a DIFFERENT
      volume image on every call, unlike a per-model cache elsewhere in
      this engine that only rewrites once) and binding 1 to
      `m_outputTexture->View()` (this binding never actually changes
      across calls since the SAME output texture is reused every time, but
      it costs nothing extra to rewrite both bindings together in the same
      `m_descriptorSet.Rewrite(m_device, {...})` call every time — simpler
      than special-casing binding 1 to only be written once):
      ```cpp
      m_descriptorSet.Rewrite(m_device,
          std::vector<ComputeDescriptorWrite>{
              ComputeDescriptorWrite::CombinedImageSampler(0, volume.imageView, m_volumeSampler),
              ComputeDescriptorWrite::StorageImage(1, m_outputTexture->View()),
          });
      ```
   d. Build this call's own `PushConstants` value (`eyePosition`/`forward`/
      `right`/`up`/`tanHalfFovY`/`boxHalfExtents` from the `VolumeCameraSetup`
      computed in (b), `densityScale = kDensityScale`, `stepCount = kStepCount`
      — matching 3.3's exact GLSL field order/padding).
   e. `renderer.ImmediateSubmit([&](VkCommandBuffer cmd) { ... })`:
      - Emit a manual image barrier transitioning `volume.image` from
        `previousState` to a state usable by a compute shader's combined
        image sampler read. **This exact state has no existing
        `rg::ResourceAccess` enumerator to reuse via `RequiredStateFor()`** —
        `ResourceAccess::ShaderRead` maps to `VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT`
        (a fragment-shader sampling case, see `RenderGraphBarrierPlanner.cpp`),
        and `ResourceAccess::ComputeShaderRead` maps to `VK_IMAGE_LAYOUT_GENERAL`
        (a storage-image/`imageLoad` case, not a sampler read) — neither is
        correct for "a COMBINED IMAGE SAMPLER read from a COMPUTE shader," so
        build this `rg::ResourceState` by hand instead:
        ```cpp
        const rg::ResourceState volumeSampledState{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        };
        rg::EmitImageBarrier(cmd, volume.image, /*range=*/{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, previousState, volumeSampledState);
        ```
        (`rg::EmitImageBarrier()`, `RenderGraphBarrierPlanner.h`, builds the
        `VkImageMemoryBarrier2` AND issues the `vkCmdPipelineBarrier2` call in
        one step — prefer it directly over calling `BuildImageMemoryBarrier2()`
        and a manual `vkCmdPipelineBarrier2` by hand, since `EmitImageBarrier()`
        already does exactly that.)
      - Emit a manual image barrier transitioning `m_outputTexture`'s image
        from `m_outputTextureState` (the real, tracked state from either
        `EnsureInitialized()` or the previous `RenderPreview()` call) to
        `VK_IMAGE_LAYOUT_GENERAL` (a storage image write target):
        ```cpp
        const rg::ResourceState outputWriteState = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
        rg::EmitImageBarrier(cmd, m_outputTexture->Image(), /*range=*/{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, m_outputTextureState, outputWriteState);
        ```
      - Bind the pipeline/descriptor set/push constants and dispatch via
        RAW Vulkan calls — never `renderer.Dispatch()` (see Step 2's own
        correction: that method asserts/no-ops outside a render-graph pass
        recording bracket, which this is not):
        ```cpp
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->Native());
        const VkDescriptorSet rawSet = m_descriptorSet.Native();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->Layout(), 0, 1, &rawSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_pipeline->Layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
        const std::uint32_t groupCountX = ComputeGroupCount(kOutputWidth, kLocalSizeX);
        const std::uint32_t groupCountY = ComputeGroupCount(kOutputHeight, kLocalSizeY);
        vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
        ```
        (`ComputeGroupCount()`, `src/Renderer/ComputeDispatch.h` — reuse it for
        the ceiling-division math exactly as every other compute pass in this
        engine does; never plain integer division.)
      - Emit a manual image barrier transitioning `volume.image` back to
        `previousState` (restoring it, mirroring
        `Renderer::CaptureImagePixels()`'s own existing "restore
        afterward" discipline exactly — a later graph-recorded frame
        touching the SAME volume texture next frame must see it in the
        state the render graph itself still believes it's in):
        ```cpp
        rg::EmitImageBarrier(cmd, volume.image, /*range=*/{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, volumeSampledState, previousState);
        ```
      - Leave `m_outputTexture`'s image in `VK_IMAGE_LAYOUT_GENERAL` — do
        not transition it back inside this same submission; the next step
        reads it back via `Renderer::CaptureImagePixels()`, which performs
        its own transition dance given whatever `ResourceState` you tell it
        the image is CURRENTLY in.
   f. Update `m_outputTextureState = outputWriteState` (`GENERAL`) so the
      NEXT call's own barrier-from-previous-state step above is correct.
   g. Call
      `renderer.CaptureImagePixels(m_outputTexture->Image(),
      VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_UNORM, {kOutputWidth,
      kOutputHeight}, m_outputTextureState, /*bytesPerPixel=*/4)` — this
      internally transitions GENERAL -> TRANSFER_SRC_OPTIMAL, copies to a
      staging buffer, transitions back to GENERAL, and hands back
      tightly-packed RGBA8 bytes — EXACTLY the shape Phase 4 needs to plug
      straight into the existing PNG-encode path with zero new conversion
      code (this output format is never BGRA, so
      `Encoding::ConvertBgraToRgbaInPlace()` is never needed for this
      capture kind — document this explicitly at the Phase 4 call site so
      a future reader doesn't wonder why that conversion is missing there).
      `CaptureImagePixels()` restores the image to `GENERAL` afterward (per
      its own contract) — `m_outputTextureState` therefore does not need
      updating again after this call (it is already `GENERAL`).
   h. Wrap the result in this class's own `CapturedRawPixels` and return it.
3. Destructor: destroy `m_volumeSampler`/`m_descriptorSetLayout` via
   `vkDestroySampler`/`vkDestroyDescriptorSetLayout` (mirroring
   `AtmosphereLutRenderer`'s own destructor pattern for its similarly
   raw-owned Vulkan objects) — `m_pipeline`/`m_outputTexture` clean up
   themselves via their own RAII destructors (`ComputePipeline`/`Texture2D`),
   no manual cleanup needed for those two. `m_descriptorSet` (a
   `ComputeDescriptorSet`) owns no Vulkan handle of its own (the descriptor
   set it wraps was allocated from a shared pool and is never individually
   freed — see `ComputeDescriptorSet.h`'s own class comment), so it needs no
   destructor logic either.

### 3.5 — CMakeLists.txt wiring

Add `src/Renderer/VolumeTexturePreviewMath.cpp`/`.h` and
`src/Renderer/VolumeTexturePreviewRenderer.cpp`/`.h` to `gte_core`'s source
list (unconditional — no `GTE_ENABLE_EDITOR` guard, per Locked Design
Decision 4), and register `src/Shaders/VolumeTexturePreview.comp` in the
shader-compile list (see 3.3 — `gte_add_shader(GreatTamanaEngine
src/Shaders/VolumeTexturePreview.comp)`). Add
`tests/Renderer/VolumeTexturePreviewMathTests.cpp` to
`tests/CMakeLists.txt`.

### Verification

- Fast compile check: build `gte_core` (this alone catches shader-adjacent
  C++/GLSL mismatches only at the level of "does the C++ side compile" —
  actually validating the shader itself compiles requires the shader
  compile step in this project's build, so build the FULL `gte_core`
  target, not just a syntax-check, since that's what actually invokes
  `glslc`/whatever this project's `cmake/CompileShaders.cmake` shells out
  to).
- Run the new `VolumeTexturePreviewMathTests` and confirm every assertion
  passes (this is the one piece of this phase that's genuinely Tier-1
  testable and MUST be green before moving on).
- A real runtime smoke check is valuable but not yet END-TO-END possible
  from the network (Phase 4 hasn't wired it up yet) — if time allows, add
  one small, throwaway, temporary call site that exercises the real thing.
  By this point in the campaign, Phases 1-2 are already done, so
  `gte::rg::RenderGraph::DebugVolumeTextureSnapshotFor("AtmosphereAerialPerspectiveVolume_GameView")`
  (populated automatically, every frame, with zero opt-in — see
  `PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md`) is the concrete,
  already-available way to obtain a real `VolumeTarget`/`rg::ResourceState`
  pair for this: e.g. from wherever `Application::Run()` already holds a
  reference to the offscreen (Game View) `RenderGraph` instance, once at
  least one frame has rendered, call
  `snapshot = renderGraph.DebugVolumeTextureSnapshotFor("AtmosphereAerialPerspectiveVolume_GameView")`,
  check `snapshot.has_value()`, then call
  `VolumeTexturePreviewRenderer::RenderPreview(renderer, snapshot->target, snapshot->state)`
  and write the result to a local PNG file via `Encoding::EncodeRgba8ToPng()`
  + `write_file`, then `load_image` it to visually sanity-check the output
  looks like a plausible raymarched box (not solid black, not solid
  background, not garbage) BEFORE moving to Phase 4 — remove this
  throwaway call site again before committing Phase 3 (Phase 4 is the
  real, permanent call site, and will do this exact same lookup for real).
- Write `PHASE3_COMPLETION_REPORT.md`, commit.
