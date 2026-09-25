# PHASE2 — `RenderGraphMetadata` Data Model + `BuildRenderGraphMetadata()` + JSON

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Design Decisions #1, #2, #5, #6, #10, #11). Also read `PHASE1_COMPLETION_REPORT.md`
before starting — this phase's own code calls `PHASE1`'s new
`RenderGraphSnapshotFormatting.h` helpers directly. This is the single
largest, most detail-sensitive phase in this campaign (the JSON shape it
locks in is a real, external, AI-facing contract from Phase 4 onward) — use
`ask_questions` freely if anything below looks ambiguous once real code is
in front of you.

## Step 1: The Goal

Ship ONE new, pure, engine-free, `nlohmann::json`-able C++ type,
`gte::rg::RenderGraphMetadata`, and the pure function that builds it,
`BuildRenderGraphMetadata()` — consuming exactly the three data sources
`RenderGraphPanel::Build()` currently reads directly (two
`RenderGraphSnapshot` regimes, a `std::vector<GpuDrivenBatchDebugInfo>`, a
`std::vector<RenderFeatureDebugEntry>`) and producing one single object that
is simultaneously (a) everything the ImGui panel needs to draw (Phase 3),
(b) everything a `GET /render_graph` JSON response needs to serialize
(Phase 4), and (c) everything a Graphviz `.dot` exporter needs (Phase 3's
"Export DOT"). Nothing calls this new code from production yet — this phase
is purely additive, Tier-1-tested, and touches ZERO existing behavior.

## Step 2: The Situation

Confirmed by direct read (re-confirm every field name/type below against the
ACTUAL current header before writing any struct — this document paraphrases,
the header is authoritative):

- `RenderGraphSnapshot` (`RenderGraphSnapshot.h`):
  `passesInExecutionOrder` (`std::vector<RenderGraphPassSnapshot>`),
  `resources` (`std::vector<RenderGraphResourceSnapshot>`),
  `timingSlotBudgetExhausted` (`bool`).
- `RenderGraphPassSnapshot`: `name`, `isCulled`, `readNames`/`writeNames`
  (`std::vector<std::string>`), `readKinds`/`writeKinds`
  (`std::vector<ResourceKind>`, parallel-indexed to
  `readNames`/`writeNames`), `kind` (`PassKind`), `category`
  (`RenderPassCategory`), `drawKind` (`RenderPassDrawKind`), `viewScope`
  (`ViewScope`), `renderPassEvent` (`RenderPassEvent`), `tags`
  (`RenderPassTagMask`, raw `uint64_t`), `stats` (`PassGpuStats`:
  `DrawStats drawStats` + `GpuTimingSample timing`).
- `RenderGraphResourceSnapshot`: `name`, `isImported`,
  `firstUsePassIndex`/`lastUsePassIndex` (`std::int32_t`, `-1` = "never
  used", both index into `passesInExecutionOrder`'s own SURVIVING prefix
  only).
- `PHASE1`'s new `gte::rg::RenderGraphSnapshotFormatting.h` provides
  `FormatGpuTiming(const GpuTimingSample&)`, `JoinNames(...)` (unused by this
  phase's own JSON path — JSON keeps `readNames`/`writeNames` as a real
  array, never a joined string; `JoinNames()` stays an ImGui-only concern,
  PHASE3), `ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot&, std::int32_t)`,
  `ToString(ResourceKind)`, `ToString(ViewScope)`.
- `RenderGraphTypes.h` already provides `ToString(PassKind)`,
  `ToString(RenderPassCategory)`, `ToString(RenderPassDrawKind)`,
  `ToString(RenderPassEvent)` — reuse these directly, never reimplement.
- `RenderPassGroupRegistry.h`: `FindPassGroupIndexForTags(RenderPassTagMask) noexcept`
  -> `std::optional<std::size_t>`; `PassGroupLabelUiHeadingAt(std::size_t) noexcept`
  -> `const char*`. Resolve a pass's single `tagGroupLabel` as:
  `auto index = FindPassGroupIndexForTags(pass.tags); tagGroupLabel = index.has_value() ? std::optional<std::string>(PassGroupLabelUiHeadingAt(*index)) : std::nullopt;`
