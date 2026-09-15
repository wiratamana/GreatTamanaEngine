# PHASE1 — Frame Debugger data model (pure, Tier-1-testable, zero ImGui)

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: nothing (first phase).
Touches: new files only, plus `CMakeLists.txt`/`tests/CMakeLists.txt`.

## Step 1: The Goal

Create a brand-new, self-contained, ImGui-free module —
`src/Editor/FrameDebuggerData.h`/`.cpp` — holding every plain-data struct
the Frame Debugger window will ever display, plus the small set of pure
functions every later phase calls into. This mirrors the exact
"`MemoryPanelData.h`/`ProfilerPanelData.h`/`JobsPanelData.h`: pure,
directly-testable reshaping module" precedent `AGENTS.md`'s "Testability
& Regression Safety" section requires new logic to follow whenever
possible.

By the end of this phase: the new files compile, are linked into
`gte_core`, have a passing Tier-1 test file, and are **not yet called
from anywhere** (exactly like `frame-debugger-1`'s own PHASE1 — "zero
call sites into the rest of the engine yet"). Nothing user-visible
changes.

## Step 2: The Situation

- Precedent for this exact kind of file: `src/Editor/JobsPanelData.h/.cpp`
  and `src/Editor/ProfilerPanelData.h/.cpp` (small, pure, `namespace gte`
  structs + reshape functions, tested under `tests/Editor/
  JobsPanelDataTests.cpp`/`ProfilerPanelDataTests.cpp`). `src/Renderer/
  RenderGraph/RenderGraphSnapshot.h` is the closest conceptual analog
  (its own doc comment literally says "mirrors ... own 'small, dedicated,
  directly-testable reshaping module' precedent") — read that file for
  its shape (it is not a dependency of this one; it just proves the
  pattern already exists for "flatten some tree of render events into a
  displayable snapshot").
- This module has **no** dependency on `gte::rg::RenderGraph`, `Renderer`,
  or anything Vulkan — it is placeholder/mock structure only this
  campaign. A **future** campaign is expected to add a *second* builder
  function (a real one) living either in this same file or a new one,
  and swap which one `FrameDebuggerPanel` calls — this phase's whole job
  is making sure that swap will be trivial.
- `CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block
  (`target_sources(gte_core PRIVATE ...)`) lists `src/Editor/
  JobsPanelData.h`/`.cpp` around line 603-604 — add the two new files
  immediately after that pair (exact line numbers shift as other
  campaigns land first; search for `src/Editor/JobsPanelData.cpp` to find
  the current insertion point rather than trusting a hardcoded line
  number).
- `tests/CMakeLists.txt`'s own `if(GTE_ENABLE_EDITOR)` block (search for
  `Editor/JobsPanelDataTests.cpp` — around line 1909) is where
  `Editor/FrameDebuggerDataTests.cpp` gets added, in the **same**
  conditional list (not the unconditional `EditorPanelCatalogTests.cpp`-style
  list near the top of the file — this module has no Network-layer
  consumer).

## Step 3: The Plan

### 3.1 New file: `src/Editor/FrameDebuggerData.h`

```cpp
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

// task_manager/frame-debugger-2 campaign (PHASE1) - the pure, ImGui-free
// data model behind the Editor's "Frame Debugger" window (Panels/
// FrameDebuggerPanel.h) - mirrors JobsPanelData.h/ProfilerPanelData.h's
// own "small, dedicated, directly-testable reshaping module" precedent
// (see AGENTS.md, "Testability & Regression Safety").
//
// THIS CAMPAIGN IS GUI-ONLY: BuildPlaceholderFrameDebuggerSnapshot()
// below always returns an EMPTY snapshot, on purpose, forever, until a
// FUTURE campaign adds real frame/draw-call capture and either replaces
// this function's body or introduces a second, real builder function
// FrameDebuggerPanel switches to calling instead. Every struct here is
// already shaped exactly like the reference screenshot's real data would
// be, specifically so that future swap requires touching NOTHING under
// Panels/FrameDebuggerPanel.cpp - only this file (or a new sibling file)
// needs to change.
namespace gte {

// One texture property row (see the reference screenshot's "Textures"
// section, e.g. "_MainTex"). `valueLabel` is already a display-ready
// string (e.g. a texture's name, or "None") - never a real GPU handle;
// this struct is intentionally decoupled from any real Vulkan/RenderTexture
// type, exactly like RenderGraphPassSnapshot's own OWNED-string
// philosophy (see RenderGraphSnapshot.h).
struct FrameDebuggerTextureProperty {
    std::string name;
    std::string valueLabel;
};

// One vector property row (e.g. "_Color", "(1, 1, 1, 1)").
struct FrameDebuggerVectorProperty {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

// One 4x4 matrix property row (e.g. "unity_MatrixVP"). Row-major, 16
// entries - values[0..3] is row 0, etc.
struct FrameDebuggerMatrixProperty {
    std::string name;
    std::array<float, 16> values{};
};

// Everything the Inspector's event-level section (bottom half of the
// right-hand pane) needs to display for ONE selected draw/event - see
// Panels/FrameDebuggerPanel.cpp's BuildEventDetailsSection() (PHASE6).
// Every field is a plain, already-formatted display string/value -
// deliberately NOT a real VkPipeline/shader-reflection handle of any
// kind (this campaign never reads one).
struct FrameDebuggerEventDetails {
    int eventIndex = -1; // Matches the owning FrameDebuggerEventNode::eventIndex.
    std::string shaderName;
    std::string passName;
    std::string blendMode;
    std::string zClip;
    std::string zTest;
    std::string zWrite;
    std::string cull;
    std::string stencilRef;
    std::string stencilComp;
    std::string stencilPass;
    std::string stencilFail;
    std::string stencilZFail;
    std::vector<FrameDebuggerTextureProperty> textures;
    std::vector<FrameDebuggerVectorProperty> vectors;
    std::vector<FrameDebuggerMatrixProperty> matrices;
};

// One row of the left-hand event tree (see the reference screenshot's
// "Camera.Render > Drawing > Render.OpaqueGeometry > ... > Draw Mesh ..."
// hierarchy). A GROUP node (isDrawCall == false, e.g. "Drawing",
// "Render.OpaqueGeometry") has children and no `details`; a LEAF node
// (isDrawCall == true, e.g. one "Draw Mesh pf_fence_02" row) has no
// children and, once real capture exists, a populated `details`.
// `details` is std::optional and almost always std::nullopt this
// campaign (there is never a real leaf node to populate it for, since
// BuildPlaceholderFrameDebuggerSnapshot() always returns zero nodes) -
// see FindEventDetailsByIndex() below for the one place that reads it.
struct FrameDebuggerEventNode {
    std::string name;
    bool isDrawCall = false;
    int eventIndex = -1; // Only meaningful when isDrawCall is true; a global, 0-based, Unity-style event counter.
    std::optional<FrameDebuggerEventDetails> details;
    std::vector<FrameDebuggerEventNode> children;
};

// Frame-level (not per-event) render-target info, shown in the
// Inspector's frame-level chrome (PHASE5) regardless of any event
// selection - e.g. the reference screenshot's "<No name>" / "866x487
// Default" row.
struct FrameDebuggerRenderTargetInfo {
    std::string name = "<No name>";
    int width = 0;
    int height = 0;
    std::string format = "Default";
};

// The whole displayable snapshot for one "frame" of Frame Debugger data.
// `totalEventCount` backs the stepper row's "N of M" label (see
// FormatFrameStepperLabel() below) - always 0 this campaign, since
// rootNodes is always empty.
struct FrameDebuggerSnapshot {
    std::vector<FrameDebuggerEventNode> rootNodes;
    int totalEventCount = 0;
    FrameDebuggerRenderTargetInfo renderTarget;
};

// Always returns a completely EMPTY snapshot (rootNodes empty,
// totalEventCount == 0, renderTarget left at its all-default state) -
// see this file's own top-of-file comment for why, and PHASE0's Locked
// Design Decision #2 for why the UI must never invent fake rows to fill
// the gap this leaves. THE single seam a future real-capture campaign
// replaces (either this function's body, or by introducing a second, real
// builder function FrameDebuggerPanel::Build() switches to calling
// instead of this one - either way, no other file needs to change).
FrameDebuggerSnapshot BuildPlaceholderFrameDebuggerSnapshot();

// Formats the stepper row's "N of M" label - 1-based display index,
// matching the reference screenshot's own "2117 of 2117" convention.
// Returns "0 of 0" whenever totalEventCount <= 0 (always true this
// campaign) rather than a divide-by-zero-adjacent "1 of 0" or similar -
// this is the one place that decision is made, so PHASE3's stepper row
// never needs its own special-casing.
std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount);

// Clamps `requested` into a valid [0, totalEventCount - 1] selection, or
// returns -1 (no valid selection possible) if totalEventCount <= 0.
// Pure integer arithmetic - the one place PHASE4's future real tree-row
// click handling, and any future prev/next-event navigation buttons,
// should route their own index math through, rather than each
// reimplementing the same clamp.
int ClampSelectedEventIndex(int requested, int totalEventCount);

// Recursively searches `snapshot.rootNodes` (and every descendant) for a
// leaf node (isDrawCall == true) whose eventIndex == eventIndex,
// returning its `details` if found. Returns std::nullopt if
// eventIndex < 0, if no such node exists, or if the matching node's own
// `details` was never populated - ALWAYS std::nullopt in production this
// campaign (BuildPlaceholderFrameDebuggerSnapshot() never produces a
// non-empty tree), but written as a REAL, correct, recursive lookup so a
// future campaign only needs to start populating FrameDebuggerEventNode::
// details for this to start returning real values - no caller-side code
// (Panels/FrameDebuggerPanel.cpp's BuildEventDetailsSection() call site,
// PHASE6) ever needs to change.
std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex);

} // namespace gte
```

### 3.2 New file: `src/Editor/FrameDebuggerData.cpp`

```cpp
#include "FrameDebuggerData.h"

namespace gte {

FrameDebuggerSnapshot BuildPlaceholderFrameDebuggerSnapshot()
{
    // Deliberately empty - see FrameDebuggerData.h's own top-of-file
    // comment and PHASE0's Locked Design Decision #2. Returning a
    // default-constructed FrameDebuggerSnapshot{} is intentional, not a
    // stub left unfinished - this IS the finished behavior for this
    // campaign.
    return FrameDebuggerSnapshot{};
}

std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return "0 of 0";
    }
    // 1-based display, matching the reference screenshot's own
    // "2117 of 2117" convention - currentEventIndex is a 0-based index
    // internally (see ClampSelectedEventIndex()'s own contract), so +1
    // here, once, is the single place that conversion happens.
    const int displayIndex = currentEventIndex < 0 ? 0 : (currentEventIndex + 1);
    return std::to_string(displayIndex) + " of " + std::to_string(totalEventCount);
}

int ClampSelectedEventIndex(int requested, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return -1;
    }
    if (requested < 0) {
        return 0;
    }
    if (requested >= totalEventCount) {
        return totalEventCount - 1;
    }
    return requested;
}

