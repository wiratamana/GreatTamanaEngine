# PHASE1 — Remove the 3 Hardcoded Demo Triangles (keep the default Camera)

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full root-cause analysis and ordering. This phase is self-contained
and can be built/tested on its own before PHASE2 begins.

## Step 1: The Goal (Where are we going?)

Delete the 3 hardcoded triangle entities (and the private Pipeline/Mesh they
share) that `Game::EnsureDemoSceneBuilt()` creates on the very first
`Render()` call — while **keeping** the one default Camera entity that same
function also creates (per the resolved design decision in PHASE0 — a
brand-new/just-loaded scene must still have something to look through). No
other engine behavior changes: `PrimitiveGpuCatalog`'s own, separate default
pipeline (which happens to load the exact same `Triangle.vert/.frag`
shaders) must keep working exactly as it does today.

## Step 2: The Situation / The Problem (Where are we now?)

`src/Game/Game.cpp`, `Game::EnsureDemoSceneBuilt(Renderer& renderer)`
(currently lines 86–150) does five distinct things in one function, guarded
by one `bool m_demoSceneBuilt`:

1. Registers a private demo `Pipeline` (`m_demoPipeline`) built from
   `shaders/Triangle.vert.spv`/`Triangle.frag.spv`.
2. Builds a private 3-vertex `Mesh` (`triangleMesh`, red/green/blue
   vertices).
3. Loops over `positions[3] = { -0.6f, 0.0f, 0.6f }`, creating 3 entities
   (`Transform` + `MeshRenderer{ triangleMesh, m_demoPipeline }`) — **this is
   "the 3 hardcoded triangles" the user asked to remove.**
4. Creates one Camera entity (`Transform{ position = (0,0,-5) }` +
   `Camera{}`) — **must be kept**, per PHASE0's resolved design decision.
5. Is called unconditionally from `Game::Render()` (line 156,
   `EnsureDemoSceneBuilt(renderer);`), before every frame's draw.

`Game.h` (currently lines 206–239) declares the same function plus its two
private members (`bool m_demoSceneBuilt = false;` and
`PipelineHandle m_demoPipeline;`), both of which exist ONLY to support the
triangle demo and must be removed alongside it.

**Guardrail — do not touch these, they are unrelated and still needed:**
`src/Game/Instantiation/PrimitiveGpuCatalog.cpp`'s
`PrimitiveGpuCatalog::EnsureDefaultPipeline()` independently loads the exact
same `shaders/Triangle.vert.spv`/`Triangle.frag.spv` pair for its own,
completely separate default pipeline used by every primitive spawned via
`Game::CreatePrimitiveEntity()` ("Create 3D Object"). This means:
- `src/Shaders/Triangle.vert`/`Triangle.frag` (the actual GLSL source files)
  must NOT be deleted.
- `CMakeLists.txt`'s `gte_add_shader(GreatTamanaEngine src/Shaders/Triangle.vert)`
  / `gte_add_shader(GreatTamanaEngine src/Shaders/Triangle.frag)` calls (near
  the bottom of the file, right after `add_executable(GreatTamanaEngine ...)`)
  must NOT be removed.

## Step 3: The Plan (A very detailed strategy)

### 3.1 — Rewrite `Game.cpp`'s `EnsureDemoSceneBuilt()` into `EnsureDefaultCameraExists()`

Replace the ENTIRE current body of `Game::EnsureDemoSceneBuilt()`
(`src/Game/Game.cpp`) with a narrower function that only creates the Camera.
Rename it to `Game::EnsureDefaultCameraExists()` — the old name is no longer
accurate once it builds no "demo scene" at all. New body:

```cpp
void Game::EnsureDefaultCameraExists()
{
    if (m_defaultCameraEnsured) {
        return;
    }
    m_defaultCameraEnsured = true;

    // The engine's one auto-created entity: a Camera sitting back along -Z
    // (an identity rotation looks straight down +Z - see
    // Camera::ViewMatrix(), ECS/Components/Camera.h) so a brand-new (or
    // freshly-loaded - see task_manager/scene-serialization-1/) scene always
    // has something to actually look through in "Scene"/"Game", instead of
    // falling back to RenderSystem::ResolveActiveCameraViewProjection()'s
    // Mat4::Identity() default. This is now the ONLY thing this function
    // does - it used to also build 3 hardcoded demo triangles proving the
    // ECS -> RenderSystem -> Renderer pipeline end to end; that scaffolding
    // is gone now that real scene content (primitives, imported meshes, and
    // - as of task_manager/scene-serialization-1/ - a real save/load loop)
    // exists instead. See TODO.md's "Scene serialization" entry.
    const Entity cameraEntity = m_registry.CreateEntity();
    Transform& cameraTransform = m_registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3{ 0.0f, 0.0f, -5.0f };
    m_registry.AddComponent<Camera>(cameraEntity);
}
```

