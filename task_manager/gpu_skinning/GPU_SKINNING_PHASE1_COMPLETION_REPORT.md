# GPU Vertex Skinning — Phase 1: Data & Buffer Layout Foundations — Completion Report

Status: **DONE**. Implements
`task_manager/gpu_skinning/GPU_SKINNING_PHASE1_DATA_BUFFER_FOUNDATIONS_STRATEGY_v1.md`
in full, per the scope fence set by
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` ("Focus on selected section
only" — this report covers ONLY the "ON GOING" Phase 1 item; Phases 2-7
remain `[TODO]`, untouched).

## What was built

One new module, `src/Renderer/GpuSkinning/` — deliberately Vulkan-header-
free (no `<volk.h>`/`<vulkan/vulkan.h>` anywhere in it), mirroring
`src/Renderer/GpuTiming.h`'s own "pure data/math half has zero Vulkan
dependency" precedent so it stays trivially Tier-1-testable with no live
`VkDevice` at all:

- **`src/Renderer/GpuSkinning/GpuSkinningTypes.h`** — the four GPU buffer
  layouts a future compute kernel (Phase 2) will read/write:
  - `GpuBindPoseVertex` — position + normal, each padded to a `vec4` (32
    bytes total) so a `std430` GLSL array of these never misaligns past
    the first element (the single most important pitfall this phase
    exists to head off — a plain `std::vector<Vec3>` uploaded byte-for-
    byte would be silently misread by GLSL's own `vec3`-in-an-array
    alignment rule).
  - `GpuUv` — a plain, unpadded `vec2` (8 bytes) — `std430`'s own `vec2`
    alignment rule needs no extra padding, unlike `vec3`.
  - `GpuSkinWeights` — mirrors `VertexSkinWeights`
    (`src/Assets/MeshData.h`) exactly (4 bone indices + 4 weights, 32
    bytes), minus its SDEF-only correction terms (deliberately not
    carried onto the GPU path, matching the CPU path's own existing
    "treat SDEF like BDEF2" simplification).
  - `GpuSkinnedVertexPositionNormal` / `GpuSkinnedVertexPositionNormalUv`
    — the tightly-packed (NOT `std430`-padded) output layouts, byte-
    identical to `MeshVertex`/`MeshVertexUv`
    (`src/Renderer/MeshVertex.h`) respectively, each guarded by a
    `static_assert` on the literal byte count (24 / 32) — this file
    deliberately does not `#include MeshVertex.h` (which pulls in
    `<volk.h>` transitively), so the two are kept in agreement by a
    named, cross-referencing comment instead of a shared `#include`,
    the same convention `GpuTiming.h`'s `kGpuTimingFramesInFlight`
    already established relative to `FramePresenter::kFramesInFlight`.
  - The binding-number table (`Binding 0..3` → bind pose / skin weights /
    bone matrices / skinned output) is written once, in this header's own
    top comment block, as the single source of truth Phase 2's `.comp`
    files and Phase 4's descriptor-set-building code must both cite.
  - Three pure packing functions: `PackBindPoseVertices()`, `PackUvs()`,
    `PackSkinWeights()` — the ONLY place any of this padding/repacking
    happens.
- **`src/Renderer/GpuSkinning/GpuSkinningTypes.cpp`** — the three packing
  functions' implementations.
- **`tests/Renderer/GpuSkinning/GpuSkinningTypesTests.cpp`** — 8 new
  GoogleTest cases (Tier 1, no live GPU/Renderer/SDL involved):
  - Bind-pose packing produces the exact expected padded values.
  - A mismatched/missing normals array falls back to `Vec3::Up()` per
    vertex — mirroring `Animation/VertexSkinning.cpp`'s own
    `SkinVertexRange()` `hasNormals ? bindNormals[i] : Vec3::Up()`
    fallback exactly (checked against the real CPU source, per the
    master strategy's own instruction to verify this line-by-line rather
    than guess).
  - UV pass-through (including the empty-input case).
  - Skin weights: real influences are preserved verbatim; unused slots
    (`boneIndex < 0`) are translated to bone 0 / weight 0.0 — **never** a
    raw bit-reinterpreted `-1` (which would alias a huge, out-of-bounds
    `std::uint32_t` index if a future GPU kernel ever indexed into the
    bone matrix buffer before checking the weight) — and the empty-input
    case.
  - A direct `sizeof()` check on all five structs, forcing this
    translation unit to compile against (and therefore exercise) every
    `static_assert` already living in the header.