namespace {

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndexRecursive(
    const std::vector<FrameDebuggerEventNode>& nodes, int eventIndex)
{
    for (const FrameDebuggerEventNode& node : nodes) {
        if (node.isDrawCall && node.eventIndex == eventIndex) {
            return node.details;
        }
        std::optional<FrameDebuggerEventDetails> found
            = FindEventDetailsByIndexRecursive(node.children, eventIndex);
        if (found.has_value()) {
            return found;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex)
{
    if (eventIndex < 0) {
        return std::nullopt;
    }
    return FindEventDetailsByIndexRecursive(snapshot.rootNodes, eventIndex);
}

} // namespace gte
```

### 3.3 New file: `tests/Editor/FrameDebuggerDataTests.cpp`

Mirror `tests/Editor/JobsPanelDataTests.cpp`'s own include/namespace shape
exactly (`#include "Editor/FrameDebuggerData.h"`, `#include <gtest/gtest.h>`,
`namespace gte { namespace { ... } }`). Cover, at minimum:

- `BuildPlaceholderFrameDebuggerSnapshotTest`: the returned snapshot has
  an empty `rootNodes`, `totalEventCount == 0`, and
  `renderTarget.name == "<No name>"`.
- `FormatFrameStepperLabelTest`: `(0, 0)` -> `"0 of 0"`; `(-1, 0)` ->
  `"0 of 0"`; `(0, 2117)` -> `"1 of 2117"`; `(2116, 2117)` ->
  `"2117 of 2117"`.
- `ClampSelectedEventIndexTest`: `(-1, 0)` -> `-1`; `(5, 0)` -> `-1`;
  `(-1, 10)` -> `0`; `(999, 10)` -> `9`; `(4, 10)` -> `4` (identity for an
  already-valid index).
- `FindEventDetailsByIndexTest`: **hand-build** a small, synthetic,
  non-empty `FrameDebuggerSnapshot` directly in the test itself (this is
  the ONE place in this whole campaign a non-empty tree is ever
  constructed - purely so the recursive lookup logic itself is proven
  correct ahead of any future real capture code depending on it) - e.g.
  a root group node `"Drawing"` containing one leaf `"Draw Mesh Foo"`
  (`isDrawCall = true`, `eventIndex = 3`, `details` populated with a
  distinguishable `shaderName`) nested two levels deep inside another
  group. Assert `FindEventDetailsByIndex(snapshot, 3)->shaderName` matches,
  `FindEventDetailsByIndex(snapshot, 999)` is `std::nullopt`, and
  `FindEventDetailsByIndex(snapshot, -1)` is `std::nullopt`.

### 3.4 Build wiring

- `CMakeLists.txt`: inside the `if(GTE_ENABLE_EDITOR)` `target_sources(gte_core
  PRIVATE ...)` block, add (search for `src/Editor/JobsPanelData.cpp` and
  insert right after that line):
  ```
  src/Editor/FrameDebuggerData.h
  src/Editor/FrameDebuggerData.cpp
  ```
- `tests/CMakeLists.txt`: inside its own `if(GTE_ENABLE_EDITOR)`
  `list(APPEND GTE_TEST_SOURCES ...)` block, add (search for
  `Editor/JobsPanelDataTests.cpp` and insert right after that line):
  ```
  Editor/FrameDebuggerDataTests.cpp
  ```

### 3.5 Compile check

Configure/build `GreatTamanaEngineTests` only (fast — no need to rebuild
the full `GreatTamanaEngine` app target for a Tier-1-only phase):
`cmake --build build --target GreatTamanaEngineTests`, then run it filtered:
`GreatTamanaEngineTests.exe --gtest_filter=*FrameDebuggerData*` and confirm
every new case passes. Do **not** run a full `ctest` regression yet
(PHASE7 does that).

### 3.6 File-change inventory (this phase only)

New: `src/Editor/FrameDebuggerData.h`, `src/Editor/FrameDebuggerData.cpp`,
`tests/Editor/FrameDebuggerDataTests.cpp`.
Modified: `CMakeLists.txt`, `tests/CMakeLists.txt`.

Write `PHASE1_COMPLETION_REPORT.md` into `task_manager/frame-debugger-2/`
when done, then `git_add`/`git_commit`.
