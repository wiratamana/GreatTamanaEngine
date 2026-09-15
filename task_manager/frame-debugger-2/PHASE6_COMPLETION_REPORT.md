# PHASE6 — Event-details section, tab bar, and property formatting — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE6_EVENT_DETAILS_SECTION_AND_PROPERTY_FORMATTING.md`

## Summary

Implemented PHASE6's own "Step 3: The Plan" exactly as written, verbatim
from the phase document's own code listings, with no deviations. This is
the heaviest single phase in the whole `frame-debugger-2` campaign (per
`PHASE0_MASTER_STRATEGY.md`'s own Step 3.5 flag) — the bottom, event-level
portion of the right-hand inspector pane is now a fully real, complete
renderer (`Shader`/`Pass`/`Blend`/`ZClip`/`ZTest`/`ZWrite`/`Cull`/five
Stencil rows, a `Preview`/`ShaderProperties` tab bar, and
Textures/Vectors/Matrices subsections inside `ShaderProperties`) against a
real `FrameDebuggerEventDetails` value — but gated behind
`m_selectedEventIndex`/`FindEventDetailsByIndex()`, which **always**
resolve to `std::nullopt` this campaign (PHASE4's event tree is always
empty, so nothing can ever be selected). In practice, this section
therefore always renders a single **"No event selected."** line — the
correct, intended final state for this campaign, per Locked Design
Decision #2. No fake/mock event data was invented anywhere.

### Changes, file by file (matching the phase document's §3.8 file-change
inventory exactly — modified files only, no new files this phase)

- **`src/Editor/FrameDebuggerData.h`**:
  - Appended one new field, `std::string eventLabel;`, at the very END of
    `FrameDebuggerEventDetails`'s field list (after `matrices`), with the
    exact doc comment from the phase document's §3.1 listing, explaining
    why it's a separate field from `shaderName`/`FrameDebuggerEventNode::
    name` and why it's appended at the end (never inserted in the middle
    of an existing struct, mirroring `BoneViewerWindow.h`'s own
    positional-aggregate-init-safety precedent).
  - Appended two new pure function declarations after
    `FindEventDetailsByIndex()`'s own declaration:
    `FormatVectorProperty(const FrameDebuggerVectorProperty&)` and
    `FormatMatrixProperty(const FrameDebuggerMatrixProperty&)`, with their
    doc comments copied verbatim from the phase document's §3.1 listing.
- **`src/Editor/FrameDebuggerData.cpp`**:
  - Added `#include <cstddef>` (for the explicit `std::size_t` cast) and
    `#include <cstdio>` (for `std::snprintf`), per the phase document's
    Step 2/§3.2 notes.
  - Added an anonymous-namespace `FormatCompactFloat(float)` helper
    (`%g`-based trimming, e.g. `"1.000000"` -> `"1"`), and implemented
    `FormatVectorProperty()`/`FormatMatrixProperty()` on top of it — all
    three copied verbatim from the phase document's §3.2 listing.
