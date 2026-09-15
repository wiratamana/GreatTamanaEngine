# PHASE1 — Frame Debugger data model — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE1_FRAME_DEBUGGER_DATA_MODEL.md`

## Summary

Implemented PHASE1's own "Step 3: The Plan" / "3.6 File-change inventory"
exactly as written, verbatim from the phase document's own code listings,
with no deviations:

### New files

- `src/Editor/FrameDebuggerData.h` — pure, ImGui-free data model: the
  `FrameDebuggerTextureProperty`/`FrameDebuggerVectorProperty`/
  `FrameDebuggerMatrixProperty`/`FrameDebuggerEventDetails`/
  `FrameDebuggerEventNode`/`FrameDebuggerRenderTargetInfo`/
  `FrameDebuggerSnapshot` structs, plus the four pure function
  declarations (`BuildPlaceholderFrameDebuggerSnapshot()`,
  `FormatFrameStepperLabel()`, `ClampSelectedEventIndex()`,
  `FindEventDetailsByIndex()`), copied from the phase document's own
  §3.1 listing.
- `src/Editor/FrameDebuggerData.cpp` — the corresponding implementation,
  copied from the phase document's own §3.2 listing.
  `BuildPlaceholderFrameDebuggerSnapshot()` is, by design, a permanently
  empty placeholder (`return FrameDebuggerSnapshot{};`) — this is the
  correct, intended final state for this phase (and this whole
  GUI-scaffolding-only campaign), not an unfinished stub. No ImGui
  include, no `Renderer`/`gte::rg::RenderGraph` dependency anywhere in
  either file.
- `tests/Editor/FrameDebuggerDataTests.cpp` — Tier-1 test file mirroring
  `tests/Editor/JobsPanelDataTests.cpp`'s own include/namespace shape
  (`#include "Editor/FrameDebuggerData.h"`, `#include <gtest/gtest.h>`,
  `namespace gte { namespace { ... } }`). Covers exactly the four cases
  the phase document's §3.3 specified:
  - `BuildPlaceholderFrameDebuggerSnapshotTest` — empty `rootNodes`,
    `totalEventCount == 0`, `renderTarget.name == "<No name>"`.
  - `FormatFrameStepperLabelTest` — `(0,0)`→`"0 of 0"`, `(-1,0)`→
    `"0 of 0"`, `(0,2117)`→`"1 of 2117"`, `(2116,2117)`→`"2117 of 2117"`.
  - `ClampSelectedEventIndexTest` — `(-1,0)`→`-1`, `(5,0)`→`-1`,
    `(-1,10)`→`0`, `(999,10)`→`9`, `(4,10)`→`4` (identity).
  - `FindEventDetailsByIndexTest` — hand-builds a synthetic, non-empty
    `FrameDebuggerSnapshot` (a root group `"Drawing"` containing a nested
    group `"Render.OpaqueGeometry"` containing one leaf `"Draw Mesh Foo"`,
    `isDrawCall = true`, `eventIndex = 3`, `details.shaderName =
    "Standard/DistinguishableTestShader"`), then asserts
    `FindEventDetailsByIndex(snapshot, 3)->shaderName` matches, and that
    both `999` and `-1` return `std::nullopt`. This is the one and only
    place in this whole campaign a non-empty
    `FrameDebuggerSnapshot`/`FrameDebuggerEventNode` tree is ever
    constructed, exactly as the phase document specifies.

### Modified files

- `CMakeLists.txt` — inside `if(GTE_ENABLE_EDITOR)`'s `target_sources(gte_core
  PRIVATE ...)` block, added `src/Editor/FrameDebuggerData.h` and
  `src/Editor/FrameDebuggerData.cpp` immediately after the existing
  `src/Editor/JobsPanelData.cpp` line (found by searching for that exact
  string, per the phase document's own instruction rather than trusting a
  hardcoded line number).
- `tests/CMakeLists.txt` — inside its own `if(GTE_ENABLE_EDITOR)`
  `list(APPEND GTE_TEST_SOURCES ...)` block, added
  `Editor/FrameDebuggerDataTests.cpp` immediately after the existing
  `Editor/JobsPanelDataTests.cpp` line, same search-based approach.

## Deviations from the phase document

None. Every struct/function/test case was implemented exactly as
specified in `PHASE1_FRAME_DEBUGGER_DATA_MODEL.md`'s own §3.1/§3.2/§3.3
code listings, and the build-wiring insertion points matched the phase
document's own described search anchors on the first try (no line-number
drift encountered).

As expected per PHASE0's own plan, `FrameDebuggerData.h`/`.cpp` are **not**
called from anywhere yet (zero call sites into the rest of the Editor) —
nothing user-visible changes in this phase. `Panels/FrameDebuggerPanel.h/.cpp`
does not exist yet; that is PHASE2's job.

## Compile check (fast, per this phase's own instructions — not a full
rebuild/regression)

Ran exactly the command PHASE1's own "3.5 Compile check" section
specifies:

```
cmake --build build --target GreatTamanaEngineTests
```

Result: **succeeded** (re-ran CMake configure automatically to pick up the
two `CMakeLists.txt`/`tests/CMakeLists.txt` changes, then compiled
`FrameDebuggerData.cpp`, relinked `gte_core`, compiled
`FrameDebuggerDataTests.cpp`, and relinked `GreatTamanaEngineTests.exe` —
no warnings or errors from the new files).

Then ran it filtered to just the new test cases, as instructed:

```
GreatTamanaEngineTests.exe --gtest_filter=*FrameDebuggerData*
```

Result: **all 4 new tests passed**:

```
[==========] Running 4 tests from 1 test suite.
[----------] 4 tests from FrameDebuggerDataTest
[ RUN      ] FrameDebuggerDataTest.BuildPlaceholderFrameDebuggerSnapshotTest
[       OK ] FrameDebuggerDataTest.BuildPlaceholderFrameDebuggerSnapshotTest (0 ms)
[ RUN      ] FrameDebuggerDataTest.FormatFrameStepperLabelTest
[       OK ] FrameDebuggerDataTest.FormatFrameStepperLabelTest (0 ms)
[ RUN      ] FrameDebuggerDataTest.ClampSelectedEventIndexTest
[       OK ] FrameDebuggerDataTest.ClampSelectedEventIndexTest (0 ms)
[ RUN      ] FrameDebuggerDataTest.FindEventDetailsByIndexTest
[       OK ] FrameDebuggerDataTest.FindEventDetailsByIndexTest (0 ms)
[----------] 4 tests from FrameDebuggerDataTest (1 ms total)
[  PASSED  ] 4 tests.
```

No full clean build and no full `ctest` regression suite were run in this
phase — per both PHASE1's own "3.5 Compile check" section and PHASE0's
"3.6 Order of work", that is reserved for the final PHASE7 step only.

## Next step

PHASE2 (`PHASE2_PANEL_SHELL_AND_WINDOW_MENU_ENTRY.md`) — bring the actual
`FrameDebuggerPanel` window into existence, wired into the new "Window"
top-level menu and `ImGuiEditorLayer`, showing only a "Coming soon"
placeholder body.