Notes:
- This function takes no `Renderer&` parameter any more (nothing it does
  needs one) — update its declaration in `Game.h` to match (see 3.2).
- Remove the now-unused `#include "../Renderer/Vertex.h"` from `Game.cpp`'s
  include list (line 003) only if nothing else in the file still needs
  `Vertex` — confirm by checking the rest of `Game.cpp` after this edit; if
  nothing else references `Vertex`, delete that include. `#include "ECS/Components/Camera.h"`
  (line 004) and `#include "ECS/Components/Transform.h"` (line 006) are still
  needed (the Camera entity above uses both) — keep them.

### 3.2 — Update `Game::Render()`'s call site

`Game::Render()` (`src/Game/Game.cpp`, currently lines 152–163) currently
calls `EnsureDemoSceneBuilt(renderer);`. Change this single call to
`EnsureDefaultCameraExists();` (no `renderer` argument, per 3.1).

### 3.3 — Update `Game.h`'s declaration + members

In `src/Game/Game.h`:
- Replace the private method declaration (currently around line 217)

  ```cpp
  void EnsureDemoSceneBuilt(Renderer& renderer);
  ```

  with

  ```cpp
  void EnsureDefaultCameraExists();
  ```

  and replace its doc comment (currently lines 206–216, describing "three
  entities sharing one triangle Mesh/Pipeline...") with a short comment
  matching the new, narrower behavior — mirror the doc comment written into
  the function body in 3.1 above (keep both in sync).

- Remove the two now-dead private members (currently lines 236–239):

  ```cpp
  bool m_demoSceneBuilt = false;
  PipelineHandle m_demoPipeline;
  ```

  Replace with a single renamed guard flag:

  ```cpp
  bool m_defaultCameraEnsured = false;
  ```

- `PipelineHandle` may become an unused include/type in `Game.h` after this
  change — check whether `Game.h` still needs `Renderer/PipelineHandle.h`
  for any other reason (it currently does NOT `#include` it directly; it
  only ever used the type via `RenderSystem.h`'s own transitive include, so
  no include-list change is needed here — just confirm this by checking
  `Game.h`'s current `#include` list has no direct `PipelineHandle.h`
  inclusion to remove).

### 3.4 — Update `Game.h`'s class-level comment

The class comment block near the top of `Game.h` does not itself mention
the demo scene in a way that needs changing (it describes the class as a
"thin composition root" in general terms) — leave it as-is. Only the
per-method doc comment from 3.3 needs rewriting.

### 3.5 — Confirm no other call site references the removed symbols

Search the codebase (`Game::EnsureDemoSceneBuilt`, `m_demoSceneBuilt`,
`m_demoPipeline`) to confirm `Game.cpp`/`Game.h` were the only two files
touching them — based on the investigation for this campaign, they were.
`src/Editor/Panels/HierarchyPanel.cpp` and every other Editor panel only
ever call `Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`/
`GetRegistry()`, never the removed function/members directly, so no Editor
file needs a change in this phase.

### 3.6 — Documentation cleanup (same change, not a separate phase)

`Game.h`'s doc comment for `CreatePrimitiveEntity()` (around line 94–108)
references "The Editor's 'Hierarchy' right-click 'Create 3D Object' menu...
is just the first caller of it" — this remains accurate and needs no
change. No other doc comment in `Game.h`/`Game.cpp` references the removed
triangle demo by name once 3.1–3.3 are applied — do a final read-through of
the edited file to confirm no stray comment still says "three entities
sharing one triangle Mesh/Pipeline" anywhere.

## Step 4: What We Will NOT Do (Focus)

- We will **not** remove or modify `src/Shaders/Triangle.vert`/`.frag`, or
  their `gte_add_shader()` CMake registration — `PrimitiveGpuCatalog` still
  depends on them (see Step 2's guardrail).
- We will **not** touch `PrimitiveGpuCatalog.h/.cpp`, `EntityBlueprint.h`, or
  `EntityInstantiator.*` in this phase — those changes belong to PHASE2.
- We will **not** add any new scene-object-creation code here — this phase
  is a pure deletion/rename, nothing new is spawned beyond the one Camera
  that already existed.
