# PHASE2 — Plugin Render Feature Enable/Disable + Live Priority Reorder — COMPLETION REPORT

**Status: DONE.** Implemented exactly what
`PHASE2_PLUGIN_RENDER_FEATURE_ENABLE_DISABLE_AND_PRIORITY.md` describes,
including Step 3.6's LOCKED decision (no new
`tests/Core/Plugins/RenderFeatureCompositorTests.cpp` file; extended the
existing `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` instead).
One small, confirmed, real deviation from the plan's literal file list — see
"Deviations" below.

## What changed

### `src/Core/Plugins/RenderFeatureCompositor.h`

- `Entry` gains `bool enabledOverride = true;` (host-side-only, never part of
  the plugin ABI), exactly matching Step 3.1's shown code verbatim.
- Two new public methods declared immediately after `DispatchOps()`:
  `bool SetFeatureEnabled(const std::string& name, bool enabled);` and
  `bool SetFeaturePriority(const std::string& name, std::int32_t priority);`.
- Two new private helpers declared alongside `EnsureTextureSized()`:
  `static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);`
  and `Entry* FindEntryByName(const std::string& name);`.

### `src/Core/Plugins/RenderFeatureCompositor.cpp`

- Added `#include <algorithm>` (was previously relying on a transitive
  include for `std::stable_sort`; `std::remove_if`/`std::find_if` used by the
  new code need it explicitly too).
- `OnPluginsLoaded()`'s former inline `sortAndDetectCollisions` lambda was
  moved OUT, VERBATIM, into the new `static void
  RenderFeatureCompositor::SortAndDetectCollisionsInStage(...)` method (same
  log lines, same tie-break, zero behavior change — confirmed by diffing the
  extracted body against the original before moving on). `OnPluginsLoaded()`'s
  own two call sites now read
  `SortAndDetectCollisionsInStage(m_postComposite, "PostComposite");` /
  `SortAndDetectCollisionsInStage(m_preUi, "PreUI");`.
- `FindEntryByName()`, `SetFeatureEnabled()`, `SetFeaturePriority()` added
  exactly matching Step 3.2's shown bodies (2-4) verbatim, including the
  `isPostComposite` re-derivation-from-container safety note and the
  "`entry` is invalidated by the re-sort, never dereferenced again after it"
  discipline.
- `ContributeRenderGraphPasses()`: immediately after the existing two
  `combinedList.insert(...)` calls and BEFORE the existing
  `if (combinedList.empty())` check, added:
  ```cpp
  combinedList.erase(std::remove_if(combinedList.begin(), combinedList.end(),
      [](const Entry& entry) { return !entry.enabledOverride; }), combinedList.end());
  ```
- `DebugSnapshot()`'s `appendStage` lambda gained
  `debugEntry.enabled = entry.enabledOverride;`, alongside the existing
  `debugEntry.name/stage/priority/blendMode` assignments.

### `src/Core/Plugins/RenderFeatureDebugEntry.h`

Added `bool enabled = true;` at the end of the struct (after `blendMode`,
never inserted in the middle), exactly matching Step 3.3.

### `src/Renderer/RenderGraph/RenderGraphMetadata.cpp`

`to_json(nlohmann::json&, const RenderFeatureDebugEntry&)`'s existing body
gained one new line, same `snake_case` style as the rest:

Before:
```cpp
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry)
{
    j = nlohmann::json{
        { "name", entry.name },
        { "stage", entry.stage },
        { "priority", entry.priority },
        { "blend_mode", entry.blendMode },
    };
}
```

After:
```cpp
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry)
{
    j = nlohmann::json{
        { "name", entry.name },
        { "stage", entry.stage },
        { "priority", entry.priority },
        { "blend_mode", entry.blendMode },
        { "enabled", entry.enabled },
    };
}
```

`GET /render_graph`'s `render_features[]` array now reports `"enabled"` on
every entry automatically — no new endpoint, no `NetworkRoutes`/
`NetworkServer` change needed, confirmed by re-reading `RenderGraphMetadata.h`
(unchanged, `renderFeatures` is `std::vector<RenderFeatureDebugEntry>` reused
directly).

### `src/Core/Core.h`

