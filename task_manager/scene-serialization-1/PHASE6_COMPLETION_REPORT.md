# PHASE6 — Completion Report: `File > Save Scene` (Ctrl+S) / `File > Open Scene` (Ctrl+O)

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full plan and `PHASE6_FILE_MENU_SAVE_OPEN_CTRL_SHORTCUTS.md` for this
phase's own detailed work order. This phase is now **complete**, and is the
**final** phase of the campaign.

## What was done

Followed the phase document's plan (v2) exactly:

1. **`src/Editor/DockLayout.h`** — widened `BuildDockspaceAndMenuBar()`'s
   signature to `(EditorContext& ctx, Game& game, Renderer& renderer)`, added
   forward declarations `class Game;`/`class Renderer;` alongside the
   existing `struct EditorContext;`, and updated the doc comment to mention
   the new `File > Save Scene`/`Open Scene` (Ctrl+S/Ctrl+O) handling.
2. **`src/Editor/EditorContext.h`** — added `#include <chrono>` (kept the
   existing `<string>`), and three new fields right after `exitRequested`:
   `std::string sceneIoStatusMessage`, `bool sceneIoStatusIsError = false`,
   `std::chrono::steady_clock::time_point sceneIoStatusSetTime` — mirroring
   `ProjectPanel`'s own private status-message pattern, surfaced through
   `EditorContext` since `DockLayout.cpp`'s `BuildDockspaceAndMenuBar()` is a
   free function with no persistent members of its own.
3. **`src/Editor/DockLayout.cpp`**:
   - Added `#include "SceneIO.h"` (PHASE5's `SaveScene()`/`LoadScene()`/
     `DefaultScenePath()`) and `#include "../Game/Game.h"` (for the `Game&`
     parameter), plus `#include <chrono>`/`#include <string>`.
   - Widened the function definition to match the new signature.
   - Added a small local lambda, `reportStatus(bool ok, okMessage,
     failMessage)`, that stamps `ctx.sceneIoStatusMessage`/
     `sceneIoStatusIsError`/`sceneIoStatusSetTime` — shared by all four
     Save/Open call sites (2 menu items + 2 keyboard shortcuts) so the
     bookkeeping is never duplicated, per the phase document's own
     instruction.
   - Inside the `File` menu, added `"Save Scene"` (`Ctrl+S` shown as the
     shortcut label) and `"Open Scene"` (`Ctrl+O`) `MenuItem`s **before** the
     existing `"Exit"` item, plus a `Separator()` between them and `Exit`.
   - Immediately after the menu bar (still inside the `"EditorDockSpaceHost"`
     window, so the shortcut works whether or not the `File` menu is open),
     added a global Ctrl+S/Ctrl+O keyboard-shortcut check gated on
     `!ImGuiIO::WantTextInput && io.KeyCtrl`, using
     `ImGui::IsKeyPressed(ImGuiKey_S/_O, /*repeat=*/false)`.
   - Added a short-lived colored status line (green when
     `!sceneIoStatusIsError`, red when it is, auto-clearing after 4 seconds —
     `kSceneIoStatusLifetime`), rendered right before `ImGui::End()`, mirroring
     `ProjectPanel::Build()`'s own identical pattern exactly.
4. **`src/Editor/ImGuiEditorLayer.cpp`** — updated the one call site
   (`BuildUI()`, was line 436) from `BuildDockspaceAndMenuBar(m_ctx);` to
   `BuildDockspaceAndMenuBar(m_ctx, game, renderer);` — both were already
   local parameters of `BuildUI()`, no other change needed.
5. **`CMakeLists.txt`/`tests/CMakeLists.txt`** — no changes needed; every file
   this phase touches was already registered (confirmed per the phase
   document's own step 3.5).
6. **`TODO.md`** — rewrote the "Scene serialization (save/load a scene to/from
   a file, e.g. JSON)" bullet using the `~~struck-through~~ - DONE, ...`
   convention every other completed roadmap item already uses, summarizing
   what actually shipped (primitive + asset-instance objects only, root
   Transform/Name, hardcoded `TestScene.gtscene` text format, `File >
   Save`/`Open` + Ctrl+S/Ctrl+O) and explicitly naming what remains
   deliberately NOT done (file picker/"Save As", per-child Transform
   overrides, Camera/physics/animation-state persistence, undo/redo,
   multi-scene), pointing at `PHASE0_MASTER_STRATEGY.md` for the full
   writeup.
