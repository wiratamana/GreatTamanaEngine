# PHASE3_COMPLETION_REPORT — Volume Raymarch Preview Renderer (network-impl-6)

Parent: `PHASE0_MASTER_STRATEGY.md`. Task file:
`PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md` (its own v2 revision, already
corrected against the real source tree before this phase started). Previous
phase: `PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md`
(`PHASE2_COMPLETION_REPORT.md` read in full before starting this one —
nothing in it superseded this phase's own assumptions; Phase 1/2 both
landed exactly as their own strategy documents described, and
`RenderGraph::DebugVolumeTextureSnapshotFor()` — the accessor this phase's
own manual smoke test needed — was confirmed real and callable).

## Summary

Implemented `gte::VolumeTexturePreviewRenderer` — a small, self-contained,
on-demand GPU compute renderer that raymarches an arbitrary live
`VolumeTarget` into a fixed-size 256x256 RGBA8 "Volume mode" thumbnail
(Unity's Texture3D-inspector style: front-to-back alpha-composited
raymarch, fixed isometric-ish camera, dark-gray background) — plus its
pure CPU math oracle (`VolumeTexturePreviewMath.h/.cpp`) and the new
compute shader (`src/Shaders/VolumeTexturePreview.comp`), exactly per Step
3 of the strategy document. This phase is scoped to building and
individually verifying the renderer only — it is **not yet wired to the
network endpoint** (that is Phase 4's job); a temporary, throwaway call
site was used to verify the whole pipeline end-to-end at runtime, then
removed before committing, exactly as the strategy document's own
Verification section asked for.

## What was done

1. **New file: `src/Renderer/VolumeTexturePreviewMath.h`/`.cpp`** (Step
   3.1) — `VolumeCameraSetup`/`ComputeVolumeCameraSetup()` (fixed 45°
   azimuth / ~35.264° elevation isometric-style camera, distance derived
   from the box's bounding-sphere radius fit inside a 45° vertical FOV with
   a 1.2x margin, `boxHalfExtents = 0.5 * (w,h,d) / max(w,h,d)`) and
   `IntersectRayBox()` (origin-centered-box slab method, IEEE-754
   `±inf`-reliant, no defensive epsilon, per the strategy document's own
   instruction). Zero Vulkan dependency, matching Locked Design Decision 4.
2. **New test file: `tests/Renderer/VolumeTexturePreviewMathTests.cpp`**
   (Step 3.2) — 8 tests: perfect-cube exact half-extents, non-cubic
   (128x128x32) proportional scaling (the campaign's own Locked Design
   Decision 7 regression), camera-basis orthonormality, eye-outside-bounding-
   sphere, the "every box corner is reachable from the eye" camera-placement
   invariant, and three direct `IntersectRayBox()` cases (hit-from-outside,
   starts-inside, misses-entirely). All 8 pass.
3. **New shader: `src/Shaders/VolumeTexturePreview.comp`** (Step 3.3) — one
   invocation per output pixel (`local_size_x/y = 16`), binding 0 =
   `sampler3D volumeTex` (combined image sampler), binding 1 = `image2D
   outputImage` (rgba8, writeonly); push constants matching the C++
   `PushConstants` struct byte-for-byte (every `vec3` immediately followed
   by one `float`, giving a natural 16-byte-aligned std140-style group with
   no padding needed); a GLSL transcription of `IntersectRayBox()` plus the
   front-to-back alpha-composite raymarch loop from the how-to reference
   material, verbatim per the strategy document. Registered in
   `CMakeLists.txt` right after
   `AtmosphereAerialPerspectiveVolumeDebugSlice.comp`'s own registration,
   unconditional (no `GTE_ENABLE_EDITOR` guard).
4. **New file: `src/Renderer/VolumeTexturePreviewRenderer.h`/`.cpp`** (Step
   3.4) — `VolumeTexturePreviewRenderer::RenderPreview()`: lazily
   initializes (once) a trilinear/clamp-to-edge `VkSampler` it owns
   directly, a descriptor set layout (combined-image-sampler + storage
   image, via `DescriptorSetLayoutBuilder`), a `ComputePipeline`, a
   `ComputeDescriptorSet`, and a persistent 256x256 `Texture2D` output
   (`allowStorageImageAccess = true`, zero-filled initial pixels). Every
   `RenderPreview()` call: computes `VolumeCameraSetup` from the volume's
   own texel extent, rewrites the descriptor set's binding 0/1 (a different
   volume every call), builds this call's `PushConstants`, then issues
   **one** `Renderer::ImmediateSubmit()` that hand-rolls its own
   `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/`vkCmdPushConstants`/
   `vkCmdDispatch` calls (never `Renderer::Dispatch()`, which is gated to
   render-graph-pass recording only) bracketed by manual `rg::EmitImageBarrier()`
   calls: the caller-supplied volume image transitions
   `previousState -> (a hand-built COMPUTE-shader-combined-image-sampler-read
   ResourceState, since neither `ShaderRead` nor `ComputeShaderRead` is
   correct for this exact case) -> back to previousState` around the
   dispatch, and the output texture transitions `tracked state -> GENERAL`
   (staying `GENERAL` afterward — `CaptureImagePixels()` handles the rest of
   its own transition dance). The final step calls
   `Renderer::CaptureImagePixels()` (RGBA8, never BGRA, so no swizzle
   needed) to read the result back to the CPU. Destructor destroys the
   owned sampler/descriptor-set-layout directly; `ComputePipeline`/`Texture2D`
   clean up themselves.
5. **CMakeLists.txt / tests/CMakeLists.txt wiring** (Step 3.5) — the four
   new `.h`/`.cpp` files added to `gte_core`'s source list (right after the
   existing `VolumeTarget.h` entry), the shader registered as described
   above, and the new test file added to `GTE_TEST_SOURCES` (right after
   `Renderer/ComputeDispatchTests.cpp`) — all unconditional, no
   `GTE_ENABLE_EDITOR` guard, per Locked Design Decision 4.

## Deviations from the strategy document

**One deliberate deviation, driven by a genuine gap the strategy document
itself did not fully resolve**, documented here as instructed:

- **The document's own "Verification" section says to build only `gte_core`
  for this phase, but also says building it is "what actually invokes
  glslc"** — that second claim does not hold for THIS repository's actual
  `CMakeLists.txt`: every `gte_add_shader(...)` call in this project targets
  `GreatTamanaEngine` (the app executable), never `gte_core` (the static
  library) — confirmed directly by reading `cmake/CompileShaders.cmake`
  (the shader's `add_custom_command`/`target_sources()` calls are always
  attached to whichever target name is passed in, and every call site in
  `CMakeLists.txt` passes `GreatTamanaEngine`). Building only `gte_core`
  therefore compiles the new C++ classes but **never actually runs `glslc`
  against the new `.comp` file at all** — a genuine syntax error in the new
  shader would have gone completely undetected by following the document's
  literal instruction. Since this phase's own stated goal ("take the time
  this phase's own detail asks for") explicitly cares about the shader
  compiling correctly, I additionally built the full `GreatTamanaEngine` app
  target (this only ever performs a COMPILE step — glslc + linking — never
  a "full regression test" in the sense the overall task's workflow rules
  forbid for this phase) specifically to exercise `glslc` against the new
  shader. It compiled cleanly with zero warnings/errors. This is a build
  verification improvement, not a scope change — no runtime/gameplay code
  outside this phase's own new files was exercised by doing this.
- No other deviations. Every concrete detail in the v2-revised strategy
  document (binding order, push-constant packing, the hand-built
  `ResourceState` for a compute-shader combined-image-sampler read, the
  `Renderer::Dispatch()` prohibition, the destructor shape) matched the real
  source tree exactly once cross-checked, and needed no further correction.

## Verification

Per this phase's own "Verification" section (with the one addition
explained above):

1. **Fast compile check** — `cmake --build build --target gte_core`
   succeeded cleanly (2 new `.obj`s compiled, static library relinked, no
   warnings from the new files).
2. **`GreatTamanaEngineTests` build** — `cmake --build build --target
   GreatTamanaEngineTests` succeeded, and the new test binary was run
   filtered to just this phase's own suite:
   `GreatTamanaEngineTests.exe --gtest_filter=VolumeTexturePreviewMathTest.*`
   — **all 8 new tests passed**, including the box-corner-reachability and
   non-cubic-scaling invariants the strategy document called out as the
   most important ones to cover.
3. **Full `GreatTamanaEngine` app build** (the deviation explained above) —
   `cmake --build build --target GreatTamanaEngine` succeeded; the build log
   shows `Compiling shader src/Shaders/VolumeTexturePreview.comp ->
   .../build/shaders/VolumeTexturePreview.comp.spv` completing with no
   `glslc` errors, and the new `.spv` was staged next to the built `.exe`
   alongside every other shader.
4. **Manual, throwaway runtime smoke test** (per the strategy document's own
   "if time allows" Verification step) — added a temporary block directly
   after `m_renderGraph.FinalizeSynchronousGpuTiming()` in
   `Application::Run()` (`src/Application/Application.cpp`) that, once, on
   frame 30 of a real running session, called
   `m_renderGraph.DebugVolumeTextureSnapshotFor("AtmosphereAerialPerspectiveVolume_GameView")`,
   fed the result into `VolumeTexturePreviewRenderer::RenderPreview()`,
   PNG-encoded the result via the existing `Encoding::EncodeRgba8ToPng()`,
   and wrote it to a local file. Launched `GreatTamanaEngine.exe` via
   `run_app_background`, confirmed the PNG was written after ~5 seconds, and
   visually inspected it via `load_image`: a clearly non-cubic (thin,
   128x128x32-shaped) box rendered from a plausible isometric-ish angle
   against the documented dark-gray background — neither solid black, nor
   solid background, nor garbage — confirming the camera math, ray-box
   intersection, descriptor binding, and raymarch compositing all work
   together correctly against a REAL live volume texture
   (`AtmosphereAerialPerspectiveVolume_GameView`). The temporary block and
   its one new `#include` were then removed in full (verified via a
   line-for-line diff against the pre-change file — `git status` shows
   `Application.cpp` with no outstanding changes), and `gte_core` was
   rebuilt afterward to confirm the revert itself still compiles cleanly.
   The one-off smoke-test PNG file was deleted before committing.
5. **Full regression pass** (not required by this phase's own workflow rule,
   but run anyway as extra confidence, mirroring Phase 1/2's own practice,
   since it completes in under 90 seconds): `ctest -C Debug
   --output-on-failure` from `build/` — **1264 tests total, 1263 passed, 1
   skipped** (the same pre-existing, machine-gated
   `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`).
   Zero regressions, zero new failures — includes this phase's own 8 new
   `VolumeTexturePreviewMathTest` cases and Phase 1's 8
   `RenderGraphDebugVolumeTextureRegistryTest` cases, both green.

## Result

Phase 3 is complete: `VolumeTexturePreviewRenderer` exists, compiles into
every configuration (no `GTE_ENABLE_EDITOR` dependency), its pure math core
is fully Tier-1 tested, its compute shader compiles cleanly via `glslc`, and
a manual runtime smoke test proved the whole pipeline produces a visually
plausible raymarched thumbnail from a real, live `VolumeTarget`. Nothing
outside this phase's own new files was touched in the final, committed
state — `Application.cpp` is byte-for-byte unchanged from before this phase
started. Next: Phase 4
(`PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md`), which wires this
renderer into `GET /get_texture`'s existing handler for real.