- `GpuDrivenBatchDebugInfo` (`src/Renderer/Culling/GpuDrivenBatchDebugInfo.h`,
  `namespace gte`): `batchName` (`std::string`), `instanceCount`
  (`std::uint32_t`), `visibleCount` (`std::optional<std::uint32_t>`).
- `RenderFeatureDebugEntry` (`src/Core/Plugins/RenderFeatureDebugEntry.h`,
  `namespace gte`): `name`, `stage`, `blendMode` (all `std::string`),
  `priority` (`std::int32_t`).
- `nlohmann::json`'s own ADL `to_json(nlohmann::json&, const T&)` free-
  function pattern (`src/ECS/Reflection/MathJsonAdapters.h`'s
  `to_json(nlohmann::json& j, const Vec3& v)` is the exact, already-used
  precedent) is how this phase exposes JSON-ability — never a member
  function, never `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` (this repo has zero
  existing use of that macro — a plain, explicit `to_json` free function per
  type, written out field-by-field, matches every existing precedent and
  keeps the exact JSON key names under this phase's own explicit control,
  which matters for an externally-consumed contract).

## Step 3: The Plan

### Step 3.1 — New files: `src/Renderer/RenderGraph/RenderGraphMetadata.h` + `.cpp`

```cpp
#pragma once

// editor-core-separation-7 campaign, PHASE2
// (PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md) - the single, JSON-able,
// engine-free "everything the Render Graph panel shows, in one object"
// reshape. Sits ONE LEVEL ABOVE RenderGraphSnapshot.h (never inside it - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #11): RenderGraphSnapshot
// keeps its own narrow, already-tested "one regime's worth of already-
// executed passes/resources" contract untouched; this file folds TWO
// regimes of that, PLUS GpuDrivenBatchDebugInfo, PLUS RenderFeatureDebugEntry,
// into one object, with every enum already resolved to a human string and
// every tag bitmask already resolved to at most one human label.
//
// BuildRenderGraphMetadata() is a PURE function of already-computed plain
// data - no live RenderGraph/VkDevice/Renderer/Core - directly Tier-1-
// testable with hand-fabricated inputs, exactly like BuildRenderGraphSnapshot()
// itself already is (see RenderGraphSnapshotTests.cpp's own precedent).
//
// This is the ONE object that simultaneously backs:
//   - RenderGraphPanel::Build()'s ImGui tables (PHASE3) - never a second,
//     independently-hand-maintained presentation path.
//   - GET /render_graph's JSON response body (PHASE4), via to_json() below.
//   - "Export DOT"'s Graphviz output (PHASE3), via
//     src/Editor/RenderGraphDotExport.h/.cpp (gte_editor-tier, NOT a
//     same-folder sibling of this file - see PHASE0_MASTER_STRATEGY.md's
//     Locked Design Decision #14) consuming this exact same struct.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../Culling/GpuDrivenBatchDebugInfo.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"

#include <nlohmann/json.hpp> // confirm the exact existing include path/style used by
                             // src/Scene/SceneJsonFormat.h or MathJsonAdapters.h - copy
                             // it verbatim rather than guessing "<nlohmann/json.hpp>"
                             // blind; some repos vendor this as "json.hpp" only.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {

// One resource REFERENCE from a pass's own reads/writes list - name +
// already-resolved ResourceKind string, parallel-array-free (unlike
// RenderGraphPassSnapshot's own readNames/readKinds parallel-vector shape) -
// this is deliberately a single array of small structs here, since a JSON
// consumer benefits from {"name":...,"kind":...} pairs far more than two
// same-length parallel arrays it has to zip itself.
struct RenderGraphResourceRefMetadata {
    std::string name;
    std::string kind; // rg::ToString(ResourceKind) - "Texture" | "Buffer" | "VolumeTexture" (confirm exact current enumerator set, PHASE1).
};

// One pass, fully presentation-ready - every enum already resolved to its
// ToString() text, every GpuTimingSample already resolved via
// RenderGraphSnapshotFormatting.h's FormatGpuTiming() (PHASE1), tagGroupLabel
// resolved via RenderPassGroupRegistry (at most ONE label - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #5).
struct RenderGraphPassMetadata {
    std::string name;
    bool isCulled = false;
    std::string kind;              // rg::ToString(PassKind)
    std::string category;          // rg::ToString(RenderPassCategory)
    std::string drawKind;          // rg::ToString(RenderPassDrawKind)
    std::string viewScope;         // rg::ToString(ViewScope) (PHASE1's new function)
    std::string renderPassEvent;   // rg::ToString(RenderPassEvent)
    std::optional<std::string> tagGroupLabel; // RenderPassGroupRegistry::FindPassGroupIndexForTags() result, or nullopt.
    std::vector<RenderGraphResourceRefMetadata> reads;
    std::vector<RenderGraphResourceRefMetadata> writes;
    std::uint32_t drawCallCount = 0;   // 0 for a culled pass (mirrors RenderGraphSnapshot's own "culled pass keeps stats at default" rule).
    std::uint32_t triangleCount = 0;   // same rule.
    std::string gpuTimingText;        // FormatGpuTiming() text - "N/A" for a culled pass too (its stats.timing is always default/Absent).
    std::optional<double> gpuTimingMilliseconds; // the SAME value, still numeric (nullopt unless GpuTimingSample::Status::Present), for a machine caller that wants a number, not a string to reparse.
};

// One resource, fully presentation-ready - the raw indices are KEPT (never
// removed - a caller cross-referencing this SAME response's own `passes`
// array positionally still wants them) but the referenced pass's NAME is
// ALSO resolved here, always, so this response is meaningful without the
// caller having to re-index anything (PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #10).
struct RenderGraphResourceMetadata {
    std::string name;
    bool isImported = false;
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;
    std::optional<std::string> firstUsePassName; // nullopt iff firstUsePassIndex < 0 ("never used").
    std::optional<std::string> lastUsePassName;  // nullopt iff lastUsePassIndex < 0.
};

// One ExecuteTimingMode regime's worth of already-formatted data.
struct RenderGraphRegimeMetadata {
    std::string regimeName; // "SynchronousImmediateReadback" | "PipelinedDeferredReadback" - the EXACT gte::rg::ExecuteTimingMode enumerator name, never a friendlier paraphrase, so a caller can round-trip this string back to the enum if it ever needs to.
    std::vector<RenderGraphPassMetadata> passes; // survivors first, then culled - matches RenderGraphSnapshot::passesInExecutionOrder's own order exactly.
    std::vector<RenderGraphResourceMetadata> resources;
    bool timingSlotBudgetExhausted = false;
};

// THE single, top-level, JSON-able object. See PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #1 for why offscreen/present are two NAMED fields,
// not an array, and Locked Design Decision #2 for why gpuDrivenBatches AND
// renderFeatures are both folded in here too (not just the two regimes).
struct RenderGraphMetadata {
    std::uint32_t schemaVersion = 1; // bump on any FUTURE breaking JSON shape change - see PHASE4.
    RenderGraphRegimeMetadata offscreenRegime; // ExecuteTimingMode::SynchronousImmediateReadback
    RenderGraphRegimeMetadata presentRegime;   // ExecuteTimingMode::PipelinedDeferredReadback
    std::vector<GpuDrivenBatchDebugInfo> gpuDrivenBatches;   // reused directly, never re-wrapped (Locked Design Decision #6).
    std::vector<RenderFeatureDebugEntry> renderFeatures;     // reused directly, never re-wrapped (Locked Design Decision #6).
};

// Pure, Tier-1-testable - takes already-resolved RenderGraphSnapshot/
// GpuDrivenBatchDebugInfo/RenderFeatureDebugEntry values, never a live
// RenderGraph&/Core&. `offscreen`/`present` map 1:1 onto
// RenderGraphMetadata::offscreenRegime/presentRegime.
RenderGraphMetadata BuildRenderGraphMetadata(const RenderGraphSnapshot& offscreen, const RenderGraphSnapshot& present,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatches,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures);

// ADL free function - nlohmann::json's own standard pattern (mirrors
// src/ECS/Reflection/MathJsonAdapters.h's to_json(nlohmann::json&, const Vec3&)
// precedent exactly).
void to_json(nlohmann::json& j, const RenderGraphMetadata& metadata);

} // namespace gte::rg

namespace gte {

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6 - these two
// otherwise-homeless small structs get their OWN to_json() here, in
// namespace gte (matching each struct's own namespace, for correct ADL),
// physically inside RenderGraphMetadata.cpp, rather than adding a
// nlohmann::json include to GpuDrivenBatchDebugInfo.h/RenderFeatureDebugEntry.h
// themselves (keeping both exactly as dependency-free as their own doc
// comments already promise).
void to_json(nlohmann::json& j, const GpuDrivenBatchDebugInfo& info);
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry);

} // namespace gte
```