`GetRenderFeatureCompositor() const noexcept`'s declared return type widened
from `const RenderFeatureCompositor*` to `RenderFeatureCompositor*`. The
method itself stays `const`-qualified.

## Deviations from the plan

**One, real, confirmed-safe deviation, honestly recorded rather than
smoothed over:** the phase doc's Step 3.5 assumed
`GetRenderFeatureCompositor()`'s definition lives in `Core.cpp` ("Update
`Core.cpp`'s matching definition's own return-type in its signature the same
way"). Direct inspection of the REAL, current `Core.h` before editing showed
this is **not the case** — the whole method (`{ return
m_renderFeatureCompositorPtr; }`) is defined INLINE, directly inside
`Core.h`, with no separate out-of-line definition in `Core.cpp` at all
(confirmed: `search_in_dir` for `"GetRenderFeatureCompositor"` across
`src/Core/Core.cpp` only turns up two comment mentions, never a real
function-definition signature). This means **`Core.cpp` needed ZERO changes
for this phase** — the entire return-type widening is a single-line edit,
100% inside `Core.h`. This is a strictly smaller diff than the plan assumed,
not a different behavior — `Core.cpp` is simply absent from this phase's
real, final `git_status` diff, which is fully consistent with the phase's own
"Verification" §3 wording ("confirm the diff touches EXACTLY: ... `Core.h`,
`Core.cpp`, ...") read as an upper bound (a maximum allowed file set), not a
mandatory-minimum one — no new/different file outside that named set was
touched.

Everything else matches the plan's own shown code verbatim — every new
method's signature, every insertion point, and the `RenderFeatureDebugEntry.h`
field placement (appended, not inserted in the middle) match Step 3.1-3.4
exactly.

### Step 3.6 — locked decision followed exactly, confirmed explicitly

- **No new test file was created.** `tests/Core/Plugins/
  RenderFeatureCompositorTests.cpp` does NOT exist and was NOT created —
  confirmed by this phase never writing to that path.
- **No `tests/CMakeLists.txt` change was made** — confirmed by `git_status`
  below (that file does not appear in the diff at all).
- `SetFeatureEnabled()`/`SetFeaturePriority()`/`FindEntryByName()`/
  `SortAndDetectCollisionsInStage()`/`ContributeRenderGraphPasses()`'s new
  filter remain **Tier 2, with NO automated coverage added this phase** — this
  is the LOCKED, already-resolved decision Step 3.6 describes (constructing a
  real `RenderFeatureCompositor` needs a live/headless `Renderer&`, and this
  development machine's one headless-construction test
  (`tests/Core/CoreHeadlessConstructionTests.cpp`) already self-`GTEST_SKIP()`s
  here per `AGENTS.md`). These 5 pieces of new mutator logic are exercised
  end-to-end ONLY by PHASE5's own live HTTP smoke test, against a real,
  running `GreatTamanaEditor.exe` — not by this phase.
- Instead, `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` (the
  file this phase WAS told to extend) got real, passing Tier-1 coverage for
  the plain-data serialization half:
  - `RenderGraphMetadataTest.GpuDrivenBatchesAndRenderFeaturesAreCopiedThroughUnchanged`
    — added `feature.enabled = false;` to the hand-built
    `RenderFeatureDebugEntry`, plus `EXPECT_FALSE(metadata.renderFeatures[0].enabled);`.
  - `RenderGraphMetadataTest.ToJsonProducesExpectedTopLevelShapeAndNullHandling`
    — added `feature.enabled = true;` (the struct's own default, made
    explicit so this test also proves the true-case round-trips), plus
    `EXPECT_TRUE(j["render_features"][0]["enabled"].get<bool>());`.
  - Both tests together now exercise BOTH boolean values through BOTH the
    plain-struct-copy path (`BuildRenderGraphMetadata()`) AND the real
    `to_json()` JSON-serialization path — this is the genuine regression
    proof that `GET /render_graph`'s new `"enabled"` field actually
    serializes correctly, exactly as Step 3.6 asks for.

## Verification evidence

1. **`git_status` at start**: branch was `feature/editor-core-separation`,
   working tree clean (PHASE1's diff was already committed, nothing
   outstanding) — confirmed before touching any file.

2. **Incremental build**:
   - `cmake --build build --target GreatTamanaEngineTests -j 8` — succeeded,
     26 build steps, touching exactly the expected `gte_core`/`gte_editor`
     translation units that transitively include the changed headers
     (`RenderFeatureCompositor.cpp`, `RenderGraphMetadata.cpp`, `Core.cpp`,
     `NetworkRoutes.cpp`, `NetworkServer.cpp`, `EditorHost.cpp`,
     `ImGuiEditorLayer.cpp`, `RenderGraphPanel.cpp`, `FrameDebuggerPanel.cpp`,
     `GBufferValidation.cpp`, `RenderGraphDotExport.cpp`,
     `PluginRenderPassBuilderAdapter_v2.cpp`, `NullEditorLayer.cpp`,
     `LegacyRenderFeatureOrchestrator.cpp`, `FrameCaptureBridge.cpp`, plus the
     touched/new test `.cpp` files) — a genuine incremental build, not a full
     clean one.
   - `cmake --build build --target GreatTamanaEditor -j 8` — succeeded (only
     `main.cpp` recompiled + relink), confirming the real production
     executable (not just the test binary) compiles and links cleanly against
     the changed `RenderFeatureCompositor.h`/`Core.h`.

3. **Targeted `ctest` run** (`ctest -C Debug -R "RenderGraphMetadata"
   --output-on-failure`, from `build/`): **13/13 tests passed** — all 11
   pre-existing `RenderGraphMetadataTest.*` cases (2 of them now extended
   with the new `enabled` assertions, both still green) plus the 2
   pre-existing `BuildRenderGraphMetadataResponseJsonTests.*` cases,
   unmodified and still passing.

4. **`git_status` immediately before this commit**: diff touches exactly —
   `src/Core/Core.h`, `src/Core/Plugins/RenderFeatureCompositor.cpp`,
   `src/Core/Plugins/RenderFeatureCompositor.h`,
   `src/Core/Plugins/RenderFeatureDebugEntry.h`,
   `src/Renderer/RenderGraph/RenderGraphMetadata.cpp`,
   `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` — all modified,
   nothing else. In particular, confirmed NOT touched:
   `src/Renderer/RenderGraph/RenderGraphMetadata.h` (unchanged, as required),
   `plugins/gte_plugin_abi/RenderFeatureDescriptor.h` (unchanged, plugin ABI
   untouched), `tests/CMakeLists.txt` (unchanged, no new test file), and
   `src/Core/Core.cpp` (unchanged — see "Deviations" above for why).

No live/HTTP/screenshot-based verification was performed for this phase —
correctly out of scope per the phase's own "What this phase does NOT do":
`SetFeatureEnabled()`/`SetFeaturePriority()` are not called by any production
code path yet (PHASE4/PHASE5's job), so there is no new observable behavior
to visually confirm from a running Editor this phase.

## Honest notes for future phases

- `Entry::enabledOverride` defaults `true` — every currently-loaded plugin
  behaves exactly as before this phase until PHASE5's HTTP bridge (or a test)
  explicitly calls `SetFeatureEnabled(name, false)`.
- A disabled plugin render feature is skipped only inside
  `ContributeRenderGraphPasses()` — it is NEVER removed from
  `m_postComposite`/`m_preUi`, so it never disappears from `DebugSnapshot()`/
  `GET /render_graph`'s `render_features[]` array — exactly the documented
  asymmetry versus a disabled BUILT-IN pass (PHASE0's Step 2.4), now
  confirmed true in the real, shipped code.
- `SetFeaturePriority()`'s re-sort invalidates the `Entry*` returned by
  `FindEntryByName()` immediately afterward — confirmed the final method body
  never dereferences it again after `SortAndDetectCollisionsInStage()` is
  called, matching the phase doc's own explicit warning.
- PHASE5 is the first phase that will actually exercise
  `SetFeatureEnabled()`/`SetFeaturePriority()` at runtime, against a real,
  live `GreatTamanaEditor.exe` (per Step 3.6's locked Tier-2 decision) — until
  then these two methods are compiled, but genuinely never called, in
  production.
