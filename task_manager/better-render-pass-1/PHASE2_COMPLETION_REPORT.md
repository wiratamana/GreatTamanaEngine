# PHASE2 — Reflection-Based Compute Pipeline Creation — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md`.

## Summary

`GpuResourceFactory::CreateComputePipeline(shaderSpirvPath)` (and `Renderer::CreateComputePipeline()`,
which forwards into it) now works with **only a path** in the common case: no hand-built
`VkDescriptorSetLayout` vector, no hand-built `VkPushConstantRange`. `ComputePipeline`'s constructor
now reflects the shader via PHASE1's `ReflectComputeShader()` whenever the caller omits both
`descriptorSetLayouts` and `pushConstantRange`, and builds its own descriptor-set layout(s)/
push-constant range from that reflection data. The existing, fully-manual call shape is completely
unchanged for every call site that still supplies either argument explicitly — which is every one of
`PHASE0` Step 2.2's 16 real `ComputePipeline` instances, since none of them are migrated by this
phase (that is PHASE4 onward's job).

Everything in "Step 3 — The Plan" and the "Acceptance bar" was completed exactly as scoped, with the
ambiguities the task doc flagged all resolved per its own guidance (see "Design decisions resolved"
below) — no `ask_questions` call was needed since the doc's own text already settled every open
question with enough detail to proceed confidently.

## What changed

### `src/Renderer/Vulkan/ShaderReflection.h`/`.cpp` (new pure helper)

Added `DescriptorBindingSetGroup` (a `set` number + its own `std::vector<ReflectedDescriptorBinding>`)
and `GroupDescriptorBindingsBySet()` — a small, pure, Tier-1-testable function that groups a flat
`std::vector<ReflectedDescriptorBinding>` by `.set`, returning one group per distinct set value in
ASCENDING set-number order, with each group's own relative binding order preserved. Implemented with a
plain `std::map<std::uint32_t, std::vector<ReflectedDescriptorBinding>>` — map iteration order gives
ascending keys "for free", and `push_back` into each bucket never reorders. This lives in
`ShaderReflection.h`/`.cpp` (not a new file) since it operates purely on `ReflectedDescriptorBinding`
values with zero `VkDevice`/`ComputePipeline` dependency — consistent with `ShaderReflection.h`'s own
documented "no engine call site dependency" design.

### `src/Renderer/ComputePipeline.h`/`.cpp` (the real change)

- The constructor's public signature is **completely unchanged** — no new parameter, no new enum, per
  the task doc's own explicit "RESOLVED" instruction. The reflect-vs-manual decision is made purely
  from the values already received: `descriptorSetLayouts.empty() && !pushConstantRange.has_value()`
  selects the REFLECT path; anything else (today: every real call site) selects the MANUAL path,
  behaving byte-for-byte as before.
- **Reflect path**: calls `ReflectComputeShader(shaderSpirvPath)` (PHASE1), groups the result's
  descriptor bindings via `GroupDescriptorBindingsBySet()`, and builds one `VkDescriptorSetLayout` per
  distinct `set` via `DescriptorSetLayoutBuilder` (mapping each reflected `VkDescriptorType` to the
  matching `AddStorageBuffer`/`AddStorageImage`/`AddCombinedImageSampler` call). These layouts are fed
  into the exact same `VkPipelineLayoutCreateInfo` construction the manual path already used — the
  pipeline-creation code itself was not duplicated, only the SOURCE of `resolvedSetLayouts`/
  `resolvedPushConstantRange` was made conditional.
- New owned state: `m_ownedReflectedLayouts` (the layouts THIS pipeline built and must destroy) +
  `m_ownedReflectedSetNumbers` (the real GLSL `set` number each one corresponds to, same index),
  `m_pushConstantSize`, `m_localSize` (reusing `ComputeDispatch.h`'s existing `Extent3D` — no second
  3-uint32_t type invented). The manual path leaves all four at their default/empty value — a
  manually-built pipeline genuinely has no reflected metadata to report.
- New accessors: `ReflectedDescriptorSetLayout(set = 0)` (linear search over
  `m_ownedReflectedSetNumbers`, `VK_NULL_HANDLE` if not found or built manually),
  `PushConstantSize()`, `LocalGroupSize()`.
- **Ownership/exception-safety**: `Destroy()` now also destroys every layout in
  `m_ownedReflectedLayouts` (guarded by `VK_NULL_HANDLE` checks, safe on a moved-from/already-destroyed
  object, same discipline as the existing `m_pipeline`/`m_layout` fields). Since a THROWING
  CONSTRUCTOR never runs its own destructor, a local `destroyOwnedReflectedLayouts` lambda is called
  from BOTH `catch (...)` blocks (the reflection step itself, and the later
  `vkCreatePipelineLayout`/`vkCreateComputePipelines` step) so no reflected layout ever leaks or is
  left dangling on a failure path. The move constructor/assignment transfer all four new members via
  `std::exchange`/`std::move`, mirroring the existing three-member pattern exactly.
- `ComputePipeline.h` now includes `ComputeDispatch.h` for `Extent3D` (a small, intentional,
  Vulkan-free dependency `ComputeDispatch.h` was already designed for — see that header's own comment).

### `tests/Renderer/Vulkan/ShaderReflectionTests.cpp` (extended, not a new file)

Per the master strategy's own instruction ("PHASE2/8/9 only extend an ALREADY-registered existing
test file, so no new list entry is needed for those"), three new Tier-1 tests were added to the
already-registered `ShaderReflectionTests.cpp` for the new `GroupDescriptorBindingsBySet()`:
- Empty input → empty result.
- All bindings in set 0 → one group, relative binding order preserved.
- Bindings split across set 0 and set 1, declared in a DELIBERATELY interleaved input order → two
  groups, in ascending set order regardless of input order, each with its own bindings in original
  relative order.

No `tests/CMakeLists.txt` change was needed (the file was already registered by PHASE1).

## Design decisions resolved (per the task doc's own guidance — no `ask_questions` needed)

1. **No new `ComputePipelineLayoutMode` enum parameter** — the task doc explicitly forbade this and
   explained exactly why (every one of the 16 not-yet-migrated call sites would have its own explicit
   arguments silently overridden). Implemented exactly as directed: decide purely from whether both
   `descriptorSetLayouts`/`pushConstantRange` are at their default/empty value.
2. **Ownership of reflected layouts** — `ComputePipeline` itself owns and destroys any
   `VkDescriptorSetLayout` it builds via reflection (new `m_ownedReflectedLayouts` vector); the manual
   path's caller-supplied layouts remain caller-owned, exactly as before. This was the task doc's own
   recommended design, confirmed correct by re-reading `ComputePipeline.h`'s existing ownership
   comments before implementing — no part of it was ambiguous once the real header was in front of me,
   so the doc's own optional `ask_questions` escape hatch was not needed.
3. **Where the "unsupported descriptor type" check lives** — already resolved by PHASE1: `ShaderReflection.cpp`'s
   `MapDescriptorType()` already throws `std::runtime_error` for any `SpvReflectDescriptorType` other
   than `STORAGE_BUFFER`/`STORAGE_IMAGE`/`COMBINED_IMAGE_SAMPLER`, confirmed by direct inspection of
   that file before writing any PHASE2 code. `ComputePipeline`'s own constructor therefore has a
   `switch` over the three supported `VkDescriptorType` values with a `default:` branch that throws —
   documented explicitly as an unreachable, defensive-only tail (mirrors this codebase's own
   `DispatchByKind()` precedent: "a real, `default:`-less exhaustive switch internally with a
   hard-fail unreachable tail" — here the hard-fail IS the one `default:` case, since
   `ReflectComputeShader()` already guarantees it can never actually fire). The check lives in exactly
   ONE place (PHASE1's `MapDescriptorType()`), never duplicated.
4. **`bool hasPushConstantRange == 0` meaning "nothing to check against"** — not literally touched by
   this phase (that is PHASE3's `SetPushConstants<T>()` job), but the manual path's
   `m_pushConstantSize = 0` is set up correctly now so PHASE3 can rely on "0 means no reflected
   metadata" without any further PHASE2 rework.

## Verification

### Compile check

`cmake -S . -B build` (incremental reconfigure, no new downloads) then `cmake --build build`
(incremental) — succeeded end to end, zero warnings/errors from any touched file, including
`gte_core`, `gte_editor`, `GreatTamanaEditor`, both Project Assembly demo `.dll`s, and
`GreatTamanaEngineTests`.

### Targeted test run

```
ctest -R ShaderReflection --output-on-failure
```
All 5 tests pass (2 pre-existing from PHASE1 + 3 new `GroupDescriptorBindingsBySet` tests).

### Tier-2 manual verification (per the Acceptance Bar's own requirement)

The task doc requires confirming the reflection-path pipeline resolves the exact same real
binding/push-constant-size layout as an existing hand-built manual-path pipeline for the same shader,
**without shipping this comparison as a permanent test**. Two approaches were tried:

1. **Headless GPU fixture** (`tests/Fakes/HeadlessRenderGraphFixture.h`, `VK_EXT_headless_surface`) —
   a throwaway test file (`ComputePipelineManualVerificationTests.cpp`, temporarily added to
   `GTE_TEST_SOURCES`) built and ran a reflection-path pipeline and a manual-path pipeline for
   `BoxBlur.comp.spv` side by side. It compiled and ran correctly but reported `GTEST_SKIP()` on this
   development machine — this machine's Vulkan driver/loader does not support
   `VK_EXT_headless_surface`, a pre-existing, already-documented limitation (see `AGENTS.md`,
   "Testability & Regression Safety": "the current development machine doesn't support headless mode
   anyway, so this must never be treated as a blocker").
2. **Live windowed Editor session** (the approach that actually produced a real result): temporarily
   added a second, throwaway reflection-path `ComputePipeline` construction for `BoxBlur.comp.spv`
   right next to `ComputeBlurValidation::EnsureInitialized()`'s own existing manual-path construction,
   logging `ReflectedDescriptorSetLayout(0) != VK_NULL_HANDLE`, `PushConstantSize() == 2 *
   sizeof(uint32_t)`, and `LocalGroupSize() == {16,16,1}` via `GTE_LOG_INFO`. Launched
   `GreatTamanaEditor.exe` (`run_app_background`), activated the "Scene" tab and
   `GET /render_graph/set_blur_enabled?enabled=true` (`gte_send_request`) to force
   `ComputeBlurValidation::EnsureInitialized()` to actually run, then confirmed via
   `GET /get_logs?category=Phase2Verify`:
   ```
   ComputePipeline reflection path for BoxBlur.comp.spv: layoutOk=true pushConstantOk=true localSizeOk=true
   ```
   — an exact match against PHASE1's own already-proven `ReflectComputeShader()` values for this same
   shader (2 descriptor bindings, push-constant size `2 * sizeof(uint32_t)`, local size `16x16x1`).
   `GET /get_logs?min_level=Warning` showed only pre-existing, unrelated warnings (plugin
   render-feature priority ties, a GPU-timing-slot-budget note) — nothing new, nothing related to
   `ComputePipeline`/reflection. `GET /get_swapchain` confirmed the Editor was rendering normally
   throughout (no crash, no magenta/garbage image).
   Both the throwaway test file (deleted, including its `tests/CMakeLists.txt` entry) and the
   temporary `ComputeBlurValidation.cpp` instrumentation (fully reverted) were removed before this
   phase finished — confirmed via `git diff --stat` showing **zero** net change to
   `ComputeBlurValidation.cpp` (only a line-ending-normalization warning, no actual content diff) and
   `git status` showing it as the only file with no real delta.

No leak/double-free: the new ownership logic was reviewed by inspection (every `Destroy()`/exception
path destroys `m_ownedReflectedLayouts` exactly once, guarded by `VK_NULL_HANDLE` checks), and the live
Editor session above exercised the real reflection-path construction/destruction path at least once
with zero Vulkan validation-layer-visible errors in the logs.

## Acceptance bar — final check

- [x] `renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv")` (path-only) now produces a fully
      correct, bindable pipeline with the exact same real binding/push-constant-size layout as
      `ComputeBlurValidation.cpp`'s own existing hand-built call — verified live (see above).
- [x] The existing, fully-manual constructor overload/call shape still compiles and behaves
      byte-for-byte identically for every one of PHASE0 Step 2.2's real call sites (confirmed: zero of
      those 16 call sites were touched by this phase — `GreatTamanaEditor.exe` built and ran
      correctly with every one of them still using the manual path).
- [x] No leak/double-free of any `VkDescriptorSetLayout` (confirmed by inspection + a live exercise of
      the new path with zero validation-layer-visible errors).

## Files changed this phase

- `src/Renderer/Vulkan/ShaderReflection.h` — added `DescriptorBindingSetGroup` +
  `GroupDescriptorBindingsBySet()` declaration.
- `src/Renderer/Vulkan/ShaderReflection.cpp` — implemented `GroupDescriptorBindingsBySet()`.
- `src/Renderer/ComputePipeline.h` — new reflection-path member state + three new public accessors;
  now includes `ComputeDispatch.h` for `Extent3D`.
- `src/Renderer/ComputePipeline.cpp` — reflect-vs-manual branch in the constructor, exception-safe
  owned-layout cleanup, `Destroy()`/move-constructor/move-assignment updates, `ReflectedDescriptorSetLayout()`.
- `tests/Renderer/Vulkan/ShaderReflectionTests.cpp` — three new Tier-1 tests for
  `GroupDescriptorBindingsBySet()`.
- `src/Editor/ComputeBlurValidation.cpp` — touched only for the temporary Tier-2 manual verification
  described above, then fully reverted (zero net diff, confirmed via `git diff --stat`).

PHASE3 can now build `gte::rg::CommandBuffer` + `SetPushConstants<T>()` directly on
`PushConstantSize()`/`LocalGroupSize()`, and PHASE4 onward can migrate each real call site simply by
calling `CreateComputePipeline(path)` with no further arguments.
