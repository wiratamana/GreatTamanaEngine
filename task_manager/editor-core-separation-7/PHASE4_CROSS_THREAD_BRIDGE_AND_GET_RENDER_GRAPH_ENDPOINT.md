# PHASE4 — Cross-Thread Bridge + `GET /render_graph` HTTP Endpoint

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Design Decisions #3, #4, #7, #8, #9). Also read `PHASE1`/`PHASE2`/`PHASE3`
completion reports before starting. This phase is what actually makes the
feature reachable by an external/AI caller over HTTP — use `ask_questions`
for any genuine ambiguity, especially around the exact status-code mapping.

## Step 1: The Goal

Extend `FrameCaptureBridge` with a `PublishRenderGraphMetadata()`/
`GetPublishedRenderGraphMetadata()` pair (mirroring its existing
`PublishTextureList()`/`GetPublishedTextureList()` pair exactly), wire the
publish call into `EditorHost.cpp`'s real `Run()` loop once per frame, and
ship `GET /render_graph`: `200` + the JSON body from
`rg::RenderGraphMetadata::to_json()` on success, `503` if the bridge pointer
is null. By the end of this phase, `gte_send_request("/render_graph")`
against a live, running `GreatTamanaEditor.exe` returns real, current-frame
render-graph data as JSON — the actual, stated goal of this whole campaign.

## Step 2: The Situation

Confirmed by direct read, `src/Application/FrameCaptureBridge.h`'s existing
texture-list half (the exact shape this phase's own new pair mirrors):

```cpp
void PublishTextureList(std::vector<PublishedTextureListEntry> entries);           // main thread, once/frame, OVERWRITES wholesale.
std::vector<PublishedTextureListEntry> GetPublishedTextureList() const;            // network thread, never blocks, cheap copy.
private:
    mutable std::mutex m_textureListMutex; // independent of every Slot's own mutex.
    std::vector<PublishedTextureListEntry> m_publishedTextureList;
```

Confirmed, `src/Editor/EditorHost.cpp`'s real `Run()` loop (the actual
composition root — PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3),
the exact existing block this phase's own new publish call sits next to:

```cpp
{
    GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
    const RenderFeatureCompositor* renderFeatureCompositor = m_core.GetRenderFeatureCompositor();
    const std::vector<RenderFeatureDebugEntry> renderFeatureEntries =
        renderFeatureCompositor != nullptr ? renderFeatureCompositor->DebugSnapshot()
                                            : std::vector<RenderFeatureDebugEntry>{};
    m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
        m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries);
}
```

and, later in the SAME `Run()` iteration, the existing `PublishTextureList()`
call site:

```cpp
{
    const std::vector<rg::DebugTextureSnapshot> snapshots = m_renderGraph.ListDebugTextures();
    ...
    m_captureBridge.PublishTextureList(std::move(published));
}
```

