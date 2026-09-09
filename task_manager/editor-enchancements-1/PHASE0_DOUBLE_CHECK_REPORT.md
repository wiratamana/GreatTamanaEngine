# PHASE0_DOUBLE_CHECK_REPORT — Second-Iteration Self-Review Of The Scene Grid Strategy Docs

## Scope and method

Every concrete claim in all six strategy files (`PHASE0_MASTER_STRATEGY.md`
through `PHASE5_POLISH_AND_VERIFICATION.md`) was cross-referenced against the
REAL, current source in this repository — not just re-read on its own — using
`read_file`/`read_line`/`search_in_dir` against the actual files each phase
cites. Verified directly against real source, line-for-line where a phase
quoted one:

- `src/Math/Vec4.h`, `src/Math/Mat4.h`, `src/Math/MathTypes.h`
  (`kEpsilon`) — PHASE1's exact `Vec4`/`Mat4` API usage (`TryInverse()`,
  `operator*(const Mat4&, const Vec4&)`, `Data()`, constructor signature).
- `src/Editor/AssetPreviewMesh.h`/`.cpp` — PHASE3's pipeline-building
  precedent (`ReadShaderFile()`/`CreateShaderModule()`/`DepthFormatHasStencil()`,
  `EnsurePipeline()` shape, push-constant stage flags/size, depth/blend
  state fields).
- `src/Renderer/Renderer.h` (`VulkanContextInfo`, `ColorFormat()`/
  `DepthFormat()`/`GetVulkanContextInfo()`/`BeginGraphPassRecording()`),
  `src/Renderer/Pipeline.cpp` (`depthWriteEnable`/`blendEnable` defaults).
- `src/Application/RenderPasses.h`/`.cpp` and `src/Application/Application.cpp`
  — PHASE4's exact current `AddSceneViewPass()` signature/body and its real
  call site (line ~329), including the `AddGpuSkinningPasses()`/
  `FinalizeRenderTextureForExternalSampling()`/`AddBlurValidationPass()`
  surrounding context.
- `src/Editor/EditorLayer.h`, `src/Editor/NullEditorLayer.cpp`,
  `src/Editor/ImGuiEditorLayer.cpp` (`FinalizeBlurValidationForSampling`
  ordering, `m_sceneCamera` member neighborhood, the real destructor's manual
  cleanup list) — confirms PHASE4's insertion points and its "no destructor
  change needed" claim.
- `src/Renderer/RenderGraph/RenderGraphTypes.h`, `RenderGraphBarrierPlanner.cpp`
  (`RequiresBarrier()` == `!(previous == next)`), `RenderGraphCompiler.cpp`
  (the WAW edge-building loop) — the load-bearing "separate pass would be a
  synchronization bug" argument in PHASE0.
- `CMakeLists.txt` (exact `EditorCamera.cpp` line, the `MeshPreview.vert/frag`
  block at lines 727-730, `cmake/CompileShaders.cmake`'s `gte_add_shader()`
  staging path convention) and `tests/CMakeLists.txt` (the
  `if(GTE_ENABLE_EDITOR)` test list containing `EditorCameraTests.cpp`).
- `src/ECS/Components/Camera.h` (`farZ = 1000.0f` default) and
  `src/Editor/EditorCamera.h` (default-constructed `Camera m_camera{}`) —
  PHASE5's far-plane/fade-distance interaction check.
- A full-tree `grep` for "grid" — confirms PHASE0's claim that no
  ground/grid visual exists anywhere in the engine yet.

**Result: every one of the above checks passed — every concrete file path,
line-number reference, function/class/member signature, CMake block
location, and push-constant/pipeline-state claim in all six documents
matches the real source exactly, with no drift found.** This is a strong,
already-accurate strategy (a genuine second iteration in spirit, not a
from-scratch review), so most edits below are targeted closures of a real
but narrow gap, not corrections of factual drift.

## Per-file results

### PHASE0_MASTER_STRATEGY.md — no change needed
Every architectural claim (the RenderGraph WAW-edge-vs-no-barrier rejected
design, the exact push-constant convention, the `AddBlurValidationPass()`/
`AddPresentPass()`'s `recordImGui` precedents, the file-by-file change map)
checks out exactly against the real source. Left unmodified.

### PHASE1_GRID_MATH_FOUNDATION.md — edited
**Gap found and fixed:** the document's own Step 1 originally scoped exactly
two pure functions (`ComputeGridPlaneHit`/`ComputeGridLineCoverage`), but
Phase 2's `SceneGrid.frag` also needs a THIRD piece of math — the colored
X/Z axis-line coverage (`AxisLineCoverage()`) — which had no CPU-side
mirror or test anywhere in the original Phase 1 scope. This directly
contradicts this engine's own established, load-bearing convention (see
`AGENTS.md`, "GPU Vertex Skinning": *"the CPU path is the permanent
ORACLE"*) and its "Testability & Regression Safety" rule that new pure
logic should be Tier-1-tested from the start, not conditionally added later
only if a visual bug happens to surface it (which is what the original
PHASE5 Step 3.3 proposed). Fixed by:
- Adding a third function, `ComputeAxisLineCoverage(float distanceFromAxis,
  float derivative, float halfWidthWorld) noexcept`, to
  `SceneGridMath.h`/`.cpp`'s planned contents (declaration + implementation),
  mirroring `SceneGrid.frag`'s `AxisLineCoverage()` exactly (with an added
  `std::fabs()` on the derivative for robustness against a synthetic
  negative test input — a real `fwidth()` value is always non-negative, so
  this never changes on-GPU behavior).
- Adding a matching `ComputeAxisLineCoverage` test bullet list to Section
  3.4 (inside-half-width, beyond-half-width, non-positive-half-width, and
  negative-derivative-parity cases).
