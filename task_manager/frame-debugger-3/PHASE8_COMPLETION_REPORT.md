# PHASE8 — Integration, build wiring, docs, full regression, live automated end-to-end verification — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE8_INTEGRATION_BUILD_DOCS_AND_FULL_VERIFICATION.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1`..`PHASE7` (already landed).

## Summary

Implemented this phase's own "Step 3: The Plan" exactly as written: re-confirmed
every build-system wiring point named in Step 3.1 (found ZERO gaps — every
phase's own file already registered its sources correctly at landing time), did
the full documentation sweep from Step 3.2 (`docs/conventions/frame-debugger.md`
rewritten from scratch to describe the real system; `AGENTS.md`'s "Frame
Debugger" section updated and the word "scaffolding" dropped; a new top
`README.md` "Status" bullet added; `docs/README.md`'s index entry updated;
`TODO.md`'s "Frame Debugger (scaffolding)" section rewritten as "Frame
Debugger" with a "now DONE" list plus a "still genuinely deferred" list), ran
the full validation suite from Step 3.3 (full clean build both Editor
configurations, full `ctest` regression), and — the closing achievement of the
whole campaign — ran the exact 9-step, fully-automated, HTTP-driven,
screenshot-verified end-to-end smoke test from Step 3.4, closing the
manual-verification gap `frame-debugger-1`/`frame-debugger-2` both had to
accept.

**No production source code needed to change this phase** — every build-system
wiring point re-checked was already correct, matching the phase document's own
prediction ("No production source code should need to change in this phase
unless 3.1/3.3's re-confirmation pass finds a genuine, small integration bug").

## Step 3.1 — Build-system re-confirmation (found: no gaps)

Checked every source file added across PHASE1–PHASE7 against
`CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)` and
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, plus `cmake/CompileShaders.cmake`'s
handling of `Shaders/FrameDebuggerPreview.comp`:

- `src/Application/FrameDebuggerCommandBridge.h/.cpp` — present in the
  always-compiled (not `GTE_ENABLE_EDITOR`-gated) `gte_core` source list,
  right after `EditorUiCommandBridge.h/.cpp` — correct, since
  `FrameDebuggerCommandBridge` is a plain data/mutex/condvar type with no
  ImGui/Editor dependency, exactly mirroring its sibling bridge's own
  placement.
- `src/Editor/FrameDebuggerData.h/.cpp`, `FrameDebuggerCapture.h/.cpp`,
  `FrameDebuggerHistory.h/.cpp`, `FrameDebuggerPreviewProcessing.h/.cpp`,
  `Panels/FrameDebuggerPanel.h/.cpp` — all present inside the root
  `CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` `gte_core` source block — correct.
- `tests/Application/FrameDebuggerCommandBridgeTests.cpp` — present in
  `tests/CMakeLists.txt`'s always-compiled `GTE_TEST_SOURCES` list, right
  after `EditorUiCommandBridgeTests.cpp` — correct (mirrors the bridge's own
  unconditional placement).
- `tests/Editor/FrameDebuggerDataTests.cpp`, `FrameDebuggerCaptureTests.cpp`,
  `FrameDebuggerHistoryTests.cpp`, `FrameDebuggerSnapshotBuilderTests.cpp`,
  `FrameDebuggerPreviewProcessingTests.cpp` — all present inside
  `tests/CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block — correct.
- `src/Shaders/FrameDebuggerPreview.comp` — confirmed this codebase uses an
  explicit, hand-written `gte_add_shader(GreatTamanaEngine ...)` call per
  shader file (`cmake/CompileShaders.cmake`), never a glob — the shader's own
  call site exists, correctly wrapped in `if(GTE_ENABLE_EDITOR)` (its only real
  consumer, `FrameDebuggerPreviewProcessing.cpp`, is Editor-only), immediately
  after the `VolumeTexturePreview.comp` (unconditional) entry, with an
  explanatory comment already in place.
- `src/Network/NetworkRoutes.h/.cpp`, `src/Network/NetworkServer.h/.cpp`,
  `src/Application/Application.h/.cpp`, `src/Editor/EditorLayer.h`,
  `src/Editor/NullEditorLayer.cpp`, `src/Editor/ImGuiEditorLayer.cpp` — all
  pre-existing, always-compiled files PHASE7 modified in place (no new file
  registration needed for these).

**Result: zero gaps found.** No `CMakeLists.txt`/`tests/CMakeLists.txt` change
was needed this phase.

## Step 3.2 — Documentation sweep

- **`docs/conventions/frame-debugger.md` — completely rewritten.** Retired the
  "GUI SCAFFOLDING ONLY" framing and the "exact glue seams a future real-capture
  campaign should replace" section entirely (both are now history, not a
  forward-looking TODO). The new document is organized as: "What is real
  today" (capture reality, the permanent pass-level-granularity/Game-View-only
  design facts — explicitly cross-referencing `PHASE0_MASTER_STRATEGY.md`'s own
  Locked Design Decisions #1/#7 so a future reader never mistakes these for
  unfinished work — the real history ring buffer, real pass-scoped property
  reflection, real preview reconstruction, real Channels/Levels, and full HTTP
  automation), "The data model and panel" (an updated structural walkthrough,
  largely unchanged in SHAPE from `frame-debugger-2` but now describing real
  data flowing through it), "HTTP automation" (every one of the eight
  `/frame_debugger/*` routes, the main-viewport-pinning mechanism), and
  "Testing this feature" (pointing at every Tier-1 test file plus this
  phase's own live smoke-test evidence). The old "Manual verification
  limitation" section was removed entirely and replaced with an explicit
  statement that this gap is now CLOSED.
- **`AGENTS.md`'s "Frame Debugger (scaffolding)" section** — renamed to "Frame
  Debugger" (word "scaffolding" dropped from both the heading and the body),
  rewritten to describe the real capture/history/reflection/Channels-Levels/
  HTTP-automation system, mirroring "Time and Playback Pause"'s own
  tone/structure (a dense, single-paragraph summary of what's real today plus
  a link to the full convention doc) as instructed.
- **`README.md`'s "Status" section** — a new top-of-section bullet added
  (matching the prose density/style of the two most recent existing bullets
  read for tone — the Frame Debugger `frame-debugger-2` entry immediately
  below it, and the `network-impl-7` entry further down), describing this
  campaign's real result: real pass-level capture, the capture context, the
  history ring buffer, real Channels/Levels, the HTTP automation surface, and
  the final verification evidence (full clean build both configs, full
  `ctest`, and — called out explicitly since it is new for this feature — the
  first genuine HTTP-driven, screenshot-verified end-to-end smoke test).
- **`docs/README.md`'s "Frame Debugger" index entry** — updated to describe the
  real system (dropping "GUI-only scaffolding for a future..." framing),
  naming every file this campaign added (`FrameDebuggerCapture.h/.cpp`/
  `FrameDebuggerHistory.h/.cpp`/`FrameDebuggerPreviewProcessing.h/.cpp`) and
  the HTTP automation surface.
- **`TODO.md`'s "Frame Debugger (scaffolding)" section** — renamed to "Frame
  Debugger", restructured into two parts: a "now DONE" list (crossed out with
  `~~...~~`, matching this repository's own existing strikethrough convention
  seen elsewhere in `TODO.md`'s "Engine Roadmap" section) covering every item
  the `frame-debugger-2` campaign had explicitly deferred, plus a "Still
  genuinely deferred" list covering what remains a real, permanent design
  choice or genuine future work (per-draw-call granularity, Scene View/Present
  capture, a full generic shader-reflection system, a Pause/frozen-snapshot
  control that's redundant with snapshot-on-demand, and the optional
  `RenderGraphDebugTextureRegistry` publication that was never needed).

### A small correction made during the doc sweep (worth flagging)

While editing `README.md`'s "Status" section with `edit_line`, an initial pass
accidentally duplicated the opening lines of the pre-existing `frame-debugger-2`
bullet immediately below the new one (a copy-paste artifact from reading the
file for context). This was caught immediately by re-reading the file back,
and fixed with a follow-up `edit_line` call removing the 7 duplicated lines
before anything was staged/committed — the final file has exactly one copy of
each bullet, confirmed by a full re-read of the "Status" section end-to-end.
Flagged here per this task's own "clearly document any such deviation"
instruction, even though it was caught and fixed within the same editing
session and never reached a committed state.

## Step 3.3 — Full validation

1. **Full clean build, `GTE_ENABLE_EDITOR=ON`**: `cmake --build build
   --clean-first` — **441/441 steps, zero errors.** Every Frame-Debugger file
   from every phase (`FrameDebuggerCommandBridge.cpp`, `FrameDebuggerData.cpp`,
   `FrameDebuggerCapture.cpp`, `FrameDebuggerHistory.cpp`,
   `FrameDebuggerPreviewProcessing.cpp`, `Panels/FrameDebuggerPanel.cpp`,
   `NetworkRoutes.cpp`, `NetworkServer.cpp`, `Application.cpp`) compiled
   cleanly, `Shaders/FrameDebuggerPreview.comp` compiled to `.spv` with no
   GLSL errors, and both `GreatTamanaEngine.exe` and
   `GreatTamanaEngineTests.exe` relinked successfully.
2. **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: fresh configure
   (`cmake -S . -B build-editor-off -G Ninja -DGTE_ENABLE_EDITOR=OFF`, zero
   network access needed — every dependency already present on disk) followed
   by `cmake --build build-editor-off --clean-first` — **366/366 steps, zero
   errors.** Confirmed none of `FrameDebuggerData.cpp`/`FrameDebuggerCapture.cpp`/
   `FrameDebuggerHistory.cpp`/`FrameDebuggerPreviewProcessing.cpp`/
   `Panels/FrameDebuggerPanel.cpp`/`FrameDebuggerPreview.comp` were even
   attempted for compilation in this configuration (absent from the build
   log entirely), while `FrameDebuggerCommandBridge.cpp`/`NullEditorLayer.cpp`
   (with its eight new no-op stub overrides)/`NetworkRoutes.cpp`/
   `NetworkServer.cpp`/`Application.cpp` all compiled correctly — confirming
   the always-compiled/Editor-gated split this whole campaign carefully
   maintained (PHASE1's own "forward-declare in the header, `#if
   GTE_ENABLE_EDITOR`-guard the real `#include`/dereference in the .cpp"
   discipline) holds all the way through the final integration pass.
3. **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure` from
   `build/` — **100% of 1402 tests passed** (1 pre-existing, machine-gated
   skip, `PmxLoaderRealModelSmokeTest` — the same single skip every prior
   campaign in this repository has always had), 98.89 sec total, covering
   every `FrameDebuggerCapture`/`FrameDebuggerHistory`/
   `FrameDebuggerPreviewProcessing`/`FrameDebuggerSnapshotBuilder`/
   `FrameDebuggerData`/`FrameDebuggerCommandBridge` test plus every
   `ParseFrameDebugger*`/`BuildFrameDebugger*` `NetworkRoutesTests` case added
   across PHASE1–PHASE7. As an extra, not-strictly-required sanity check
   (this phase's own Step 3.3 only names the `build/` `ctest` run), also ran
   `ctest -C Debug --output-on-failure` from `build-editor-off/` —
   **100% of 1180 tests passed** as well, confirming the Editor-OFF
   configuration's own reduced test suite (no `GTE_ENABLE_EDITOR`-gated Frame
   Debugger tests compiled in at all) is equally green.

## Step 3.4 — Live, HTTP-automation-driven end-to-end smoke test

Launched the real `GreatTamanaEngine.exe` (from the freshly-rebuilt `build/`
directory) in the background via `run_app_background`, then drove the ENTIRE
feature over HTTP via `gte_send_request`, confirming visually via
`GET /get_swapchain` at every numbered step, exactly per this phase's own
Step 3.4 sequence:

1. **`GET /frame_debugger/open`** → `200`,
   `{"state":{"channel":"all","enabled":false,"historyCount":0,"historyCursor":0,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":0,"windowOpen":true},"success":true}`.
   **`GET /get_swapchain`** confirmed: the "Frame Debugger" window is visible,
   pinned inside the main viewport, titlebar visible, right where the default
   dockspace's own top-left work area begins — an ordinary programmatic open
   never escaped to its own OS-level platform window.
2. **`GET /frame_debugger/enable?value=true`** → `200`, `historyCount` 0 → 1,
   `totalEventCount: 1`, `enabled: true`. **`GET /get_swapchain`** confirmed:
   the event tree now shows a real "Game View" group with one real "GameView"
   leaf — not "No frame captured yet." — the Enable edge's own auto-capture
   fired exactly as designed.
3. **`GET /frame_debugger/state`** → `200`,
   `{"channel":"all","enabled":true,"historyCount":1,"historyCursor":0,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":1,"windowOpen":true}`
   — confirms `enabled: true`, `totalEventCount: 1 >= 1`, `historyCount: 1 >=
   1`, exactly as this step requires.
4. **`GET /frame_debugger/select_event?index=0`** → `200`,
   `selectedEventIndex: 0`. **`GET /get_swapchain`** confirmed: the
   event-details section now reads "Event #0: Draw Mesh" with real
   Pass/Blend/ZClip/ZTest/ZWrite/Cull/Stencil Ref rows populated (`Pass =
   GameView`, `Blend = Opaque (no blend)`, `ZClip = On`, `ZTest = Less`,
   `ZWrite = On`, `Cull = None`, `Stencil Ref = n/a (no stencil test)`) — not
   "No event selected."
5. **`GET /frame_debugger/capture`** → `200`, `historyCount` 1 → 2,
   `historyCursor: 1` — a new history entry was genuinely added (the
   ring buffer has not yet reached its `kCapacity = 8`, so this is a real
   increment, not a wraparound).
6. **`GET /frame_debugger/step_history?direction=prev`** → `200`,
   `historyCursor` 1 → 0. **`GET /get_swapchain`** confirmed: the toolbar now
   reads "Frame 1 of 2" (moved backward from "Frame 2 of 2"), and the
   `selectedEventIndex` was correctly reset to `-1` by `Build()`'s own
   existing "did the viewed history entry change" detection (visible as "No
   event selected." on screen, and confirmed via the JSON's own
   `selectedEventIndex: -1`) — the viewed frame's content genuinely changed to
   an earlier captured frame.
7. **`GET /frame_debugger/set_channel?value=r`** → `200`, `channel: "r"`.
   **`GET /get_swapchain`** confirmed: the preview image visibly changed to a
   grayscale, isolated-red-channel rendering (a smooth vertical gradient
   matching the original sky gradient's own red-channel profile), and the "R"
   Channels button is now highlighted/active.
8. **`GET /frame_debugger/set_levels?black=0.2&white=0.8`** → `200`,
   `levelsBlack: 0.2`, `levelsWhite: 0.8`. **`GET /get_swapchain`** confirmed:
   the Levels row now reads "Black 0.20"/"White 0.80" and the preview image's
   contrast visibly changed — a noticeably sharper black/white transition
   band than step 7's own unstretched gradient, exactly the expected
   `[0.2, 0.8]` remap.
9. **Clean-up**: `GET /frame_debugger/enable?value=false` → `200`,
   `enabled: false` (confirming the asymmetric "disabling doesn't auto-resume
   playback" behavior stays intact and reachable over HTTP), then
   `stop_app_background` cleanly terminated the process.

**Every one of this phase's own required checks passed, exactly as specified
— no shortcuts, no simulated/mocked responses, every screenshot below sourced
from a real, running `GreatTamanaEngine.exe` instance.** This is the concrete
evidence that finally closes the manual-verification gap both
`frame-debugger-1` and `frame-debugger-2` had to accept as a documented
limitation.

## Deviations from the phase document

None. Every one of this phase's own Step 3.1–3.5 items was completed exactly
as written: the build-system re-check found zero gaps (no
`CMakeLists.txt`/`tests/CMakeLists.txt` change needed), the documentation
sweep touched exactly the five files Step 3.6's own file-change inventory
names (`docs/conventions/frame-debugger.md`, `AGENTS.md`, `README.md`,
`TODO.md`, `docs/README.md`), the full validation suite (both Editor
configurations' clean builds plus the full `ctest` regression) passed 100%,
and the live smoke test exercised every one of the nine numbered steps Step
3.4 specifies, each with real, observed evidence (not just "it worked")
recorded above. The one small self-caught-and-fixed editing mistake during the
`README.md` sweep (see "A small correction made during the doc sweep" above)
is documented per this task's own explicit instruction, even though it never
reached a committed state.

## File-change inventory (this phase)

Modified:
- `docs/conventions/frame-debugger.md` (completely rewritten)
- `AGENTS.md` (Frame Debugger section rewritten, "scaffolding" dropped)
- `README.md` (new top-of-"Status" bullet)
- `docs/README.md` (Frame Debugger index entry updated)
- `TODO.md` (Frame Debugger section restructured: DONE list + still-deferred
  list)

New:
- `task_manager/frame-debugger-3/PHASE8_COMPLETION_REPORT.md` (this file)
- `task_manager/frame-debugger-3/CAMPAIGN_COMPLETION_REPORT.md`

No production source file was modified or added this phase.

## Next step

None — this is the closing phase of the `frame-debugger-3` campaign. See
`CAMPAIGN_COMPLETION_REPORT.md` for the full eight-phase writeup.