**Load-bearing constraint (PHASE0_MASTER_STRATEGY.md's Locked Design
Decision #7): `RenderFeatureCompositor::DebugSnapshot()` may be called AT
MOST ONCE per `Run()` iteration.** `renderFeatureEntries` is ALREADY computed
in the first block above — this phase's own new metadata-building code must
REUSE that exact same local variable, never call `DebugSnapshot()` a second
time. This means this phase's own new code CANNOT simply live inside the
existing `PublishTextureList()` block (which runs later and has no access to
`renderFeatureEntries`, a variable local to the earlier scoped block above) —
either (a) widen `renderFeatureEntries`'s scope so it survives past its
current `{ }` block, or (b) build and publish the `RenderGraphMetadata` OBJECT
INSIDE that same first block, right after
`m_editorLayer->BuildUI(...)` returns, using `renderFeatureEntries` while it
is still in scope, and `m_core.GetGpuDrivenBatchDebugInfo()` called a SECOND
time there (this one is a cheap `const&` accessor with no "at most once"
restriction, confirmed by its own doc comment — re-reading it twice per frame
is harmless and already how OTHER code in this same file behaves, e.g. it is
read once here and the panel itself re-reads live values every frame too).
**Option (b) is the correct, minimal one** — it needs no scope-widening at
all, just three or four new lines appended directly after the existing
`BuildUI(...)` call, inside the SAME `{ }` block.

Confirmed, `src/Network/NetworkServer.h`'s constructor already takes a
`FrameCaptureBridge* captureBridge = nullptr` as its FIRST parameter — this
phase's new route needs NO new constructor parameter, NO new forward
declaration in `NetworkServer.h` (`class FrameCaptureBridge;` is already
forward-declared there) — it only needs a new method call inside the
already-existing `captureBridge` code paths.

Confirmed, `src/Network/NetworkServer.cpp`'s `RegisterListTexturesRoute()`
(quoted in full in PHASE0_MASTER_STRATEGY.md's Step 2) is the EXACT template
this phase's own new route registration function mirrors: null-check ->
`503` + `BuildGenericErrorResponseJson(...)` -> otherwise fetch published
data -> build the response JSON -> `res.set_content(..., "application/json")`.

## Step 3: The Plan

### Step 3.1 — `FrameCaptureBridge.h`/`.cpp` changes

`FrameCaptureBridge.h` gains (new `#include "../Renderer/RenderGraph/RenderGraphMetadata.h"` —
confirm this does NOT drag in anything Vulkan/httplib-heavy transitively that
would violate this file's own "Vulkan-free, Renderer-free, engine-free"
header comment; `RenderGraphMetadata.h` itself only depends on
`RenderGraphSnapshot.h`/`RenderGraphTypes.h`/`GpuDrivenBatchDebugInfo.h`/
`RenderFeatureDebugEntry.h`/`nlohmann::json` — all already plain-data/no-Vulkan
headers, confirmed PHASE2 — so this stays consistent with
`FrameCaptureBridge.h`'s own existing rule):

```cpp
// --- GET /render_graph support (editor-core-separation-7 campaign, PHASE4) ---
// Same shape as PublishTextureList()/GetPublishedTextureList() immediately
// above - guarded by its OWN small, dedicated mutex, independent of every
// other mutex in this class (m_textureListMutex, every Slot's own mutex).

// --- Called from the MAIN thread (EditorHost::Run()) only ----------
// Publishes a fresh, COMPLETE RenderGraphMetadata - OVERWRITES whatever was
// published before wholesale. Called once per real engine frame - see
// EditorHost.cpp's own wiring, Step 3.2 below. Independent of
// RenderGraphPanel's own "Pause" checkbox - ALWAYS the truly latest frame's
// real data (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9).
void PublishRenderGraphMetadata(rg::RenderGraphMetadata metadata);

// --- Called from the NETWORK thread (a route handler) only --------------
// A cheap, thread-safe COPY of whatever was last published - never blocks.
// Returns a default-constructed (schemaVersion == 1, everything else empty)
// RenderGraphMetadata if PublishRenderGraphMetadata() has never been called
// yet this session - a valid, normal state (e.g. queried before the very
// first Run() iteration completes), never an error - mirrors
// GetPublishedTextureList()'s own identical "empty vector, not an error"
// convention.
rg::RenderGraphMetadata GetPublishedRenderGraphMetadata() const;

private:
    ...
    mutable std::mutex m_renderGraphMetadataMutex;
    rg::RenderGraphMetadata m_publishedRenderGraphMetadata;
```

`.cpp` bodies mirror `PublishTextureList()`/`GetPublishedTextureList()`'s
own EXACT existing implementation shape (a `std::lock_guard`, a `std::move`
on publish, a plain copy-return on get) — `read_file` those two existing
function bodies first and copy the locking pattern verbatim, substituting
the new mutex/field names.

**Also update `FrameCaptureBridge.h`'s own file-level header comment** (the
one currently reading *"Deliberately Vulkan-free, Renderer-free, and
engine-free: it only ever moves plain `std::vector<std::uint8_t>` PNG bytes
(+ width/height ints) between..."*) — this statement is ALREADY inaccurate
today (it predates the texture-list `Publish*/Get*` pair added by
network-impl-4 and was never updated then) and would become doubly so once
a THIRD payload kind (render-graph metadata) is added without ever touching
this comment. Rewrite it to honestly describe all three payload kinds this
class now moves across the thread boundary (captured PNG images, the
published texture list, and the published render-graph metadata) — a small,
real, in-scope documentation-accuracy fix, not optional polish.

### Step 3.2 — `EditorHost.cpp` wiring

Directly inside the EXISTING `{ GTE_PROFILE_SCOPE("IEditorLayer::BuildUI"); ... }`
block (Step 2 above), appended immediately after the existing
`m_editorLayer->BuildUI(...)` call, still inside the same braces so
`renderFeatureEntries` is still in scope:

```cpp
m_captureBridge.PublishRenderGraphMetadata(rg::BuildRenderGraphMetadata(
    m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
    m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
    m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries));
```

Add `#include "../Renderer/RenderGraph/RenderGraphMetadata.h"` to
`EditorHost.cpp` (confirm it does not already transitively have this via
another include — check before adding a duplicate). **Do not** move this
call to run later in `Run()` alongside `PublishTextureList()`'s own block —
placing it here, right next to where `renderFeatureEntries` is already
computed, is both the least-code-churn option and satisfies Locked Design
Decision #7 (no second `DebugSnapshot()` call) by construction, without
needing to widen any variable's scope.

### Step 3.3 — `NetworkRoutes.h`/`.cpp` — the response builder

New declaration, `NetworkRoutes.h`, mirroring `BuildListTexturesResponseJson()`'s
own doc-comment style:

```cpp
// editor-core-separation-7 campaign, PHASE4 - GET /render_graph's response
// body. Takes the EXACT rg::RenderGraphMetadata FrameCaptureBridge published
// this session - unlike TextureListEntryView/PublishedTextureListEntry
// above, this crosses the Application/Network layer boundary AS-IS (no
// separate, nearly-identical Network-tier struct is introduced) because
// rg::RenderGraphMetadata::to_json() is ALREADY the fully-resolved,
// plain-scalars-and-strings JSON shape this route needs - there is no
// "resolve enums/pointers into strings first" step left for NetworkServer.cpp
// to do here, unlike TextureListEntryView's own reason for existing. Confirm
// this reasoning holds by re-reading RenderGraphMetadata.h's own struct
// definitions (PHASE2) before treating this as settled - if any field ever
// needs Network-tier-specific reshaping later, introduce a dedicated view
// struct then, mirroring TextureListEntryView, rather than fighting this
// decision after the fact.
std::string BuildRenderGraphMetadataResponseJson(const rg::RenderGraphMetadata& metadata);
```

`.cpp` body:

```cpp
std::string BuildRenderGraphMetadataResponseJson(const rg::RenderGraphMetadata& metadata)
{
    nlohmann::json body = metadata; // uses rg::to_json(nlohmann::json&, const RenderGraphMetadata&) via ADL.
    return body.dump();
}
```

`NetworkRoutes.h` needs `#include "../Renderer/RenderGraph/RenderGraphMetadata.h"`
now — confirm this is an acceptable NEW dependency for this header (it
already depends on plenty of engine-adjacent small structs, e.g.
`TextureListEntryView`'s own doc comment already references
`PublishedTextureListEntry`; this is a similar-weight addition, not a novel
category of dependency for this file).

### Step 3.4 — `NetworkServer.cpp` — the route registration

New function, mirroring `RegisterListTexturesRoute()` EXACTLY:

```cpp
// editor-core-separation-7 campaign, PHASE4 - GET /render_graph. No query
// parameters (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8).
void RegisterRenderGraphRoute(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/render_graph", [captureBridge](const httplib::Request&, httplib::Response& res) {
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("capture bridge not available"), "application/json");
            return;
        }
        const rg::RenderGraphMetadata metadata = captureBridge->GetPublishedRenderGraphMetadata();
        res.set_content(BuildRenderGraphMetadataResponseJson(metadata), "application/json");
    });
}
```

Call `RegisterRenderGraphRoute(server, captureBridge);` from wherever
`RegisterListTexturesRoute(server, captureBridge);` is currently called
(`NetworkServer.cpp`'s own route-registration entry point — `search_in_dir`
for `RegisterListTexturesRoute(` to find the exact call site), immediately
alongside it.

### Step 3.5 — `NetworkRoutesTests.cpp` — Tier-1 coverage

Mirror whatever existing test already covers `BuildListTexturesResponseJson()`
(`search_in_dir` for it in `tests/Network/NetworkRoutesTests.cpp`) — hand-
fabricate a small `rg::RenderGraphMetadata` (reusing PHASE2's own
`RenderGraphMetadataTests.cpp` fabrication helpers if that file exposes any
reusable ones, otherwise duplicate the small fabrication inline — a few lines
of test-only duplication here is acceptable, do not introduce a shared
production-code test-fixture header purely to avoid it), call
`BuildRenderGraphMetadataResponseJson()`, parse the result back with
`nlohmann::json::parse()`, and assert the expected keys/values are present
(`schema_version`, `offscreen_regime.regime_name`, etc.).

### Step 3.6 — `docs/conventions/networking.md` note

This phase does NOT write the final docs bullet yet (PHASE5 owns the
official, polished documentation update, after the live smoke test below
proves the real shape) — but if, while implementing this phase, the actual
shipped JSON shape differs from PHASE2's own sketch in any way, record the
REAL, final shape precisely in `PHASE4_COMPLETION_REPORT.md` so PHASE5 can
write accurate docs without re-deriving it from source a second time.

### Verification

1. Incremental build: `cmake --build build`.
2. Run the new `NetworkRoutesTests.cpp` cases.
3. Live, end-to-end smoke test — THE real proof this campaign works:
   `run_app_background` the real `GreatTamanaEditor.exe`.
   - `gte_send_request("/render_graph")` — confirm `200` and a JSON body
     matching PHASE2's own documented shape (`schema_version`, both regimes,
     `gpu_driven_batches`, `render_features`).
   - `gte_send_request("/activate_tab?name=Render%20Graph")`, then
     `gte_send_request("/get_swapchain")` + `load_image` to see the SAME
     frame's ImGui panel.
   - **Cross-check, by hand**: pick one real pass name visible in the
     screenshot's Offscreen Regime table, and confirm that EXACT pass name
     appears in the `/render_graph` JSON body's `offscreen_regime.passes`
     array, with matching `draw_call_count`/`gpu_timing_text` values (within
     the two calls' own frame-to-frame variance — GPU timing can legitimately
     drift by a fraction of a millisecond between two separate HTTP
     requests, this is expected, not a bug) — this cross-check is the actual
     proof the "one source of truth" goal from the Investigation document
     was genuinely met, not merely assumed.
   - `gte_send_request("/get_logs?limit=50")` — confirm no unexpected
     warning/error text was logged by any of the above.
   - `stop_app_background` afterward.
4. `git_status` — confirm the diff touches exactly:
   `FrameCaptureBridge.h/.cpp`, `EditorHost.cpp`, `NetworkRoutes.h/.cpp`,
   `NetworkServer.cpp`, and the `NetworkRoutesTests.cpp` addition.

### What this phase does NOT do

- Does not add any query-parameter filtering to `GET /render_graph` (Locked
  Design Decision #8).
- Does not touch `RenderGraphPanel.cpp` itself (Phase 3 already finished the
  panel's own migration) — this phase only adds a SECOND consumer
  (`EditorHost.cpp`'s new publish call) of the SAME `BuildRenderGraphMetadata()`
  function Phase 3 already calls for the panel.
- Does not write the final `docs/conventions/networking.md` bullet (PHASE5).
- Does not run a full build or full `ctest` pass (Workflow Rule 1 — Phase 5
  only).

### Completion

Write `PHASE4_COMPLETION_REPORT.md` (the real `GET /render_graph` JSON
response body from a live session, pasted verbatim, PLUS the matching
screenshot, PLUS the manual cross-check evidence described in Verification
step 3), then `git_add` + `git_commit`.
