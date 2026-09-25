# PHASE1 — Extract Presentation-Formatting Helpers (Zero Behavior Change)

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Design Decisions #5 and #12). This is a small, low-risk, purely-mechanical
phase — its entire job is to make Phase 2/3's later work safe, by giving
BOTH the future JSON exporter and the (for now, unchanged) ImGui panel one
single, shared, tested place to call for "turn raw render-graph data into a
human string." If a genuine ambiguity comes up, use `ask_questions` rather
than guessing.

## Step 1: The Goal

Move `FormatGpuTiming()`, `JoinNames()`, and `PassNameAtSurvivingIndex()` out
of `src/Editor/Panels/RenderGraphPanel.cpp`'s anonymous namespace and into a
NEW, shared, `gte_core`-tier, ImGui-free file:
`src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h/.cpp`. Add TWO
brand-new small helpers this campaign will need later (`ToString(ResourceKind)`,
`ToString(ViewScope)`) to the SAME new file, since neither currently exists
anywhere in the engine. `RenderGraphPanel.cpp`'s own ImGui output must be
BYTE-IDENTICAL before and after this phase — this is a pure relocation +
two brand-new additive functions, never a behavior change to anything that
already renders.

## Step 2: The Situation

Confirmed by direct read, `src/Editor/Panels/RenderGraphPanel.cpp`'s
anonymous namespace (current file, top of file):

```cpp
std::string FormatGpuTiming(const GpuTimingSample& timing) { ... } // Present -> "%.2f ms", Unsupported -> "Unsupported", Absent/default -> "N/A"
std::string JoinNames(const std::vector<std::string>& names) { ... } // empty -> "-", each empty name -> "(unnamed)", joined with ", "
const char* PassNameAtSurvivingIndex(const rg::RenderGraphSnapshot& snapshot, std::int32_t index) { ... } // out-of-range -> "?", empty name -> "(unnamed)"
```

`GpuTimingSample` (`src/Renderer/GpuTiming.h`) and `RenderGraphSnapshot`
(`src/Renderer/RenderGraph/RenderGraphSnapshot.h`) are BOTH already
`gte_core`-tier, ImGui-free types — nothing about these three functions'
OWN logic depends on ImGui or the Editor tier at all; they are trapped in
`src/Editor/Panels/` purely by historical accident (they were written
in-place, inline, when this panel was built, with no other consumer in mind
at the time).

Confirmed, `src/Renderer/RenderGraph/RenderGraphTypes.h`: `ResourceKind` is
declared (an enum with at least `Texture`/`Buffer`/`VolumeTexture`
enumerators — read the file directly to confirm the exact current list
before writing `ToString(ResourceKind)`) with existing `DispatchByKind()`
generic dispatch machinery, but **no `ToString(ResourceKind)` free function
exists** (confirmed, `search_in_dir` for `ToString(ResourceKind` — zero
hits). `ViewScope` (`enum class ViewScope { Shared, GameView, SceneView }` —
read the file directly to confirm the exact current enumerator names/count)
likewise has **no `ToString(ViewScope)` free function** (confirmed, zero
hits for `ToString(ViewScope`).

