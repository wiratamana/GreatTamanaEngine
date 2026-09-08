# PHASE6 — `File > Save Scene` (Ctrl+S) / `File > Open Scene` (Ctrl+O) (v2)

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`.
Depends on PHASE5's `SaveScene(Game&)`/`LoadScene(Game&, Renderer&)`. This is
the final phase of the campaign.

**Revision note (v2):** the second-iteration audit (see
`PHASE0_MASTER_STRATEGY.md`) found that no phase in v1 ever updated
`README.md`, even though `README.md`'s own "Status" section describes the
hardcoded demo triangles PHASE1 removes as CURRENT, present-tense behavior —
and every prior campaign in this repository closed out with a fresh, dated
`README.md` "Status" bullet. Section 3.8 below is new in v2 and fixes this;
sections 3.1–3.7 are unchanged from v1.

## Step 1: The Goal (Where are we going?)

Wire PHASE5's `SaveScene()`/`LoadScene()` into the Editor's actual top menu
bar: a new `File > Save Scene` item (with a `Ctrl+S` keyboard shortcut) and
a new `File > Open Scene` item (with a `Ctrl+O` keyboard shortcut) —
completing PHASE0's Design Decision #2 (a real, user-facing Load, not just
an internal function). Add minimal, low-risk status feedback so clicking
either one is not a silent, unobservable action. Close out the two
remaining loose ends: the call site that needs a wider function signature,
the stale `TODO.md` prose this campaign supersedes, and (as of v2) the
stale `README.md` prose describing the now-removed demo scene.

## Step 2: The Situation / The Problem (Where are we now?)

- `src/Editor/DockLayout.h`'s `BuildDockspaceAndMenuBar(EditorContext& ctx)`
  (the only function this file declares) currently takes just an
  `EditorContext&` — it has no way to reach `Game`/`Renderer` today, both of
  which PHASE5's `SaveScene()`/`LoadScene()` need.
- `src/Editor/DockLayout.cpp`'s `BuildDockspaceAndMenuBar()` body (currently
  lines 111–198) builds exactly one `File` menu item today: `"Exit"` (lines
  130–139), setting `ctx.exitRequested = true`.
- `src/Editor/ImGuiEditorLayer.cpp`'s `BuildUI(Game& game, Renderer& renderer,
  const rg::RenderGraph& renderGraph)` (the ONE call site of
  `BuildDockspaceAndMenuBar()`, currently line 436:
  `BuildDockspaceAndMenuBar(m_ctx);`) already has both `game` and `renderer`
  in scope as local parameters — updating this one call site is all that's
  needed once the signature changes.
- `src/Editor/EditorContext.h` already has a precedent for "a short-lived,
  colored status message" — not on `EditorContext` itself today (that
  pattern currently lives privately inside `ProjectPanel`'s own
  `m_statusMessage`/`m_statusIsError`/`m_statusSetTime` fields,
  `SetStatus()`/its render block in `ProjectPanel::Build()`) — but the exact
  same shape is easy to add to `EditorContext` for this campaign's own use,
  since `DockLayout.cpp` (not a class with its own persistent members) needs
  somewhere to keep it across frames.
- **(v2) `README.md`'s "Status" section describes the demo triangles in the
  present tense.** E.g. (paraphrased, see the actual file for exact
  wording): *"`Game` builds a small demo scene (three entities sharing one
  mesh/pipeline, positioned via `Transform` alone, plus one `Camera` entity
  sitting back along -Z looking at them) proving the whole ECS ->
  `RenderSystem` -> `Renderer` pipeline end to end"*, and later, describing
  `CreatePrimitiveEntity()`: *"reusing one shared `Pipeline` and one shared
  `Mesh` per shape across every instance, exactly like the existing demo
  triangles share theirs."* Both become inaccurate the moment PHASE1 lands.
  `TODO.md`'s own opening paragraph states the house rule this violates:
  `README.md` stays focused on describing the architecture/status *as it
  exists today* — and every prior campaign (Render Graph, GPU Vertex
  Skinning, the Job System, the Memory Profiler work, ...) closed out with a
  fresh, bold-lead-sentence "Status" bullet summarizing what shipped, rather
  than leaving stale prose behind for the next reader to trip over.

## Step 3: The Plan (A very detailed strategy)

### 3.1 — Widen `BuildDockspaceAndMenuBar()`'s signature

In `src/Editor/DockLayout.h`, change the one declaration from:

```cpp
void BuildDockspaceAndMenuBar(EditorContext& ctx);
```

to:

```cpp
void BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer);
```

Add forward declarations `class Game;` and `class Renderer;` near the
existing `struct EditorContext;` forward declaration at the top of this
file. Update the function's own doc comment to mention it now also handles
`File > Save Scene`/`Open Scene` (Ctrl+S/Ctrl+O), alongside the existing
`Exit` menu item.

### 3.2 — Add status-feedback fields to `EditorContext`

In `src/Editor/EditorContext.h`, add three new fields (a natural spot is
right after the existing `bool exitRequested = false;` field, since it's
conceptually adjacent "menu-bar-driven state"):

```cpp
    // Short-lived, colored status feedback for File > Save Scene/Open Scene
    // (DockLayout.cpp's BuildDockspaceAndMenuBar(), Ctrl+S/Ctrl+O too) -
    // mirrors ProjectPanel's own private SetStatus()/m_statusMessage
    // pattern (Panels/ProjectPanel.cpp), just surfaced through EditorContext
    // instead, since DockLayout.cpp's BuildDockspaceAndMenuBar() is a free
    // function with no persistent members of its own to keep this in.
    // Empty means "nothing to show right now".
    std::string sceneIoStatusMessage;
    bool sceneIoStatusIsError = false;
    std::chrono::steady_clock::time_point sceneIoStatusSetTime;