## Wiring

- `CMakeLists.txt` (root) — added
  `src/Renderer/GpuSkinning/GpuSkinningTypes.h`/`.cpp` to `gte_core`'s
  source list, right after `src/Renderer/MeshVertex.h` (the vertex-layout
  files this new module's output types are byte-compatible with).
- `tests/CMakeLists.txt` — added
  `Renderer/GpuSkinning/GpuSkinningTypesTests.cpp` to the always-built
  (no `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` dependency) Tier 1
  test list, plus a matching doc-comment entry describing what it covers,
  following this file's own existing per-test-file documentation
  convention.

## What was deliberately NOT done (per Phase 1's own "What We Will NOT Do")

- No generic/templated GPU buffer-layout system — four concrete structs,
  four concrete pack functions, nothing more.
- No compression of skin weights/bone indices (full-precision
  `float`/`uint32_t` throughout, matching the CPU path's own precision —
  this is what gives Phase 6's future parity check the best chance of
  matching within a tight epsilon).
- No support for more than 4 bone influences per vertex (matches the CPU
  path's own hard cap exactly).
- **No buffer creation, descriptor set building, or Vulkan calls of any
  kind.** This phase is pure data-shape/layout definition plus pure C++
  packing functions only — `Renderer.h`/`RenderGraph*`/`AnimationSystem*`
  were not touched at all, per Phase 1's own explicit instruction.

## Verification performed

- **Fast compile check only** (per this session's own instructions — no
  full build/regression test; that is explicitly deferred to later):
  - `cmake --build build --target gte_core` — succeeded, 26/26 objects
    built + linked into `libgte_core.a` with zero errors/warnings related
    to this change (the large number of recompiled files was a
    side-effect of the CMake reconfigure step picking up the
    `CMakeLists.txt` edit, not a sign of anything broken).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded,
    linked `GreatTamanaEngineTests.exe` with zero errors.
  - Ran the 8 new tests directly
    (`GreatTamanaEngineTests.exe --gtest_filter=GpuSkinningTypes.*`) — all
    8 passed.
- Did **not** run the full test suite (`ctest`) or a clean
  `build_joboff` verification build — both are explicitly deferred to
  "later, after everything done" per this session's instructions.

## Notes for future phases

- Phase 2 (Compute Kernel) must read the binding table directly from
  `GpuSkinningTypes.h`'s own top comment block rather than re-deriving
  binding numbers independently, and must decide/confirm the GLSL-side
  declaration shape (array-of-structs matching `GpuBindPoseVertex`
  exactly, vs. a struct-of-arrays split) — this phase settled on
  array-of-structs for `GpuBindPoseVertex` (one struct per vertex,
  `position` immediately followed by `normal`), which is the layout
  Phase 2's own strategy document already anticipated as one valid
  option; if Phase 2 finds a struct-of-arrays layout is actually needed
  on the GLSL side, that is a Phase 1 revision, not a silent Phase 2
  workaround (per Phase 2's own strategy document, Step 3.2).
- Phase 4 (Per-Model Resource Management) is what actually calls
  `PackBindPoseVertices()`/`PackUvs()`/`PackSkinWeights()` against a real
  model's `SkinnedMeshData` and uploads the results via
  `Renderer::CreateStructuredBuffer()` — this phase deliberately stops
  short of that (no `Renderer`/`Buffer` dependency exists in
  `GpuSkinningTypes.h`/`.cpp` at all).
- The `GpuSkinnedVertexPositionNormal`/`GpuSkinnedVertexPositionNormalUv`
  `static_assert`s are the load-bearing guard against a future edit to
  `MeshVertex.h` silently breaking byte-compatibility with the GPU
  skinning output buffer — add/update a matching assert here any time
  either `MeshVertex`/`MeshVertexUv` changes.