Confirmed, `src/Renderer/RenderGraph/RenderGraphTypes.h`'s own existing
style for every OTHER `ToString(...)` free function in this file (e.g.
`const char* ToString(PassKind kind) noexcept;`, declared in the header,
defined in `RenderGraphTypes.cpp`) — a plain `switch` with **no `default:`
case** (so a future new enumerator fails to compile at every `ToString()`
call site until updated, the same "no silent fallback" discipline
`DispatchByKind()`'s own doc comment already documents for `ResourceKind`).
Match this exact style for the two new functions this phase adds, even
though they live in a DIFFERENT new file (`RenderGraphSnapshotFormatting.h/.cpp`,
not `RenderGraphTypes.h/.cpp` — Locked Design Decision #12 keeps ALL new
presentation-formatting code in this one new sibling file, including these
two, rather than splitting "the two brand-new `ToString()`s" into
`RenderGraphTypes.h/.cpp` and "the three relocated helpers" into
`RenderGraphSnapshotFormatting.h/.cpp` — one new file, one home, for
everything this phase adds).

## Step 3: The Plan

### Step 3.1 — New files: `src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h` + `.cpp`

```cpp
#pragma once

// editor-core-separation-7 campaign, PHASE1
// (PHASE1_EXTRACT_FORMATTING_AND_LABEL_HELPERS.md) - pure, ImGui-free,
// Tier-1-testable presentation-formatting helpers shared by BOTH
// RenderGraphPanel.cpp (ImGui) and RenderGraphMetadata.cpp (JSON, PHASE2) -
// see PHASE0_MASTER_STRATEGY.md's Locked Design Decision #12 for why this is
// its OWN new file rather than folded into RenderGraphSnapshot.h/.cpp
// (RenderGraphSnapshot itself stays free of any "how a string should look"
// opinion, exactly as it always has been).
//
// Every function here is a pure function of already-computed plain data -
// no live RenderGraph/VkDevice/Renderer/ImGui - mirrors
// RenderGraphSnapshot.h's own "directly Tier-1-testable" precedent exactly.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../GpuTiming.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte::rg {

// Relocated verbatim from RenderGraphPanel.cpp's own anonymous namespace -
// Present -> "%.2f ms", Unsupported -> "Unsupported", Absent/default ->
// "N/A" (NEVER a fabricated "0.00 ms" - see AGENTS.md, "Profiling").
std::string FormatGpuTiming(const GpuTimingSample& timing);

// Relocated verbatim - empty vector -> "-", each empty-string name ->
// "(unnamed)", joined with ", ".
std::string JoinNames(const std::vector<std::string>& names);

// Relocated and RENAMED from PassNameAtSurvivingIndex() (a clearer name now
// that this lives outside the one panel that used to be its only caller) -
// identical behavior: out-of-range index -> "?", empty name -> "(unnamed)".
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index);

// NEW (this phase) - mirrors PassKind/RenderPassCategory/RenderPassDrawKind/
// RenderPassEvent's own existing ToString() free-function precedent
// (RenderGraphTypes.h/.cpp) exactly: a plain switch, NO default: case, so a
// future new ResourceKind enumerator fails to compile here until updated.
const char* ToString(ResourceKind kind) noexcept;

// NEW (this phase) - same "no default: case" discipline, for ViewScope
// ("Shared" / "GameView" / "SceneView").
const char* ToString(ViewScope scope) noexcept;

} // namespace gte::rg
```

`.cpp` body: copy `FormatGpuTiming()`/`JoinNames()`'s existing bodies
VERBATIM (character-for-character) from `RenderGraphPanel.cpp`. For
`ResolvePassNameAtSurvivingIndex()`, copy `PassNameAtSurvivingIndex()`'s body
verbatim too (only the function name changes). For the two new `ToString()`
functions, `read_file` `RenderGraphTypes.h` FIRST to confirm the exact,
current, complete enumerator list for `ResourceKind` and `ViewScope` before
writing either switch — do not guess the enumerator names from this
document's own paraphrase.

### Step 3.2 — Update `RenderGraphPanel.cpp`

1. `#include "../../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"`.
2. DELETE `FormatGpuTiming()`, `JoinNames()`, `PassNameAtSurvivingIndex()`
   from the anonymous namespace entirely.
3. Every call site that used `FormatGpuTiming(...)`/`JoinNames(...)` now
   calls `rg::FormatGpuTiming(...)`/`rg::JoinNames(...)` (the file already
   `using namespace gte;`-less, qualifies `rg::` types elsewhere, e.g.
   `rg::RenderGraphSnapshot` — follow the SAME existing qualification style
   already used throughout this exact file, do not introduce a new
   `using namespace gte::rg;` that does not already exist there).
4. Every call site that used `PassNameAtSurvivingIndex(snapshot, index)` now
   calls `rg::ResolvePassNameAtSurvivingIndex(snapshot, index)`.
5. **Do not touch anything else in this file** — no reordering, no
   reformatting, no rewording of any comment that isn't directly adjacent to
   a deleted function. `git diff` this file at the end of this step and
   confirm the ONLY hunks are: one new `#include`, three deleted function
   bodies, and the handful of call-site qualifications above.

### Step 3.3 — New Tier-1 test file: `tests/Renderer/RenderGraph/RenderGraphSnapshotFormattingTests.cpp`

These three relocated functions had NO test coverage before (they were
anonymous-namespace `static`-linkage functions inside a `.cpp` file,
unreachable from any test binary) — this phase is the first point they
become independently testable, and must add real coverage, not just move
untested code around. Mirror `RenderGraphSnapshotTests.cpp`'s own existing
structure/framework/include style exactly (`read_file` that file first to
copy its header/namespace/test-registration boilerplate verbatim). Minimum
cases:

- `FormatGpuTiming()`: `GpuTimingSample::Status::Present` with a known
  `milliseconds` value formats to the exact expected `"%.2f ms"` string;
  `Status::Unsupported` -> `"Unsupported"`; `Status::Absent` -> `"N/A"`.
- `JoinNames()`: empty vector -> `"-"`; one non-empty name -> that name
  unchanged; two names -> `"A, B"`; a name that is itself an empty string ->
  `"(unnamed)"` in its slot.
- `ResolvePassNameAtSurvivingIndex()`: index `-1` -> `"?"`; index `>=` the
  snapshot's own pass count -> `"?"`; a valid index into a pass with a
  non-empty name -> that name; a valid index into a pass with an empty name
  -> `"(unnamed)"`.
- `ToString(ResourceKind)`: one assertion per CURRENT enumerator (confirmed
  in Step 2 above) — each must return a distinct, non-null, non-empty
  string.
- `ToString(ViewScope)`: one assertion per CURRENT enumerator — same shape.

### Step 3.4 — CMake wiring

Add to the root `CMakeLists.txt`'s `gte_core` source list, immediately after
`src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` (confirmed current line
665 — `search_in_dir` for `RenderGraphSnapshot.cpp` in `CMakeLists.txt`
immediately before editing, in case an earlier phase in a DIFFERENT,
concurrently-landed campaign has shifted this line number):

```
src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h
src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.cpp
```

Add to `tests/CMakeLists.txt`, immediately after
`Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` (confirmed current line
2184 — re-confirm via `search_in_dir` before editing, same reasoning):

```
Renderer/RenderGraph/RenderGraphSnapshotFormattingTests.cpp
```

### Verification

1. Incremental build: `cmake --build build`.
2. Run the new test binary's relevant test cases (via whatever this repo's
   `ctest`/test-runner invocation for a SINGLE test file already looks like —
   `search_in_dir` the `tests/` folder or `AGENTS.md` for the exact
   single-file-run command this repo already documents; do NOT run the FULL
   `ctest` suite yet, per Workflow Rule 1).
