# PHASE1 — `EditorPanelCatalog.h` Relocation: `src/Editor/` → `src/Core/`

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it in full first, especially
Locked Design Decision #4.

## Step 1: The Goal (Where are we going?)

`src/Editor/EditorPanelCatalog.h` (the fixed list of known Editor panel
names, e.g. `"Hierarchy"`, `"Inspector"`, `"Scene"`, `"Game"`, ...) physically
lives under `src/Editor/` today, even though:
- It is deliberately ImGui/SDL/Vulkan-free (its own header comment says so).
- It is already compiled directly into `gte_core`'s own unconditional
  `target_sources()` list in the root `CMakeLists.txt` (confirm this with
  `search_in_dir` before touching anything — as of this strategy's own
  writing it is the line reading `src/Editor/EditorPanelCatalog.h` inside the
  `add_library(gte_core STATIC ...)` block, positioned right after the
  `Application/*CommandBridge.*` entries and right before the
  `NullEditorLayer.cpp` comment block).
- `src/Network/NetworkRoutes.h` (destined for `gte_core`) directly
  `#include`s it — a real, live violation of "`gte_core` never includes
  anything under `src/Editor/` except `EditorLayer.h`".

The goal of this phase: move this ONE file to `src/Core/EditorPanelCatalog.h`
— same content, same namespace, same symbol names, byte-for-byte identical
except its own file path — and fix every `#include`/path-reference at every
call site. Zero behavior change. This is the lowest-risk phase in the whole
campaign; treat it as a warm-up before PHASE2's much larger surgery.

## Step 2: The Situation (Where are we now?)