### Step 3.2 — `RenderGraphMetadata.cpp` body

**`BuildRenderGraphMetadata()`**: a small private helper,
`RenderGraphRegimeMetadata BuildRegimeMetadata(const char* regimeName, const RenderGraphSnapshot& snapshot)`,
does the real per-regime work (called twice, once per regime, with the
literal strings `"SynchronousImmediateReadback"`/`"PipelinedDeferredReadback"` —
confirm these are the EXACT current `ExecuteTimingMode` enumerator names by
reading `RenderGraph.h` directly before hardcoding either string):

1. For each `RenderGraphPassSnapshot` in `snapshot.passesInExecutionOrder`
   (in order, unchanged): fill `name`/`isCulled` directly;
   `kind = rg::ToString(pass.kind)`; `category = rg::ToString(pass.category)`;
   `drawKind = rg::ToString(pass.drawKind)`; `viewScope = rg::ToString(pass.viewScope)`
   (PHASE1's new function); `renderPassEvent = rg::ToString(pass.renderPassEvent)`;
   `tagGroupLabel` via `FindPassGroupIndexForTags(pass.tags)` +
   `PassGroupLabelUiHeadingAt(*index)` (Step 2 above) — `std::nullopt` if
   the optional index is empty.
2. `reads`/`writes`: zip `pass.readNames`/`pass.readKinds` (same length,
   guaranteed by `RenderGraphPassSnapshot`'s own documented invariant — assert
   or defensively `std::min()`-clamp the loop bound if you want extra safety,
   but this invariant is a hard, load-bearing contract elsewhere in the
   engine, not something this phase needs to newly defend against) into
   `RenderGraphResourceRefMetadata{name, rg::ToString(kind)}`; same for
   `writeNames`/`writeKinds`.
3. `drawCallCount`/`triangleCount` from `pass.stats.drawStats` — always `0`
   for a culled pass (its `stats` is left at default, per
   `RenderGraphSnapshot`'s own existing, documented rule — do not special-
   case this here, it falls out naturally from reading the already-default
   struct).
4. `gpuTimingText = rg::FormatGpuTiming(pass.stats.timing)` (PHASE1).
   `gpuTimingMilliseconds = (pass.stats.timing.status == GpuTimingSample::Status::Present) ? std::optional<double>(pass.stats.timing.milliseconds) : std::nullopt;`
   — confirm `GpuTimingSample`'s exact field name for the millisecond value
   (`milliseconds`, per `RenderGraphPanel.cpp`'s own existing
   `timing.milliseconds` read) before writing this line.
5. For each `RenderGraphResourceSnapshot` in `snapshot.resources`: fill
   `name`/`isImported`/`firstUsePassIndex`/`lastUsePassIndex` directly;
   `firstUsePassName = (firstUsePassIndex < 0) ? std::nullopt : std::optional<std::string>(rg::ResolvePassNameAtSurvivingIndex(snapshot, firstUsePassIndex))`
   (PHASE1's relocated function) — same pattern for `lastUsePassName`.
6. `timingSlotBudgetExhausted = snapshot.timingSlotBudgetExhausted` (direct
   copy).

`BuildRenderGraphMetadata()` itself: `schemaVersion = 1` (a literal, never a
computed value — bump this literal by hand in a FUTURE breaking-change
phase, never in this one); `offscreenRegime = BuildRegimeMetadata("SynchronousImmediateReadback", offscreen)`;
`presentRegime = BuildRegimeMetadata("PipelinedDeferredReadback", present)`;
`gpuDrivenBatches = gpuDrivenBatches` (direct copy of the parameter — the
parameter name and field name legitimately collide, disambiguate with
`this->`-style qualification or a differently-named parameter, whichever
this file's own existing style prefers — check `RenderGraphSnapshot.cpp`'s
own parameter-naming convention first); `renderFeatures = renderFeatures`
(same direct-copy note).

**`to_json(nlohmann::json&, const RenderGraphMetadata&)`**: field-by-field,
explicit, `snake_case` JSON keys (matching every EXISTING `NetworkRoutes.cpp`
JSON body's own key convention, e.g. `"frames_since_update"`,
`"has_depth"` — never `camelCase` in the JSON itself, even though the C++
struct fields are `camelCase`):

```cpp
void to_json(nlohmann::json& j, const RenderGraphPassMetadata& pass) {
    j = nlohmann::json{
        {"name", pass.name}, {"is_culled", pass.isCulled}, {"kind", pass.kind},
        {"category", pass.category}, {"draw_kind", pass.drawKind}, {"view_scope", pass.viewScope},
        {"render_pass_event", pass.renderPassEvent},
        {"tag_group_label", pass.tagGroupLabel.has_value() ? nlohmann::json(*pass.tagGroupLabel) : nlohmann::json(nullptr)},
        {"reads", pass.reads}, {"writes", pass.writes}, // RenderGraphResourceRefMetadata needs its OWN small to_json too - add it, same file, same style.
        {"draw_call_count", pass.drawCallCount}, {"triangle_count", pass.triangleCount},
        {"gpu_timing_text", pass.gpuTimingText},
        {"gpu_timing_milliseconds", pass.gpuTimingMilliseconds.has_value() ? nlohmann::json(*pass.gpuTimingMilliseconds) : nlohmann::json(nullptr)},
    };
}
```

(This is illustrative shorthand for the exact field list — write the REAL
version reading every field from Step 3.1's own struct definitions directly,
including small `to_json` overloads for `RenderGraphResourceRefMetadata`,
`RenderGraphResourceMetadata`, and `RenderGraphRegimeMetadata` following the
identical explicit-field-list style, all inside `RenderGraphMetadata.cpp`,
all in `namespace gte::rg`.) The TOP-level `to_json(nlohmann::json&, const RenderGraphMetadata&)`
produces:

```json
{
  "schema_version": 1,
  "offscreen_regime": { "regime_name": "SynchronousImmediateReadback", "passes": [...], "resources": [...], "timing_slot_budget_exhausted": false },
  "present_regime": { "regime_name": "PipelinedDeferredReadback", "passes": [...], "resources": [...], "timing_slot_budget_exhausted": false },
  "gpu_driven_batches": [ { "batch_name": "...", "instance_count": 0, "visible_count": null } ],
  "render_features": [ { "name": "...", "stage": "...", "priority": 0, "blend_mode": "..." } ]
}
```

`to_json(nlohmann::json&, const GpuDrivenBatchDebugInfo&)`:
`{"batch_name":..., "instance_count":..., "visible_count": (nullopt -> null)}`.
`to_json(nlohmann::json&, const RenderFeatureDebugEntry&)`:
`{"name":..., "stage":..., "priority":..., "blend_mode":...}`.

**A real, deliberate naming note**: `RenderGraphMetadata`'s own field is
`gpuDrivenBatches`/`renderFeatures` (C++, camelCase, matching this struct's
own sibling fields), but the JSON keys are `gpu_driven_batches`/
`render_features` (snake_case) — this is not an inconsistency to "fix", it
is this repo's own, already-established, universal convention (C++ member
names are always camelCase; every JSON key anywhere in `NetworkRoutes.cpp`
is always snake_case) applied consistently.

### Step 3.3 — New Tier-1 test file: `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp`

Mirror `RenderGraphSnapshotTests.cpp`'s own existing hand-fabrication style
(construct `RenderGraphSnapshot`/`RenderGraphPassSnapshot`/
`RenderGraphResourceSnapshot` values directly, no live `RenderGraph`).
Minimum cases:

- An empty `RenderGraphSnapshot` (no passes, no resources) for both regimes,
  empty `gpuDrivenBatches`/`renderFeatures` -> `BuildRenderGraphMetadata()`
  produces a `RenderGraphMetadata` with `schemaVersion == 1`, both regimes'
  `passes`/`resources` empty, both `gpuDrivenBatches`/`renderFeatures` empty.
- One surviving pass with real reads/writes/stats/tags matching a
  REGISTERED `RenderPassGroupRegistry` label (call
  `RegisterPassGroupLabel(...)` + `ResetPassGroupRegistryForTesting()` at the
  start/end of this test, mirroring whatever existing test in this repo
  already exercises that registry — `search_in_dir` for
  `ResetPassGroupRegistryForTesting` to find one) -> `tagGroupLabel` resolves
  to the expected string.
- One pass whose tags match NO registered label -> `tagGroupLabel` is
  `std::nullopt`.
- One CULLED pass -> `drawCallCount`/`triangleCount` are `0`,
  `gpuTimingText == "N/A"`, `gpuTimingMilliseconds` is `std::nullopt`, but
  `name`/`kind`/`category`/`drawKind`/`viewScope`/`renderPassEvent`/`reads`/
  `writes` are STILL correctly populated (a culled pass must still be fully
  describable — mirrors `RenderGraphSnapshot`'s own existing "a culled pass
  is still visible" rule).
- One resource with `firstUsePassIndex/lastUsePassIndex >= 0` -> both
  `firstUsePassName`/`lastUsePassName` resolve to the correct pass names.
- One resource with `firstUsePassIndex == -1` ("never used") ->
  `firstUsePassName`/`lastUsePassName` are both `std::nullopt`.
- A `GpuTimingSample` with `Status::Present` and a known `milliseconds` value
  -> `gpuTimingText` matches `FormatGpuTiming()`'s own exact formatting AND
  `gpuTimingMilliseconds` holds that same numeric value.
- `to_json(RenderGraphMetadata)` round-trip: build a small, fully-populated
  `RenderGraphMetadata` by hand, call `to_json`, and assert specific expected
  keys/values exist in the resulting `nlohmann::json` (e.g.
  `j["schema_version"] == 1`, `j["offscreen_regime"]["regime_name"] == "SynchronousImmediateReadback"`,
  a `null` JSON value for a `std::nullopt` field) — this is the test that
  actually proves the JSON CONTRACT, not just the intermediate C++ struct.

### Step 3.4 — CMake wiring

Add to the root `CMakeLists.txt`'s `gte_core` source list, immediately after
`RenderPassGroupRegistry.cpp` (confirmed current line 673 — re-confirm via
`search_in_dir` for `RenderPassGroupRegistry.cpp` before editing, in case
PHASE1's own new files shifted this line number):

```
src/Renderer/RenderGraph/RenderGraphMetadata.h
src/Renderer/RenderGraph/RenderGraphMetadata.cpp
```

Add to `tests/CMakeLists.txt`, immediately after
`Renderer/RenderGraph/RenderGraphSnapshotFormattingTests.cpp` (PHASE1's own
new entry):

```
Renderer/RenderGraph/RenderGraphMetadataTests.cpp
```

### Verification

1. Incremental build: `cmake --build build`.
2. Run the new test binary's relevant test cases (same single-file-run
   mechanism PHASE1 used) — confirm every case above passes.
3. No live-engine smoke test is needed for this phase — nothing in
   production code calls this new code yet (confirm via `search_in_dir` for
   `BuildRenderGraphMetadata(` outside `RenderGraphMetadata.cpp`/
   `RenderGraphMetadataTests.cpp` — must be ZERO hits, proving this phase is
   genuinely inert in the running Editor so far).
4. `git_status` — confirm the diff touches exactly the two new
   `RenderGraphMetadata.h/.cpp` files, the new
   `RenderGraphMetadataTests.cpp` file, and the two `CMakeLists.txt` files.

### What this phase does NOT do

- Does not change `RenderGraphPanel.cpp` at all — PHASE3.
- Does not add "Export DOT"/any Graphviz code — PHASE3.
- Does not touch `FrameCaptureBridge`/`NetworkRoutes.h`/`NetworkServer.cpp`/
  `EditorHost.cpp` — PHASE4.
- Does not decide the FINAL, locked, external JSON contract wording in
  stone forever — Phase 4/5's own live smoke test is the real, final proof
  this shape is genuinely useful to an external caller; if that phase
  surfaces a real problem with a key name/shape chosen here, fixing it here
  (before `schemaVersion` ever leaves this campaign) is cheap — after Phase 5
  ships, changing it means bumping `schemaVersion` to `2`.

### Completion

Write `PHASE2_COMPLETION_REPORT.md` (the new tests' pass/fail console
output, plus one full example `RenderGraphMetadata::to_json()` output for a
small hand-fabricated input, pasted verbatim into the report so a reviewer
can eyeball the real JSON shape without running anything), then `git_add` +
`git_commit`.