- Updating Step 1's intro to describe all three functions instead of two.

### PHASE2_GRID_SHADERS.md — edited
**Incorrectness found and fixed:** the `SceneGrid.frag` code snippet
actually contains TWO stray `#`-instead-of-`//` typos (one in the file's own
header comment, a SECOND, previously-uncalled-out one inside
`AxisLineCoverage()`'s own doc comment), but the document's own trailing
note only warned about ONE of them ("the stray `#` before `line up with.`").
An implementer following that note literally would have proofread only the
first occurrence and shipped a `SceneGrid.frag` that fails to compile
(a stray `#` at the start of a GLSL line is parsed as an invalid
preprocessor directive, not silently treated as a comment). Fixed by
rewriting the trailing note to explicitly call out both occurrences by
exact quoted text, and to instruct the implementer to proofread the WHOLE
file rather than assume only one instance exists.

**Gap closed (in step with the PHASE1 fix above):** `AxisLineCoverage()`'s
own doc comment now explicitly cross-references `SceneGridMath.cpp`'s new
`ComputeAxisLineCoverage()` as its CPU mirror (previously it was an
undocumented, un-mirrored, un-tested one-off), and its `derivative` term
now goes through `abs()` before the `max(..., 1e-6)` clamp — for parity
with the new CPU function's own `std::fabs()` guard (a genuine no-op change
for real, on-GPU `fwidth()` values, which are already non-negative).

### PHASE3_SCENE_GRID_RENDERER.md — no change needed
Cross-checked field-for-field against the real `AssetPreviewMesh.h`/`.cpp`
precedent it explicitly copies the shape of: `ReadShaderFile()`/
`CreateShaderModule()`/`DepthFormatHasStencil()` helpers, `EnsurePipeline()`
lazy-build pattern, `Reset()`'s `vkDeviceWaitIdle()`-then-destroy order, and
the deliberate, correctly-called-out difference from that precedent (this
pipeline's push-constant range is `VK_SHADER_STAGE_FRAGMENT_BIT`, 128 bytes,
not `VERTEX_BIT` like every other pipeline in the engine) — all confirmed
accurate against `Pipeline.cpp`'s real `depthWriteEnable`/`blendEnable`
defaults too. The document's own single stray-`#` typo-trap note (one
instance, at "`Renderer::Submit() entirely...`") was re-verified to
genuinely have exactly one instance in this file (unlike PHASE2's two) —
correct as written. Left unmodified.

### PHASE4_RENDERGRAPH_INTEGRATION.md — no change needed
`AddSceneViewPass()`'s real, current signature/body (`RenderPasses.h`/
`.cpp`) and its real call site (`Application.cpp`, inside the
`if (sceneTarget != nullptr)` block) match the document's quoted
before/after snippets verbatim, including the exact surrounding
`AddGpuSkinningPasses()`/`FinalizeRenderTextureForExternalSampling()`/
`AddBlurValidationPass()` context. `IEditorLayer::FinalizeBlurValidationForSampling()`'s
real position in `EditorLayer.h` (immediately before `BuildUI()`) and
`ImGuiEditorLayer`'s real destructor (confirmed it does NOT manually reset
`m_blurValidation`, supporting this document's own claim that the analogous
new `m_sceneGrid` member needs no destructor change either) were both
independently verified. The document's own single stray-`#` typo-trap note
was re-verified to have exactly one real instance in this file — correct
as written. Left unmodified.

### PHASE5_POLISH_AND_VERIFICATION.md — edited
`Camera::farZ`'s real default (`1000.0f`, `src/ECS/Components/Camera.h`)
and `EditorCamera`'s real default-constructed `m_camera{}` were confirmed —
comfortably larger than `SceneGrid.frag`'s `kFadeDistance = 100.0`, so
Step 3.1's own "if the far plane is comfortably larger... no code change is
needed" branch is the one that will actually apply; left as a real
implementer-time check rather than baked in as a hardcoded assumption
(appropriately conditional, no change needed there).

**Edited Step 3.3** to remove the "if this phase's own investigation
reveals Phase 1 never actually covered axis-line math at all" framing,
which is now stale given the PHASE1/PHASE2 fixes above (axis-line coverage
now has a CPU mirror + tests from the start, not conditionally). Step 3.3
now describes this as a verify-the-two-still-agree step, still instructing
a fix in both `SceneGrid.frag` AND `SceneGridMath.cpp`/`SceneGridMathTests.cpp`
(with a regression test) if a real bug is found, following the "CPU math is
the spec" discipline explicitly — never patching only the GLSL side in
isolation.

## Summary of actual edits

| File | Change |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | None — verified accurate as-is. |
| `PHASE1_GRID_MATH_FOUNDATION.md` | Added `ComputeAxisLineCoverage()` (declaration + implementation + tests) as a third, first-class function in this phase's original scope, closing a real Tier-1-testability gap. |
| `PHASE2_GRID_SHADERS.md` | Fixed the trailing note to call out BOTH stray `#` typos in the `SceneGrid.frag` snippet (previously only one was mentioned); cross-referenced `AxisLineCoverage()` to the new CPU mirror and aligned its derivative-clamping to match. |
| `PHASE3_SCENE_GRID_RENDERER.md` | None — verified accurate as-is. |
| `PHASE4_RENDERGRAPH_INTEGRATION.md` | None — verified accurate as-is. |
| `PHASE5_POLISH_AND_VERIFICATION.md` | Updated Step 3.3 to reflect that axis-line coverage now has a CPU mirror/tests from Phase 1 onward, removing the stale "add it now if this phase finds a bug" conditional framing. |

No engine source code (`src/`, `tests/`, `CMakeLists.txt`, `tests/CMakeLists.txt`)
was touched — this was a documentation-only review, exactly as scoped.