```

Add `#include <chrono>` and `#include <string>` to `EditorContext.h`'s
include list if not already present (confirm — the current file already
includes `<string>` for other fields; `<chrono>` is new).

### 3.3 — Add the two new `File` menu items + keyboard shortcuts in `DockLayout.cpp`

In `src/Editor/DockLayout.cpp`:

- Add `#include "../Game/Game.h"` (for the `Game&` parameter — needed since
  this file didn't depend on `Game` before) and `#include "SceneIO.h"`
  (PHASE5's `SaveScene()`/`LoadScene()`) to the top of the file, alongside
  the existing `#include "EditorContext.h"`.
- Change the function definition's signature to match 3.1:
  `void BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer)`.
- Inside the existing `if (ImGui::BeginMenu("File")) { ... }` block
  (currently lines 130–140), add two new `MenuItem` calls **before** the
  existing `"Exit"` item, plus a `Separator()` between them and `Exit` for
  visual grouping:

  ```cpp
  if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
          const bool saved = SaveScene(game);
          ctx.sceneIoStatusMessage = saved
              ? ("Saved scene to " + DefaultScenePath().string())
              : "Failed to save scene.";
          ctx.sceneIoStatusIsError = !saved;
          ctx.sceneIoStatusSetTime = std::chrono::steady_clock::now();
      }
      if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
          const bool loaded = LoadScene(game, renderer);
          ctx.sceneIoStatusMessage = loaded
              ? ("Loaded scene from " + DefaultScenePath().string())
              : "Failed to load scene (missing or malformed file).";
          ctx.sceneIoStatusIsError = !loaded;
          ctx.sceneIoStatusSetTime = std::chrono::steady_clock::now();
      }
      ImGui::Separator();
      // Simple example of a menu item that exits the application
      // programmatically: sets ctx.exitRequested, which
      // ImGuiEditorLayer::WantsExit() checks once per frame, rather
      // than calling exit()/SDL_Quit() directly here, so shutdown
      // still goes through Application's normal RAII teardown.
      if (ImGui::MenuItem("Exit")) {
          ctx.exitRequested = true;
      }
      ImGui::EndMenu();
  }
  ```

- Immediately after the `ImGui::EndMenuBar();` call (currently line 141) —
  i.e. still inside the `"EditorDockSpaceHost"` window, so the keyboard
  shortcut works whether or not the `File` menu is currently open — add a
  global shortcut check:

  ```cpp
  {
      ImGuiIO& io = ImGui::GetIO();
      // WantTextInput guards against a user typing a literal 's'/'o' into
      // an unrelated ImGui text field (e.g. renaming something, once that
      // exists) from being misread as this shortcut - the same defensive
      // pattern IEditorLayer::WantsCaptureKeyboard() already establishes
      // for gameplay input (see AGENTS.md, "Editor Module Structure").
      if (!io.WantTextInput && io.KeyCtrl) {
          if (ImGui::IsKeyPressed(ImGuiKey_S, /*repeat=*/false)) {
              const bool saved = SaveScene(game);
              ctx.sceneIoStatusMessage = saved
                  ? ("Saved scene to " + DefaultScenePath().string())
                  : "Failed to save scene.";
              ctx.sceneIoStatusIsError = !saved;
              ctx.sceneIoStatusSetTime = std::chrono::steady_clock::now();
          }
          if (ImGui::IsKeyPressed(ImGuiKey_O, /*repeat=*/false)) {
              const bool loaded = LoadScene(game, renderer);
              ctx.sceneIoStatusMessage = loaded
                  ? ("Loaded scene from " + DefaultScenePath().string())
                  : "Failed to load scene (missing or malformed file).";
              ctx.sceneIoStatusIsError = !loaded;
              ctx.sceneIoStatusSetTime = std::chrono::steady_clock::now();
          }
      }
  }
  ```

  Factor the repeated "run an action, then stamp `ctx.sceneIo*`" four-line
  block above into one small local lambda (e.g. `auto reportStatus = [&ctx](bool ok, std::string okMessage, std::string failMessage) { ... };`)
  used by all four call sites (menu item ×2, shortcut ×2) — avoids writing
  the same bookkeeping four times; keep the actual `SaveScene()`/
  `LoadScene()` call itself at each of the four call sites, not hidden
  inside the lambda, so the menu-vs-shortcut origin stays easy to read.

- Optionally (small, cheap polish — matches this codebase's existing UX
  conventions, but not load-bearing for the feature to work): render
  `ctx.sceneIoStatusMessage` as a short-lived colored line, e.g. right after
  the existing `ImGui::DockSpace(...)` call and before `ImGui::End()`
  (currently around line 145–146), reusing the exact same "still within
  `kStatusMessageLifetime`-equivalent window, then auto-clear" pattern
  `ProjectPanel::Build()` already establishes (`std::chrono::milliseconds`
  comparison against `ctx.sceneIoStatusSetTime`, green text when
  `!sceneIoStatusIsError`, red when it is).

### 3.4 — Update `ImGuiEditorLayer.cpp`'s call site

In `src/Editor/ImGuiEditorLayer.cpp`'s `BuildUI()` method (currently line
436):

```cpp
BuildDockspaceAndMenuBar(m_ctx);
```

becomes:

```cpp
BuildDockspaceAndMenuBar(m_ctx, game, renderer);
```

`game`/`renderer` are already this method's own parameters — no other
change needed at this call site.

### 3.5 — `CMakeLists.txt`: no new files this phase

Every file this phase touches (`DockLayout.h/.cpp`, `EditorContext.h`,
`ImGuiEditorLayer.cpp`) is already registered in the root `CMakeLists.txt`'s
`if(GTE_ENABLE_EDITOR)` block — no new `target_sources()` entry is needed.

### 3.6 — `TODO.md` cleanup

In `TODO.md`'s "Engine Roadmap (not yet started)" section, the existing
"Scene serialization (save/load a scene to/from a file, e.g. JSON)" bullet
(and its prose about `Game.cpp`'s "own demo entities remain hardcoded in
C++") is now stale on two counts: the demo entities are gone (PHASE1), and a
first save/load loop now exists (PHASES 2–6). Rewrite this bullet using the
same `~~struck-through~~ - DONE, see...` convention every other completed
roadmap item in this same file already uses (e.g. "~~Transform parenting /
hierarchy~~ - DONE", "~~PMX material/texture import~~ - DONE"), briefly
summarizing what shipped (primitive + asset-instance objects only, root
Transform + asset Guid, hardcoded `TestScene.gtscene` text format, `File >
Save`/`Open` + Ctrl+S/Ctrl+O) and explicitly naming what remains a
deliberate, NOT-yet-done follow-up (arbitrary save path/file picker,
per-child Transform overrides, Camera/physics/animation-state persistence,
undo/redo, multi-scene) — pointing at
`task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md` for the full
writeup, the same cross-referencing convention this file already uses for
other campaigns (e.g. its Job System / GPU Vertex Skinning references).

### 3.7 — `AGENTS.md`: optional, small addition (not required, but consistent with convention)

`AGENTS.md` documents every other cross-cutting subsystem this codebase has
built (Job System, GPU Vertex Skinning, Profiling, ...) with its own section
explaining the conventions future code must follow. Consider adding a short
new "Scene Serialization" section (mirroring the existing section
structure) stating the two rules future contributors must not violate
without re-reading this campaign's own docs first: (1) `Scene/SceneBuilder.h`
only ever walks ROOT entities and only ever recognizes `PrimitiveSource`/
`MeshAssetSource` — a future third serializable "kind" needs a matching new
`SceneObjectKind` enumerator, a new `SceneTextFormat.cpp` key, and a new
branch in both `BuildSceneDocumentFromRegistry()`/`LoadScene()`'s spawn
dispatch, never a silent special case bolted on elsewhere; (2)
`Editor/SceneIO.cpp`'s `AssetDatabase` is always a fresh, throwaway scan,
never cached — a future change must not start persisting it across calls
without re-deriving whether that's still safe given `AssetDatabase`'s own
`FindByGuid()`/`FindByPath()` pointer-lifetime caveats (see
`AssetDatabase.h`'s own doc comments); and (3, added in the v2 audit)
`Scene/SceneTextFormat.cpp`'s `kind == Asset` requires a genuinely valid
`assetGuid` (checked once, at each object's `END`) — a future new
`SceneObjectKind` that also carries a `Guid` reference should apply the
exact same "validate at END, scoped to that one kind" pattern rather than
reintroducing v1's unscoped, per-line check this phase's own PHASE3 v2
revision removed. This step is genuinely optional polish, not required for
the feature to work — do it if time remains, skip it otherwise without
blocking on it.

### 3.8 — (v2, new) `README.md`: add a fresh "Status" bullet, and leave older prose as history

`README.md`'s "Status" section (near the bottom of the file, right before
the closing "## Roadmap" section that points at `TODO.md`) is a
chronological log — every previous campaign appended a NEW, bold-lead-
sentence bullet describing what it shipped, rather than rewriting or
deleting earlier entries (the earlier entries remain accurate as history —
e.g. "GPU memory allocation goes through VMA..." is still true, it's just no
longer the newest thing). Follow that exact same convention here: do **not**
rewrite the existing "`Game` builds a small demo scene (three entities
sharing one mesh/pipeline...)" or "exactly like the existing demo triangles
share theirs" sentences in place — leave them as an accurate description of
what the engine looked like AT THAT POINT in its history. Instead, append
one new bullet at the END of the "## Status" list (immediately before the
closing "## Roadmap" section), in the same style, e.g.:

> - **The hardcoded demo triangle scene is gone, replaced by a real (if
>   deliberately minimal) scene Save/Load loop.** `Game::EnsureDemoSceneBuilt()`'s
>   3 hardcoded triangle entities and their private `Pipeline`/`Mesh` (see
>   earlier "Status" entries above, and the "Entity-Component-System"
>   section's own now-historical description of them) are gone —
>   `Game::EnsureDefaultCameraExists()` now creates only the one default
>   `Camera` entity every fresh/just-loaded scene still needs. In their
>   place, a new, always-compiled `src/Scene/` module (`SceneDocument.h`,
>   `SceneTextFormat.h/.cpp`, `SceneBuilder.h/.cpp` — fully Tier-1-tested,
>   zero ECS/Renderer/filesystem dependency) implements a small, hand-rolled,
>   line-oriented TEXT file format (`*.gtscene` — deliberately not JSON, no
>   JSON library is vendored in this engine) that can serialize/restore the
>   two kinds of top-level scene object the Editor can currently create: a
>   built-in primitive (`Game::CreatePrimitiveEntity()`, by which
>   `PrimitiveType` it is — a new `PrimitiveSource` component) and an
>   imported-asset instance (`Game::CreateMeshEntityFromGtaFile()`, by its
>   stable `AssetDatabase` `Guid` — never a raw machine-local path, the same
>   principle `MaterialTextureRef::guid` already established). A new
>   `File > Save Scene` (Ctrl+S) / `File > Open Scene` (Ctrl+O) menu pair
>   (`src/Editor/SceneIO.h/.cpp`, `DockLayout.cpp`) reads/writes one
>   hardcoded path, `<Project folder>/TestScene.gtscene` — no file picker,
>   no "Save As", no undo/redo yet; only each object's ROOT entity's
>   Transform/Name round-trips (a multi-part asset's own child "submesh
>   part" entities are always freshly re-derived from the asset itself on
>   Load, never individually serialized), and Camera/physics/animation-
>   runtime state is intentionally not persisted at all. See
>   `task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md` for the
>   full six-phase writeup.

Adjust the exact wording/file list above only if the actual implementation
ends up naming something differently than planned here (e.g. if a phase's
own file/function name changed during implementation) — the point is that
this new bullet must describe what ACTUALLY shipped, not just restate this
plan verbatim. This is the same "describes the architecture/status as it
exists today" rule `TODO.md`'s own opening paragraph states as the reason
this kind of backlog content was moved OUT of `README.md` in the first
place — a `README.md` "Status" section that silently goes stale the moment
a described feature is removed/replaced defeats that rule just as much as
scattering TODO-style prose through it would.

## Step 4: What We Will NOT Do (Focus)

- We will **not** add a "File > New Scene" (empty-scene) menu item — outside
  this campaign's literal scope; `ClearSerializableSceneObjects()` (PHASE4)
  is available for a future one to build on, but wiring it to a menu item of
  its own is not part of this request.
- We will **not** add a confirmation/"unsaved changes" prompt before `Open`
  overwrites the current in-memory scene — matches this campaign's
  "keep it simple" scope throughout.
- We will **not** persist `ctx.sceneIoStatusMessage`/etc. across application
  restarts, or route it through `Profiling`/logging — it is purely
  transient, in-memory, single-session UI feedback.
- We will **not** (v2) rewrite/delete any EARLIER `README.md` "Status" bullet
  that mentions the demo triangles — those remain accurate, dated history;
  only a new bullet is appended (see 3.8 above), matching how every other
  past campaign has always updated this same section.