3. Live smoke test: `run_app_background` the real `GreatTamanaEditor.exe`,
   `gte_send_request` against `GET /activate_tab?name=Render%20Graph` (or
   simply confirm the panel builds without error via `GET /get_logs`, no new
   warning/error text), then `GET /get_swapchain` — take a screenshot with
   the "Render Graph" panel visible and manually confirm it is pixel-for-
   pixel unchanged from a screenshot taken on the commit immediately BEFORE
   this phase (use `load_image` to view both). `stop_app_background`
   afterward.
4. `git_status` — confirm the diff touches exactly: the two new
   `RenderGraphSnapshotFormatting.h/.cpp` files, the new
   `RenderGraphSnapshotFormattingTests.cpp` file, `RenderGraphPanel.cpp`
   (the narrow diff from Step 3.2), and the two `CMakeLists.txt` files.

### What this phase does NOT do

- Does not create `RenderGraphMetadata` or any JSON conversion — PHASE2.
- Does not change `RenderGraphPanel::Build()`'s own signature or its call
  order/section layout in any way — PHASE3 is the phase that changes what
  data the panel reads from; this phase only changes WHERE three already-
  correct formatting functions physically live.
- Does not touch `RenderPassGroupRegistry`/tag-label resolution — that logic
  is added directly inside `BuildRenderGraphMetadata()` in PHASE2, it is not
  a "formatting helper" in the sense this phase's file is for (it needs the
  registry, a different, heavier dependency than this file wants).

### Completion

Write `PHASE1_COMPLETION_REPORT.md` (the exact before/after screenshot
comparison evidence via `load_image`/`gte_send_request`, confirming
byte-for-byte-unchanged panel output, plus the new tests' pass/fail
console output), then `git_add` + `git_commit`.