7. **`agents.md`** — added the optional (step 3.7) new "Scene Serialization"
   section, right before "Editor Module Structure", naming the three rules
   future contributors must not violate: (1) `Scene/SceneBuilder.h` only
   walks ROOT entities and only recognizes `PrimitiveSource`/
   `MeshAssetSource`; (2) `Editor/SceneIO.cpp`'s `AssetDatabase` is always a
   fresh, throwaway scan, never cached; (3) `Scene/SceneTextFormat.cpp`'s
   `kind == Asset` validation happens once, at each object's `END` line; plus
   a fourth rule this session added covering the "only a ROOT entity's
   Transform/Name round-trips" design decision, since that's exactly the kind
   of thing a future contributor could accidentally "fix" without re-reading
   PHASE0's own Design Decision #3.
8. **`readme.md`** — appended a new "Status" bullet (step 3.8, v2) right
   before the closing "## Roadmap" section, describing what actually shipped:
   the hardcoded demo triangle scene is gone (confirmed the real function
   name is `Game::EnsureDefaultCameraExists()`, per PHASE1's own completion
   report), replaced by the `src/Scene/` module + `File > Save Scene`/`Open
   Scene` (Ctrl+S/Ctrl+O) loop, with the exact same scope caveats (ROOT-only
   Transform/Name round-trip, no Camera/physics/animation-state persistence,
   no file picker/"Save As"/undo-redo yet) called out. Left every earlier
   "Status" entry describing the demo triangles completely untouched, per the
   phase document's own "What We Will NOT Do".

## Verification

- **Fast compile check**: `cmake --build build --target gte_core` — clean
  build. `cmake --build build --target GreatTamanaEngineTests` — clean build
  (no new test file this phase; confirms the widened `DockLayout.cpp`/
  `ImGuiEditorLayer.cpp` signature change didn't break anything the test
  binary links against).
- **Full build + full regression test** (this task's own workflow rules
  explicitly carve out an exception for "the last one" in a multi-phase
  campaign, and PHASE0/PHASE5 both name this phase as the one expected to run
  it):
  - `cmake --build build` (no target filter) — **clean build** of every
    target, including the real `GreatTamanaEngine.exe`.
  - `ctest -C Debug --output-on-failure` from `build/` — **1012/1012 tests
    passed** (1 pre-existing, machine-gated smoke test —
    `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` —
    skipped, exactly as every prior phase's own regression run also reported).
    No regressions introduced by this phase's changes.

## Notes / deviations from the phase document

None of substance — every step (3.1–3.8) was followed as written. One small,
deliberate simplification: the phase document's illustrative code sample
repeats the same `okMessage`/`failMessage` construction inline at all four
call sites (2 `MenuItem`s + 2 keyboard shortcuts); this was implemented
exactly that way (the `reportStatus` lambda only owns the three-field
bookkeeping, not the `SaveScene()`/`LoadScene()` call itself), matching the
phase document's own explicit instruction to "keep the actual
`SaveScene()`/`LoadScene()` call itself at each of the four call sites, not
hidden inside the lambda."

## Campaign status

This was the **last** phase of the `scene-serialization-1` campaign. All six
phases (removing the hardcoded demo triangles, the `PrimitiveSource`
component + `DestroyEntityAndDescendants()`, the `*.gtscene` text format, the
`SceneBuilder` ECS↔AssetDatabase bridge, the `SceneIO`/`ProjectRootPath`
Editor glue, and this phase's `File > Save Scene`/`Open Scene` menu wiring)
are complete, compiling cleanly, and passing the full regression test suite
(1012 tests, 1 pre-existing machine-gated skip) with a full, non-filtered
build. See `PHASE0_MASTER_STRATEGY.md` for the full six-phase writeup and
`readme.md`'s own newest "Status" bullet for the user-facing summary of what
shipped.
