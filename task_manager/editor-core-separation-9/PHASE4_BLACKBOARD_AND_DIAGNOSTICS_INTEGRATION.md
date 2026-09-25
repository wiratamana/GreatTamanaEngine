# editor-core-separation-9 — PHASE4: Blackboard & Diagnostics Integration

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it FIRST, in full, especially
Step 2.7 and Locked Architecture Decision #13. Read `PHASE2_COMPLETION_REPORT.md`
and `PHASE3_COMPLETION_REPORT.md` before starting.

**Use `ask_questions`** whenever a real design ambiguity comes up that
`PHASE0_MASTER_STRATEGY.md` or this file does not already resolve. If you
delegate any further work, that work must also be told to use
`ask_questions`.

This phase has two genuinely different halves — build the blackboard (real,
new code), then VERIFY diagnostics integration (mostly confirmation, not new
plumbing — see Step 2.7 of the master strategy for why). Do not over-build
the second half; if the live check in Step 3.3 below already looks right,
say so plainly and stop, rather than inventing extra UI work with no real
requirement behind it.

## Step 1: The Goal

1. Ship a real, working `IPluginBlackboard` (`Publish`/`Fetch`, a fixed
   tagged-union `PluginBlackboardValue`) so two mutually-unaware `_v3`
   plugins can hand data to each other within one frame, replacing PHASE2's
   temporary stand-in.
2. VERIFY, live, that a `_v3` plugin's own declared passes are ALREADY
   generically visible through `GET /render_graph`/the "Render Graph"
   panel — and only add a small, additive UI/label improvement if the live
   check surfaces a real, genuine gap (never invent speculative panel work).

## Step 2: The Situation

Read before starting: `src/Renderer/RenderGraph/RenderPipeline.h`'s
`RenderPassBlackboard::Publish<T>(RenderPassId key, T value)`/`Fetch<T>(key)`
(the internal-engine precedent this phase's ABI-safe version mirrors in
SPIRIT, never in literal type — `std::any` cannot cross the ABI, per
`PublicSurface.md`), `src/Renderer/RenderGraph/RenderGraphMetadata.h/.cpp`
(already fully quoted in `PHASE0_MASTER_STRATEGY.md` Step 2.2/2.7 — re-read
the real file), and `src/Editor/Panels/RenderGraphPanel.cpp`'s "Plugin
Render Features" section (`search_in_dir` for `"Plugin Render Features"` to
find the exact line range — read the whole section, understand it iterates
`RenderFeatureDebugEntry` rows, one per LOADED PLUGIN).

**Restated finding from `PHASE0_MASTER_STRATEGY.md` Step 2.7 (do not
re-derive this from scratch, verify it instead):** a `_v3` plugin's pass is
a real `rg::PassRecord` with a real `debugName`, produced by the SAME
`rg::RenderGraphBuilder::AddRenderPass()` chokepoint every internal engine
pass uses. `RenderGraphCompiler`/`RenderGraphSnapshot`/`RenderGraphMetadata`/
the "Render Graph" panel's own PASS tables (as opposed to its separate
"Plugin Render Features" SECTION, which lists plugins, not passes) already
walk every real `PassRecord` generically, with no per-feature special-casing
anywhere (confirmed precedent: the `mrt-1` campaign's own finding that
`RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` needed ZERO changes for
an entirely new resource-write shape). This phase's job is to CONFIRM this
is still true for `_v3` specifically (a `_v3` pass is declared through a
slightly different code path than every prior pass — worth a real, live
check, not just an assumption), not to rebuild anything.

## Step 3: The Plan

### 3.1 — `IPluginBlackboard`'s real implementation