Before touching anything, run `search_in_dir` (path:
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\src`, content:
`EditorPanelCatalog.h`) and a second one over
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\tests` — re-confirm every
match below is still accurate (file paths/line numbers may have drifted
since this strategy was written; always trust the fresh search over this
document's own line numbers):

Real `#include` statements that must change (the file's own physical
dependency, not just a comment mentioning it):
- `src/Network/NetworkRoutes.h`: `#include "../Editor/EditorPanelCatalog.h"`
  → `#include "../Core/EditorPanelCatalog.h"`
- `src/Editor/DockLayout.cpp`: `#include "EditorPanelCatalog.h"` (a
  same-directory include today, since both files lived under `src/Editor/`)
  → `#include "../Core/EditorPanelCatalog.h"`

Doc-comment-only mentions of the OLD path that should be updated to the NEW
path for accuracy (these do not affect compilation, but leaving them stale
would mislead the next reader — fix them as part of this same phase, do not
leave a "TODO fix comment" behind):
- `src/Editor/DockLayout.h`
- `src/Editor/EditorLayer.h`
- `src/Editor/Panels/FrameDebuggerPanel.h`
- `src/Network/NetworkServer.cpp` (comment only, near
  `RegisterRoutes()`'s `/list_tabs` route)
- `src/Application/EditorUiCommandBridge.h`
- `tests/Editor/EditorPanelCatalogTests.cpp` (this one's comment references
  the OLD `if(GTE_ENABLE_EDITOR)` macro too, which no longer exists anywhere
  in this codebase since `editor-core-separation-1` Phase 8 — feel free to
  simplify/correct that stale reference while you are already editing this
  comment, but do not otherwise change this test file's behavior)
- `tests/Network/ActivateTabEndpointEndToEndTests.cpp` — this one has a REAL
  `#include "Editor/EditorPanelCatalog.h"` (not just a comment) that must
  become `#include "Core/EditorPanelCatalog.h"`.

Also re-run `search_in_dir` for the literal string `EditorPanelCatalog.h`
across the WHOLE repo (`src/` and `tests/` and `CMakeLists.txt`/
`tests/CMakeLists.txt`) as your own final check before moving on to Step 3,
to catch anything this list missed.

## Step 3: The Plan — exact steps

1. Read `src/Editor/EditorPanelCatalog.h` in full (already read once during
   this campaign's own strategy research — re-read it yourself to confirm
   current content).
2. Create `src/Core/EditorPanelCatalog.h` with the EXACT same content as
   `src/Editor/EditorPanelCatalog.h`, except:
   - Update its own header comment's claim that it is "a SECOND explicit,
     documented exception [to living under src/Editor/]" — that sentence
     becomes simply wrong/obsolete once the file no longer lives under
     `src/Editor/` at all. Rewrite that paragraph to state plainly: this file
     lives under `src/Core/` because it is pure, `gte_core`-owned compile-time
     data (the canonical list of known Editor panel names) that BOTH
     `gte_core`-tier code (`src/Network/NetworkRoutes.h`, for
     `GET /activate_tab`/`GET /list_tabs`) and `gte_editor`-tier code
     (`src/Editor/DockLayout.cpp`, for building the default dock layout)
     need to agree on — the single source of truth belongs in `gte_core`
     precisely because it must be reachable from both tiers, and only
     `gte_core` is visible from both. Do not otherwise reword anything
     else in the header comment or the file body (the panel name list, the
     `kKnownEditorPanelNames`/`kKnownEditorPanelNameCount`/
     `IsKnownEditorPanelName()` symbols, the `GTE_ENABLE_PROJECT_PANEL`
     `#if` guard around `"Project"`) — these stay byte-for-byte identical.
3. Delete `src/Editor/EditorPanelCatalog.h` (the old file).
4. Update `src/Network/NetworkRoutes.h`'s `#include` line.
5. Update `src/Editor/DockLayout.cpp`'s `#include` line.
6. Update every doc-comment-only mention listed in Step 2 above to say
   `Core/EditorPanelCatalog.h` instead of `Editor/EditorPanelCatalog.h`
   (or `EditorPanelCatalog.h` alone, where the comment didn't include a
   directory prefix — add `Core/` for clarity while you're there).
7. Update `tests/Network/ActivateTabEndpointEndToEndTests.cpp`'s real
   `#include` line.
8. Update `tests/Editor/EditorPanelCatalogTests.cpp`'s comment (Step 2 above)
   — its actual `#include "Editor/EditorPanelCatalog.h"` line ALSO needs to
   become `#include "Core/EditorPanelCatalog.h"` (re-confirm via
   `search_in_dir` — this file was flagged by this campaign's own research
   as having a real `#include`, not just a comment, of the old path).
9. In the root `CMakeLists.txt`, find the line
   `src/Editor/EditorPanelCatalog.h` inside `gte_core`'s own
   `target_sources()` list (re-confirm the exact line via `search_in_dir`
   first) and change it to `src/Core/EditorPanelCatalog.h`. Do NOT move this
   entry to a different position in the list unless you have a specific
   reason to (e.g. grouping it near `Core/Logging.h`/`Core/EditorCapabilities.h`
   is fine and arguably clearer, but is not required — use your judgement,
   this is purely cosmetic).
10. Leave `tests/CMakeLists.txt` untouched UNLESS it references the OLD path
    explicitly by string (re-confirm via `search_in_dir` for
    `EditorPanelCatalogTests.cpp` inside `tests/CMakeLists.txt` — the test
    FILE's own name/location under `tests/Editor/` does not need to move as
    part of this phase; only its internal `#include` changes).

## Step 4: Compile check

1. `cmake --build build --target gte_core` — must succeed with zero errors.
   This is the target that most directly exercises this phase's own change
   (`NetworkRoutes.h`'s new `#include` path).
2. `cmake --build build --target gte_editor` — must succeed with zero
   errors (exercises `DockLayout.cpp`'s new `#include` path).
3. Do NOT attempt the full `GreatTamanaEditor`/`GreatTamanaEngineTests` link
   yet if you want to keep this phase's own check maximally fast/isolated —
   but if you do attempt it (e.g. because you also want a quick
   `run_app_background` smoke check), it should still succeed, since this
   phase changes zero linkage, only `#include` paths. If it does NOT
   succeed, that is a signal this phase's own edit broke something — do not
   assume it is "just PHASE2/PHASE3's problem", since this phase alone
   should never break the link.
4. Optional but encouraged: `run_app_background` the built executable, then
   `gte_send_request` a `GET /list_tabs` and a
   `GET /activate_tab?name=Hierarchy` — both routes are the real, live
   consumers of `EditorPanelCatalog.h`'s content; a correct relocation must
   leave their JSON responses byte-for-byte identical to before this phase.
   `stop_app_background` when done.

## Step 5: Wrap-up

- Write `PHASE1_COMPLETION_REPORT.md` into this same folder: what was moved,
  every file touched (the exact list, confirmed against what you actually
  changed, not just this plan's prediction), the compile-check result, and
  the `GET /list_tabs`/`GET /activate_tab` smoke-check result if you ran one.
- `git_add` + `git_commit` (code + report together, one commit).
- If you hit a genuine ambiguity not resolved by this document, call
  `ask_questions` before guessing.