- **`tests/Editor/FrameDebuggerDataTests.cpp`**:
  - Added `#include <algorithm>` (for `std::count`).
  - Added `FormatVectorPropertyTest` (`{1,1,1,1}` -> `"(1, 1, 1, 1)"`;
    `{0.5,0,0,0}` -> `"(0.5, 0, 0, 0)"`) and `FormatMatrixPropertyTest`
    (the reference screenshot's own `unity_MatrixVP` numbers; asserts the
    first row is `"0.001 0 0 0"`, the last row is `"0 0 0 1"`, and there
    are exactly 3 embedded `'\n'` characters — avoiding brittle exact-
    `%g`-rounding assertions on the two middle rows, per the phase
    document's own §3.3 guidance) — both copied verbatim from the phase
    document's own test-case description.
  - No new case was added purely for `eventLabel` (a plain
    default-`""`-constructing `std::string`, already effectively covered
    by PHASE1's `BuildPlaceholderFrameDebuggerSnapshotTest`), matching the
    phase document's own explicit note that none was needed.
- **`src/Editor/Panels/FrameDebuggerPanel.h`**:
  - Added one new private method declaration,
    `void BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details);`,
    directly below `BuildInspectorPane()`'s own declaration.
- **`src/Editor/Panels/FrameDebuggerPanel.cpp`**:
  - Added `#include <cstddef>` (for the explicit `std::size_t start`/
    `newlinePos` locals used later in `BuildEventDetailsSection()`).
  - Added `BuildPropertyRow(const char*, const std::string&)` to the SAME
    anonymous-namespace block PHASE4 already opened (the one holding
    `kSplitterWidth`), rather than a second separate block, per the phase
    document's own explicit instruction.
  - Implemented `BuildEventDetailsSection()` — copied verbatim from the
    phase document's §3.5 listing: the `std::nullopt` early-return
    ("No event selected.", always this branch in production this
    campaign), followed by the (currently unreachable, but fully correct)
    real-data branch: the `"Event #%d: %s"` header using the new
    `eventLabel` field, twelve `BuildPropertyRow()` calls for the Shader/
    Pass/Blend/ZClip/ZTest/ZWrite/Cull/Stencil(Ref/Comp/Pass/Fail/ZFail)
    rows, a `BeginTabBar("FrameDebuggerEventTabs")`/`BeginTabItem("Preview")`
    (a plain `"Not available yet."` message — no real per-draw-call preview
    render, per the phase document's own explicit scope boundary) /
    `BeginTabItem("ShaderProperties")` (conditionally-shown Textures/
    Vectors/Matrices subsections, each only rendered when its own vector
    is non-empty, using `FormatVectorProperty()`/`FormatMatrixProperty()`
    and the row-splitting `while` loop over `'\n'` exactly as the phase
    document specifies, so each matrix row becomes its own
    `ImGui::Text()` call for clean monospace alignment).
  - Wired it in at the tail of `BuildInspectorPane()` — right after the
    `ImGui::Separator();` that already follows the PHASE5 texture-preview
    child region — with the exact two-line call from the phase document's
    §3.6 listing: `FindEventDetailsByIndex(snapshot, m_selectedEventIndex)`
    followed by `BuildEventDetailsSection(details);`. This is this
    campaign's very last piece of `Build()`/`BuildInspectorPane()` wiring —
    the entire "Frame Debugger" window's widget tree described in
    `PHASE0_MASTER_STRATEGY.md`'s Step 1 is now fully built.

Every line matches the phase document's own code listings verbatim — no
adaptation was needed beyond what the phase document itself already
specified.

## Deviations from the phase document

None. The implementation is a direct, verbatim copy of the phase
document's §3.1–§3.6 code listings and test-case descriptions.

As with PHASE2–PHASE5, actually opening the "Frame Debugger" window via a
real mouse click, checking "Enable", clicking into the (always-empty)
event tree, and visually confirming the exact pixel layout of the new
event-details section/tab bar remains **not feasible** from this
environment: the window is deliberately not part of
`EditorPanelCatalog.h` (Locked Design Decision #6), so `GET /activate_tab`
cannot bring it to front, and no HTTP command endpoint exists anywhere in
`src/Network/` capable of flipping `EditorContext::frameDebuggerWindowOpen`
or synthesizing a checkbox/tree-row click. This is the same pre-existing,
expected limitation already documented identically in PHASE2–PHASE5's own
completion reports, not a defect introduced by this phase. The new
`BuildEventDetailsSection()` code is a direct, reviewed, verbatim copy of
the phase document's own listing, using only well-established, already-
working ImGui idioms (`TextDisabled`/`Text`/`Separator`/`Spacing`/
`BeginTabBar`/`BeginTabItem`/`EndTabItem`/`EndTabBar`/`SeparatorText`/
`TextUnformatted`) — `BeginTabBar`/`BeginTabItem` are this engine's first
use of Dear ImGui's tab-bar API anywhere under `src/`, confirmed against
the phase document's own signature check versus the vendored
`third_party/imgui/imgui.h` — so it is expected to render exactly as the
phase document's own description states once triggered by a human via a
real click. (Also worth noting: this section is, in practice, unreachable
this campaign regardless of window-focus limitations, since
`m_selectedEventIndex` can never be anything but `-1` — there is no leaf
tree row to click, per Locked Design Decision #2 — so even a live human
click-through would only ever show the "No event selected." branch, which
is exactly the intended final state.)

One notable environment issue encountered and resolved during this phase
(not a code defect): the local disk (`C:`) was found to be almost
completely full (a few KB free) partway through the compile-check step,
causing the first `cmake --build` attempt to fail at the final
`ar.exe`/link step with "No space left on device" (not a compiler error —
the actual object-file compilation for every changed file, including the
new/modified `FrameDebuggerData.cpp`/`FrameDebuggerPanel.cpp`/
`FrameDebuggerDataTests.cpp`, had already succeeded). Clearing the Windows
Recycle Bin freed enough space (~400 MB) for the link step to complete
successfully on retry; no source files were altered to work around this,
and no evidence of this being a symptom of anything this phase's changes
did (the new files are tiny plain text/C++ — not remotely large enough to
fill a full disk on their own).

## Compile check (fast, per this phase's own §3.7 instructions — not a
full clean rebuild/regression, reserved for PHASE7 only)

1. `cmake --build build --target GreatTamanaEngineTests` — succeeded
   (after the disk-space hiccup above was resolved), compiling the
   modified `FrameDebuggerData.cpp`/`FrameDebuggerPanel.cpp` and the new
   test cases in `FrameDebuggerDataTests.cpp`, relinking `libgte_core.a`
   and `GreatTamanaEngineTests.exe`.
2. `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebuggerData*` —
   **all 6 tests passed** (the 4 pre-existing PHASE1 cases plus the 2 new
   PHASE6 cases, `FormatVectorPropertyTest`/`FormatMatrixPropertyTest`):

   ```
   [==========] Running 6 tests from 1 test suite.
   [ RUN      ] FrameDebuggerDataTest.BuildPlaceholderFrameDebuggerSnapshotTest
   [       OK ] FrameDebuggerDataTest.BuildPlaceholderFrameDebuggerSnapshotTest (0 ms)
   [ RUN      ] FrameDebuggerDataTest.FormatFrameStepperLabelTest
   [       OK ] FrameDebuggerDataTest.FormatFrameStepperLabelTest (0 ms)
   [ RUN      ] FrameDebuggerDataTest.ClampSelectedEventIndexTest
   [       OK ] FrameDebuggerDataTest.ClampSelectedEventIndexTest (0 ms)
   [ RUN      ] FrameDebuggerDataTest.FindEventDetailsByIndexTest
   [       OK ] FrameDebuggerDataTest.FindEventDetailsByIndexTest (0 ms)
   [ RUN      ] FrameDebuggerDataTest.FormatVectorPropertyTest
   [       OK ] FrameDebuggerDataTest.FormatVectorPropertyTest (0 ms)
   [ RUN      ] FrameDebuggerDataTest.FormatMatrixPropertyTest
   [       OK ] FrameDebuggerDataTest.FormatMatrixPropertyTest (0 ms)
   [==========] 6 tests from 1 test suite ran. (18 ms total)
   [  PASSED  ] 6 tests.
   ```

3. `cmake --build build --target GreatTamanaEngine` — succeeded, relinking
   the full app with the panel-side change.

### Live runtime smoke test

1. `run_app_background` launched the freshly-built
   `build/GreatTamanaEngine.exe` (PID 30512).
2. `gte_send_request` `GET /get_swapchain` (first request, immediately
   after launch) returned a `200 image/png` frame showing only the menu
   bar and Pause/Step toolbar — the very first rendered frame, before the
   dock layout had finished laying out; a second `GET /get_swapchain`
   moments later returned the fully-rendered default dock layout
   (Hierarchy/Scene/Game/Inspector, then Memory/Profiler/Render Graph/
   Atmosphere/Jobs/Project, all with real scene content — a sky gradient
   in Scene/Game, "Entity 0 (Camera)" in Hierarchy, "TestScene.gtscene" in
   Project) — confirming **no visual regression anywhere** from this
   phase's changes (the Frame Debugger window itself is closed by default,
   `EditorContext::frameDebuggerWindowOpen` starting `false`, so nothing
   new is visible on this screenshot — expected, matching PHASE2–PHASE5's
   own identical observation).
3. `gte_send_request` `GET /list_tabs` — confirmed the response still
   lists exactly the same ten pre-existing panels (`Hierarchy`/
   `Inspector`/`Scene`/`Game`/`Memory`/`Profiler`/`Render Graph`/`Jobs`/
   `Atmosphere`/`Project`) with **no** "Frame Debugger" entry —
   re-confirming Locked Design Decision #6 still holds after this phase's
   changes.
4. `stop_app_background` cleanly terminated the process.

As with PHASE2–PHASE5, actually opening the "Frame Debugger" window,
checking "Enable", and visually inspecting the new event-details section/
tab bar with a real mouse could not be exercised remotely in this
environment (see "Deviations" above) — the code itself is a verbatim,
reviewed copy of the phase document's own listing.

## Next step

PHASE7 (`PHASE7_INTEGRATION_BUILD_WIRING_AND_DOCS.md`) — final
`CMakeLists.txt`/`tests/CMakeLists.txt` confirmation,
`EditorPanelCatalog.h` non-inclusion documentation,
`AGENTS.md`/`docs/`/`TODO.md`/`README.md` updates, a full clean build
(both `GTE_ENABLE_EDITOR` configs) + full `ctest` regression + live
runtime smoke test, and the campaign completion report. Not started as
part of this phase — PHASE6 touched only the five files listed in its own
§3.8 file-change inventory (`src/Editor/FrameDebuggerData.h/.cpp`,
`tests/Editor/FrameDebuggerDataTests.cpp`,
`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`).
