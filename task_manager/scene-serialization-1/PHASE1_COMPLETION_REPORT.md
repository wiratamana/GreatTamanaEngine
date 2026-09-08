# PHASE1 — Completion Report: Remove Hardcoded Demo Triangles

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full plan and `PHASE1_REMOVE_HARDCODED_DEMO_TRIANGLES.md` for this
phase's own detailed work order. This phase is now **complete**.

## What was done

Followed the phase document's plan exactly, as a surgical deletion/rename —
no new scene-object-creation code was added:

1. **`src/Game/Game.cpp`** — `Game::EnsureDemoSceneBuilt(Renderer& renderer)`
   was rewritten into `Game::EnsureDefaultCameraExists()`:
   - Removed the private demo `Pipeline` (`m_demoPipeline`, built from
     `shaders/Triangle.vert.spv`/`Triangle.frag.spv`), the private 3-vertex
     `Mesh` (`triangleMesh`), and the loop that spawned the 3
     `Transform`+`MeshRenderer` triangle entities at
     `x = { -0.6, 0.0, 0.6 }`.
   - **Kept** the one default Camera entity (`Transform{ position =
     (0,0,-5) }` + `Camera{}`) exactly as before — a brand-new/freshly-loaded
     scene still has something to look through.
   - `Game::Render()`'s call site updated from `EnsureDemoSceneBuilt(renderer);`
     to `EnsureDefaultCameraExists();` (no `Renderer&` argument needed any
     more).
   - Removed the now-unused `#include "../Renderer/Vertex.h"` (nothing else
     in the file references `Vertex` any more). `ECS/Components/Camera.h`
     and `ECS/Components/Transform.h` includes were kept, as instructed —
     both still needed for the Camera entity.
2. **`src/Game/Game.h`** —
   - Renamed the private method declaration to
     `void EnsureDefaultCameraExists();` and rewrote its doc comment to
     describe the narrowed, Camera-only behavior (previously described "three
     entities sharing one triangle Mesh/Pipeline...").
   - Removed the two dead private members (`bool m_demoSceneBuilt = false;`,
     `PipelineHandle m_demoPipeline;`), replaced with a single renamed guard
     flag: `bool m_defaultCameraEnsured = false;`.
   - Confirmed `Game.h` never directly `#include`s `PipelineHandle.h` (it
     only ever saw the type transitively through `RenderSystem.h`), so no
     include-list change was needed there.
3. **`src/Editor/EditorCamera.h`** — updated a stale comment (not itself part
   of the phase's file list, but a direct doc-comment reference to the
   now-renamed function) from "`Game::EnsureDemoSceneBuilt()`" to
   "`Game::EnsureDefaultCameraExists()`", keeping the cross-reference
   accurate for future readers.
4. **Confirmed guardrails held**: `src/Shaders/Triangle.vert`/`.frag` and
   their `gte_add_shader()` CMake registration were left completely
   untouched — `PrimitiveGpuCatalog::EnsureDefaultPipeline()`
   (`src/Game/Instantiation/PrimitiveGpuCatalog.cpp`) still independently
   loads the same shader pair for every "Create 3D Object" primitive, and
   was not modified in this phase.
5. **Confirmed no other call site referenced the removed symbols** — a
   repository-wide search for `EnsureDemoSceneBuilt`, `m_demoSceneBuilt`, and
   `m_demoPipeline` after the edit returned zero remaining matches anywhere
   in `src/` (the one non-code hit was the `EditorCamera.h` comment fixed in
   step 3 above).

## Verification

- **Fast compile check** (per this task's workflow rules — no full build/
  regression test yet): `cmake --build build --target gte_core` from the
  repository root. Result: **clean build**, `libgte_core.a` linked
  successfully, including `src/Game/Game.cpp` and `src/Editor/EditorCamera.cpp`
  (the two files touched). Only pre-existing, unrelated warnings/messages
  appeared (a KTX-Software `git describe` version-fallback warning from
  `third_party/ktx`), no errors.
- No test files were added/changed in this phase (matches the phase
  document's own "What We Will NOT Do" — this is a pure deletion/rename, no
  new Tier-1-testable logic was introduced).

## Notes / deviations from the phase document

- `src/Game/Game.cpp` also `#include`s `ECS/Components/MeshRenderer.h`,
  which is no longer referenced anywhere in the file after this phase's
  edit (it was only used by the removed triangle-spawning loop). The phase
  document only explicitly called out `Vertex.h` as a removal candidate and
  named `Camera.h`/`Transform.h` as "still needed" — it did not mention
  `MeshRenderer.h` either way. Left it in place, unused, to match the
  phase document's literal instructions exactly rather than guessing at an
  unstated intent; this is a header-only, zero-cost leftover (no link/size
  impact) and can be swept up in a later cleanup pass if desired.

## Next phase

**PHASE2_PRIMITIVE_SOURCE_TAG_AND_HIERARCHY_DESTROY_HELPER** — add the
`PrimitiveSource` ECS component and
`TransformHierarchy::DestroyEntityAndDescendants()`, per
`PHASE0_MASTER_STRATEGY.md`'s ordering.