`RenderFeatureCompositor` gains one member,
`std::unordered_map<std::string, PluginBlackboardValue> m_blackboard;`,
cleared at the very START of `ContributeRenderGraphPasses()` (BEFORE the
`combinedList` construction — a fresh, empty blackboard every frame,
mirroring `rg::RenderPassBlackboard`'s own per-frame lifetime exactly).

A small, private nested (or file-local) class,
`RenderFeatureCompositorBlackboardAdapter final : public IPluginBlackboard`,
wraps a reference to this map:

```cpp
void Publish(const char* key, const PluginBlackboardValue& value) override {
    m_map[key] = value; // last-publish-wins, mirrors RenderPassBlackboard::Publish()'s own rule
}
bool Fetch(const char* key, PluginBlackboardValueKind expectedKind, PluginBlackboardValue& outValue) const override {
    auto it = m_map.find(key);
    if (it == m_map.end() || it->second.kind != expectedKind) {
        return false; // never guesses/coerces - matches the ABI's own documented contract
    }
    outValue = it->second;
    return true;
}
```

`PluginRenderPassBuilderAdapter_v3::Blackboard()` returns a reference to one
instance of this adapter (constructed once, held by `RenderFeatureCompositor`
itself alongside `m_blackboard`, handed to every `_v3` adapter constructed
this frame — NOT re-constructed per plugin, since it is a thin wrapper over
the ONE shared per-frame map every `_v3` plugin this frame must see the
SAME contents of). Replace PHASE2's stand-in/empty implementation with this
real one now.

### 3.2 — A real, minimal 2-plugin blackboard proof

Extend `plugins/demo_render_feature_v3/` (or add one more tiny `_v3` demo
plugin if that is structurally cleaner — implementer's choice, record which
in the completion report) so ONE plugin `Publish()`es a small value (e.g.
`"DemoV3.BlurStrength"`, a `Float` kind) and a SECOND, independently loaded
`_v3` plugin `Fetch()`es that exact key and asserts (via a
`GTE_LOG_WARNING`-on-mismatch check, not a crash) that it got the expected
value — proving two mutually-unaware plugins really did hand data to each
other within one frame, with neither one's own code needing to change when
the other was added (Design Doc's own "neither plugin's own code needed to
change when the other was added" framing, restated for this concrete case).
A live `GET /get_logs` check after a run confirms the fetch succeeded (log a
`GTE_LOG_INFO` line on success too, so this is confirmable without needing a
crash-on-failure to prove a pass).

### 3.3 — Live diagnostics-integration verification (mostly confirmation)

1. `run_app_background` `GreatTamanaEditor.exe` with both `_v3` demo
   plugins loaded (from Steps 3.2/PHASE3).
2. `gte_send_request GET /activate_tab?name=Render%20Graph` then `GET
   /get_swapchain` (or whatever capture endpoint shows the Editor UI itself)
   — visually confirm the "Render Graph" panel's ordinary pass tables show
   `"DemoV3.Downsample"`/`"DemoV3.UpsamplePresent"` (PHASE3's own pass names)
   exactly like any other real pass, with correct `kind`
   (Compute/Graphics), reads/writes, and (if GPU timing is enabled) a
   plausible timing value — NO special-casing needed, confirming Step 2's
   restated finding.
3. `gte_send_request GET /render_graph` — confirm the same two pass names
   appear in the JSON response body's `offscreen_regime.passes`/
   `present_regime.passes` arrays (these are the REAL, snake_case JSON keys
   `RenderGraphMetadata.cpp`'s own `to_json()` actually emits —
   `RenderGraphMetadata::offscreenRegime`/`presentRegime` are only the C++
   field names, confirmed by direct reading of
   `src/Renderer/RenderGraph/RenderGraphMetadata.h/.cpp`; searching the JSON
   body for the camelCase spelling will find nothing — whichever regime the
   Game/Scene view's offscreen graph is), with
   `reads`/`writes` correctly naming `"SceneColor"` (or whatever
   `CreateTexture`-chosen debug name PHASE3 used for the transient half-res
   texture) — confirm `RenderGraphResourceRefMetadata`'s `name` field
   reflects the PLUGIN'S OWN literal debug name string, never an anonymous
   one (Design Doc R23).
4. Confirm the EXISTING "Plugin Render Features" section still lists BOTH
   `_v3` demo plugins by name/stage/priority/blendMode/enabled, exactly like
   any `_v2` plugin, unmodified (`RenderFeatureDebugEntry`'s existing 5
   fields already cover this — `moduleV3` entries populate the identical
   struct the identical way `moduleV2` entries already do, since
   `DebugSnapshot()`'s own `appendStage` lambda reads `entry.descriptor`/
   `entry.enabledOverride` only, never branching on which module pointer is
   set).
5. **If, and only if,** this live check finds ANY real, concrete
   discoverability gap (e.g. "a reader looking at the Plugin Render
   Features section has no way to tell THIS ROW is a `_v3` (multi-pass)
   plugin vs. a `_v2` (fixed-op) plugin, and that distinction is genuinely
   useful to know") — add ONE small, additive fix: a `bool isV3` field on
   `RenderFeatureDebugEntry` (populated by `DebugSnapshot()`'s existing
   `appendStage` lambda, trivial to add: `debugEntry.isV3 = (entry.moduleV3
   != nullptr);`), surfaced as a small text label ("v3") next to the
   existing row in `RenderGraphPanel.cpp`'s "Plugin Render Features" table,
   and threaded into `RenderGraphMetadata.cpp`'s existing
   `to_json(nlohmann::json&, const RenderFeatureDebugEntry&)` (one more
   JSON field). This is the ONLY new data-plumbing this phase is allowed to
   add, and only if Step 4 genuinely shows it is useful — do not add it
   speculatively if the live check already looks clear enough without it.
6. `gte_send_request GET /get_logs?limit=200` — confirm zero unexpected
   warnings/errors. `stop_app_background` when done.

## Verification

- `cmake --build build` (incremental).
- The blackboard proof (Step 3.2) and diagnostics live check (Step 3.3),
  both with real evidence captured (log excerpts, JSON response bodies,
  screenshots) directly in the completion report.

## Non-Goals for this phase

- No new HTTP endpoint (the existing `GET /render_graph` already carries
  everything this phase needs — Non-Goal restated from `PHASE0_MASTER_STRATEGY.md`).
- No change to `RenderFeatureBlendMode`/`RenderFeatureStage`.
- No change to how `_v2` plugins are diagnosed/displayed (their own
  `RenderFeatureDebugEntry` rows are unaffected except for gaining
  `isV3=false`, if that field is added at all).

## Completion

Write `PHASE4_COMPLETION_REPORT.md`: the blackboard implementation summary,
the 2-plugin proof evidence, the full diagnostics live-check evidence
(explicitly state whether Step 3.3.5's optional `isV3` field was added, and
why/why not), and any `ask_questions` round needed. `git_add` + `git_commit`
("editor-core-separation-9 PHASE4: blackboard & diagnostics integration").
