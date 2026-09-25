# editor-core-separation-7 — PHASE0 MASTER STRATEGY

**Branch:** `feature/editor-core-separation` (stay on it — do not create a new branch).

**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

**This folder:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\task_manager\editor-core-separation-7`

**Source finding/proposal document (read this IN FULL before starting ANY phase):**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\better-render-graph-editor\RenderGraph_DataDriven_Editor_And_AI_Debug_Endpoint_Investigation_2026-09-25.md`
(referred to below as "the Investigation"). This master strategy narrows,
corrects, and in a few real places OVERRIDES the Investigation's own §5
proposal, based on live inspection of the actual current source (some of it
newer than the Investigation itself — see Locked Design Decision #2 below,
which is a real, evidence-based correction, not a style preference). Where
this document and the Investigation disagree, **this document wins**.

This document is the ORCHESTRATOR for this whole campaign. Every child phase
file (`PHASE1_*.md` .. `PHASE5_*.md`) in this same folder must be read
together with this file before starting work on that phase. Every phase
produces its own `PHASEn_COMPLETION_REPORT.md` in this same folder when done,
and ends with its own `git_add` + `git_commit`.

**Use `ask_questions` whenever a real design ambiguity comes up that this
master strategy or the relevant phase file does not already resolve — never
silently guess. This applies transitively: if a phase (or any task it
delegates) itself ever hands off further work, that further work must also be
told to use `ask_questions` for its own genuine ambiguities.**

---

## Step 1: The Goal (Where are we going?)

Today, the Editor's "Render Graph" panel (`src/Editor/Panels/RenderGraphPanel.cpp`)
is the ONLY consumer of "what did the render graph actually do last frame" —
and it draws that data by reaching into THREE independent, differently-shaped
pieces of state every frame (two `rg::RenderGraphSnapshot` regimes, a
`std::vector<GpuDrivenBatchDebugInfo>`, and a
`std::vector<RenderFeatureDebugEntry>`), with real formatting/presentation
logic (`FormatGpuTiming()`, `JoinNames()`, `PassNameAtSurvivingIndex()`) baked
directly into the panel's own `.cpp` file. There is no JSON form of any of
this anywhere in the engine, and no way for an external tool — including an
AI agent doing LLM-driven debugging of this very engine — to ask "what did
the render graph do last frame" over the network the way it already can ask
"what does the screen look like" (`GET /get_game_view`) or "what did the
engine log" (`GET /get_logs`).

The goal of this campaign is to make the Render Graph panel genuinely
**data-driven** and ship a real `GET /render_graph` HTTP endpoint, so the
flow becomes:

```
RenderGraph (live C++ object, two regimes)  -\
GpuDrivenBatchDebugInfo (per-frame culling)   >-- rg::BuildRenderGraphMetadata() --> rg::RenderGraphMetadata (ONE plain, JSON-able struct)
RenderFeatureDebugEntry (loaded _v2 plugins) -/                                            |
                                                                                            +--> rendered by RenderGraphPanel (ImGui reads the metadata, draws it)
                                                                                            +--> served over GET /render_graph (an external/AI caller fetches the exact same metadata)
                                                                                            +--> exported as a real Graphviz .dot file ("Export DOT" button, finally implemented)
```

Concretely, this campaign ships:

1. A single, engine-free, ImGui-free, `nlohmann::json`-able C++ type,
   `gte::rg::RenderGraphMetadata` (new file,
   `src/Renderer/RenderGraph/RenderGraphMetadata.h/.cpp`), built by a pure
   function, `BuildRenderGraphMetadata()`, from already-computed data — no
   live `RenderGraph&`/`VkDevice`/`Renderer` involved, so it is directly
   Tier-1-testable exactly like `BuildRenderGraphSnapshot()` already is.
2. `RenderGraphPanel::Build()` refactored to build (or reuse a frozen) ONE
   `RenderGraphMetadata` per frame and draw its ImGui tables **from that**,
   never from the three raw sources directly — the literal meaning of
   "data-driven".
3. A real "Export DOT" implementation (the button has been disabled since
   Phase 8 of the original Render Graph campaign specifically waiting for
   this) — a pure function, `BuildRenderGraphDot()`, consuming the exact same
   `RenderGraphMetadata`.
4. A new, narrow, thread-safe publish/consume path so a `GET /render_graph`
   HTTP route (running on `NetworkServer`'s own background thread) can read
   the SAME metadata the Editor panel is showing, without ever touching
   `RenderGraph`/`Core`/`Renderer` directly from the network thread — this
   campaign EXTENDS the already-existing, already-reviewed
   `FrameCaptureBridge` (see Locked Design Decision #4 below for the
   evidence-based reason this is the right bridge, not a new one).
5. `GET /render_graph` itself: `200` + a versioned (`"schema_version":1`) JSON
   body on success, `503` if the bridge is null — following
   `src/Network/NetworkRoutes.h`'s own established "pure builder function,
   Tier-1-tested" shape exactly, and `docs/conventions/networking.md`'s own
   documentation convention (a new bullet describing the endpoint).

**This is read-only introspection of an ALREADY-EXECUTED graph.** This
campaign never touches how passes are declared/authored in C++
(`RenderGraphBuilder::AddPass()` etc.), never adds a way to replay/mutate a
graph from JSON, and is NOT the same thing as the Phase 9 backlog's
"data-driven/scripted pass declaration" idea
(`task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
item A.8) — that is about the *input* (how passes are declared); this
campaign is about the *output* (what already ran). Keep these two ideas
distinct in every phase's own writing.

## Step 2: The Situation (Where are we now?)

Confirmed by direct inspection of the real, current source (not guessed —
several of these facts are more current than the Investigation document
itself, see Locked Design Decision #2):

- `src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp` is the existing,
  already-Tier-1-tested (`tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`),
  pure, ImGui-free "flatten a compiled graph into displayable data" model.
  `RenderGraphSnapshot` = `passesInExecutionOrder` (surviving passes first,
  then culled, `isCulled` distinguishes them) + `resources` +
  `timingSlotBudgetExhausted`. `RenderGraphPassSnapshot` carries `name`,
  `isCulled`, `readNames`/`writeNames` (+ parallel `readKinds`/`writeKinds`,
  raw `ResourceKind` enum, NO existing `ToString(ResourceKind)` free function —
  confirmed by `search_in_dir`, zero hits), `kind` (`PassKind`), `category`
  (`RenderPassCategory`), `drawKind` (`RenderPassDrawKind`), `viewScope`
  (`ViewScope` — confirmed NO existing `ToString(ViewScope)` either, zero
  hits), `renderPassEvent` (`RenderPassEvent`), `tags` (`RenderPassTagMask`,
  raw `uint64_t`), and `stats` (`PassGpuStats`: `DrawStats` +
  `GpuTimingSample`). `RenderGraphResourceSnapshot` carries `name`,
  `isImported`, `firstUsePassIndex`/`lastUsePassIndex` (indices into THIS
  snapshot's own surviving-pass prefix, `-1` = "never used").
  `PassKind`/`RenderPassCategory`/`RenderPassDrawKind`/`RenderPassEvent` DO
  already have `const char* ToString(...)` free functions
  (`RenderGraphTypes.h`, confirmed by `search_in_dir`) — reuse them, never
  reimplement.
- `RenderGraph::LastSnapshot(rg::ExecuteTimingMode mode) const noexcept`
  (`RenderGraph.h`) returns one persistent `RenderGraphSnapshot` per regime —
  `ExecuteTimingMode::SynchronousImmediateReadback` (Game+Scene views) and
  `ExecuteTimingMode::PipelinedDeferredReadback` (swapchain Present) — these
  are, by `RenderGraph.h`'s own explicit, load-bearing top-of-file comment,
  the ONLY two regimes that will ever exist.
- `src/Editor/Panels/RenderGraphPanel.h/.cpp` — a small, deliberately
  STATEFUL class (same precedent as `ProfilerPanel`), NOT a stateless free
  function. `Build()`'s CURRENT real signature (confirmed by direct read —
  this is where the Investigation is stale, see Locked Design Decision #2):

  ```cpp
  void Build(EditorContext& ctx, const rg::RenderGraph& renderGraph,
      const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
      const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries);
  ```

  THREE independent data sources, not two. It draws, in this exact order:
  a GPU-Driven Batches section, a Plugin Render Features section, the
  Offscreen regime's pass+resource tables, the Present regime's pass+resource
  tables, and a disabled "Export DOT" button whose own tooltip reads
  *"Planned for Phase 9, once this panel's own ImGui-list data model has
  proven itself."* `FormatGpuTiming()`/`JoinNames()`/`PassNameAtSurvivingIndex()`
  are anonymous-namespace functions inside `RenderGraphPanel.cpp` — genuinely
  reusable pure logic, currently untested and unreachable from anywhere else.
- `src/Renderer/Culling/GpuDrivenBatchDebugInfo.h` — a plain, dependency-free,
  `namespace gte` struct (`batchName`, `instanceCount`,
  `std::optional<std::uint32_t> visibleCount`), produced by `Core::BuildFrame()`,
  read via `Core::GetGpuDrivenBatchDebugInfo() const noexcept`.
- `src/Core/Plugins/RenderFeatureDebugEntry.h` — a SECOND plain,
  dependency-free, `namespace gte` struct (`name`, `stage`, `priority`,
  `blendMode`, all already-resolved strings/ints), produced by
  `RenderFeatureCompositor::DebugSnapshot() const` — **this class's own
  header comment says "Safe to call at most once per Editor frame"** — this
  is load-bearing for Phase 4's wiring (see Locked Design Decision #7): the
  metadata bridge must REUSE the already-computed `renderFeatureEntries`
  vector the existing call site produces, never call `DebugSnapshot()` a
  second time in the same frame. Read via
  `Core::GetRenderFeatureCompositor() const noexcept` (may be null, always
  null-checked at the existing call site).
- `src/Renderer/RenderGraph/RenderPassGroupRegistry.h` —
  `FindPassGroupIndexForTags(RenderPassTagMask tags) noexcept` returns
  `std::optional<std::size_t>` (an index into registration order, resolved
  via `PassGroupLabelUiHeadingAt(index)`) for the FIRST registered tag bit set
  in `tags`, or `std::nullopt` — **this returns AT MOST ONE label, never a
  list** (a real, evidence-based correction to the Investigation's own §5.2
  proposed `std::vector<std::string> tagGroupLabels` field shape — see Locked
  Design Decision #5).
- `nlohmann::json` is already a `gte_core`-PUBLIC link dependency
  (`CMakeLists.txt`) and already used for on-disk serialization
  (`src/Scene/SceneJsonFormat.cpp`, `src/ECS/Reflection/MathJsonAdapters.h`'s
  own `to_json(nlohmann::json&, const Vec3&)` ADL-function precedent) — no
  new third-party dependency needed.
- `src/Application/FrameCaptureBridge.h/.cpp` already has a proven,
  battle-tested "publish once per frame from the main thread, read a
  thread-safe copy from the network thread, no blocking" pair:
  `PublishTextureList(std::vector<PublishedTextureListEntry>)` /
  `GetPublishedTextureList() const`, guarded by its own dedicated
  `mutable std::mutex m_textureListMutex` (deliberately no condition
  variable — nothing ever blocks on this).
- **The actual, single composition root that owns the real-time `Run()` loop
  is `src/Editor/EditorHost.cpp`, NOT `Application.cpp`** (a real, confirmed
  correction to the Investigation's own §5.4 code sketch, which guessed
  `Application::Run()` — see Locked Design Decision #3). `EditorHost` owns
  `m_captureBridge`, `m_renderGraph`, `m_core`, and every other bridge
  (`m_engineCommandBridge`, `m_uiCommandBridge`, `m_frameDebuggerCommandBridge`,
  `m_assetImportCommandBridge`), and its `Run()` method is what already calls
  `m_captureBridge.PublishTextureList(std::move(published))` once per real
  engine frame (confirmed exact call site, `EditorHost.cpp`, inside a block
  building `std::vector<PublishedTextureListEntry>` from
  `m_renderGraph.ListDebugTextures()`/`ListDebugVolumeTextures()`) — this
  exact same `Run()` iteration is where this campaign's own new publish call
  belongs (Phase 4). This is TRUE regardless of `GTE_ENABLE_EDITOR` — a
  `GTE_ENABLE_EDITOR=OFF` build still runs `EditorHost::Run()` with a
  `NullEditorLayer` degrading `BuildUI()`/panel drawing to a no-op, but every
  other line in that same `Run()` iteration (including the
  `renderFeatureEntries` computation and the texture-list publish block)
  still executes unconditionally — so `GET /render_graph` will work
  identically whether or not the Editor UI itself is compiled in.
- `src/Network/NetworkServer.h`'s constructor ALREADY takes SIX defaulted,
  non-owning bridge/capability pointers
  (`FrameCaptureBridge*`, `EngineCommandBridge*`, `EditorUiCommandBridge*`,
  `FrameDebuggerCommandBridge*`, `AssetImportCommandBridge*`,
  `ILogQueryCapability*`) — every one of them forward-declared in
  `NetworkServer.h`, `nullptr`-degrades-to-`503` at each route. Extending
  `FrameCaptureBridge` itself (Locked Design Decision #4) needs **zero**
  change to this constructor's signature, since `NetworkServer` already holds
  a `FrameCaptureBridge*`.
- `src/Network/NetworkRoutes.h/.cpp` — every route's request-parsing/
  response-building is a small, pure, `httplib`-independent function, each
  with its own Tier-1 test in `tests/Network/NetworkRoutesTests.cpp`
  (confirmed pattern: `BuildListTexturesResponseJson(const std::vector<TextureListEntryView>&)`,
  called from `NetworkServer.cpp`'s `RegisterListTexturesRoute()`, which
  copies a `PublishedTextureListEntry` into a `TextureListEntryView` ONE
  FIELD AT A TIME — `NetworkRoutes.h` NEVER accepts a struct owned by a
  different layer directly; this campaign follows the exact same "layer
  boundary" convention, see Locked Design Decision #6).
- The root `CMakeLists.txt`'s `gte_core` source list already has
  `src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp` at (confirmed) lines
  664-665, immediately followed by `RenderGraphDebugTextureRegistry.h/.cpp`,
  `RenderGraphDebugVolumeTextureRegistry.h/.cpp`, `RenderPipeline.h/.cpp`,
  `RenderPassGroupRegistry.h/.cpp` (lines 666-673) — the new
  `RenderGraphMetadata.h/.cpp` files are inserted right after this exact
  block (Phase 2). `tests/CMakeLists.txt` line 2184 lists
  `Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` — the new
  `RenderGraphMetadataTests.cpp` (Phase 2) and `NetworkRoutesTests.cpp`
  additions (Phase 4) are added in the same style, alongside their existing
  neighbors.
- `docs/conventions/networking.md` is a single, long, append-only file — one
  new bullet, at the end, documents `GET /render_graph` (mirroring how every
  prior endpoint from `network-impl-1` through `stl-parser-2` got its own
  bullet there) — there is no separate `docs/conventions/render-graph.md`
  file to create or touch.
- No engine code anywhere under `src/Renderer/RenderGraph/` has any
  `to_json`/`NLOHMANN_JSON_SERIALIZE_ENUM` usage today (confirmed, zero hits) —
  every JSON conversion this campaign needs is new code.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions (resolved before writing any phase file — do not re-litigate; challenge via `ask_questions` only if real code contradicts one)

1. **The two `ExecuteTimingMode` regimes are two NAMED fields on
   `RenderGraphMetadata`** (`offscreenRegime` / `presentRegime`), never a
   `std::vector<RenderGraphRegimeMetadata>` searched by a string field. There
   are, by permanent, documented engine design, exactly two regimes — a fixed
   shape is more useful to both a human reading raw JSON and an AI client
   than an array. Additive if a third regime is EVER added (extremely
   unlikely, per `RenderGraph.h`'s own top comment).
2. **`RenderGraphMetadata` folds in ALL THREE of the panel's current data
   sources — `RenderGraphSnapshot` (both regimes), `GpuDrivenBatchDebugInfo`,
   AND `RenderFeatureDebugEntry`** — not just the first two the Investigation
   document describes. The Investigation's own §2.1/§2.4/§5.2 only names TWO
   sources (snapshot + GPU-driven batches) because the "Plugin Render
   Features" section (`RenderFeatureDebugEntry`, `editor-core-separation-6`
   campaign's PHASE7) was either not yet landed or not noticed at the time
   that document was written — direct inspection of the CURRENT
   `RenderGraphPanel::Build()` signature (Step 2 above) proves there are
   three, not two. "Make the panel data-driven" is only true edge-to-edge if
   ALL of what it currently shows moves into the one metadata object — this
   correction is real and non-optional, not a style preference.
3. **The per-frame publish call for the network bridge (Phase 4) is added
   inside `src/Editor/EditorHost.cpp`'s existing `Run()` loop, immediately
   after the existing block that computes `renderFeatureEntries` and calls
   `m_editorLayer->BuildUI(...)`** — NOT inside `Application.cpp` (which does
   not own the real-time frame loop at all, contrary to the Investigation's
   §5.4 code sketch, which was written without having actually located the
   true composition root). This is a real, load-bearing correction, not a
   naming nitpick — a phase that looks for this wiring point in
   `Application.cpp` will not find it.
4. **The cross-thread bridge EXTENDS `FrameCaptureBridge`, it does NOT
   create a sixth bridge class.** The Investigation left this "weakly held,
   open" (§5.4); this master strategy resolves it, for two concrete,
   evidence-based reasons: (a) `NetworkServer`'s constructor ALREADY has SIX
   bridge/capability pointer parameters (Step 2 above) — a seventh would make
   an already-long constructor longer for a payload that is conceptually the
   SAME KIND of thing (`FrameCaptureBridge`'s own existing texture-list half
   already carries "already-computed, already-rendered-frame introspection
   data, published once per frame, read-only, no blocking" — render-graph
   metadata is exactly this, not a structurally different payload the way
   pixels/ECS-mutation/UI-focus/asset-import are each their own bridge); (b)
   `docs/conventions/networking.md`'s own rule ("a future endpoint needing
   DIFFERENT engine data must NOT extend `FrameCaptureKind` for an unrelated
   purpose") is specifically about the REQUEST/WAIT half's `FrameCaptureKind`
   enum (pixels) — it does NOT forbid extending the SAME class's already-
   established, already-precedented "publish list / get published list"
   half (used once already for the texture list) with a SECOND unrelated
   payload of that same shape. Two new methods,
   `PublishRenderGraphMetadata(rg::RenderGraphMetadata)` /
   `GetPublishedRenderGraphMetadata() const`, guarded by their OWN new,
   dedicated `mutable std::mutex m_renderGraphMetadataMutex` (never shared
   with `m_textureListMutex` or any `Slot`'s own mutex) — mirroring
   `PublishTextureList`/`GetPublishedTextureList`'s exact shape, one level
   further. **`FrameCaptureBridge.h`'s own file-level header comment
   ("Deliberately Vulkan-free, Renderer-free, and engine-free: it only ever
   moves plain `std::vector<std::uint8_t>` PNG bytes...") is ALREADY stale
   today** — it predates the texture-list `Publish*/Get*` pair (a
   `PublishedTextureListEntry` struct, not PNG bytes) and was never updated
   when that shipped. Phase 4 must update this comment to honestly describe
   THREE payload kinds (captured PNG images, the published texture list, and
   now the published render-graph metadata) instead of leaving it
   describing only the first — a small, real, in-scope documentation fix,
   not optional polish, since this campaign is the second time this exact
   comment goes further out of date if left untouched.
5. **`RenderGraphPassMetadata::tagGroupLabel` is a SINGLE
   `std::optional<std::string>`, not a `std::vector<std::string>`** — a real
   correction to the Investigation's §5.2 proposed shape
   (`std::vector<std::string> tagGroupLabels`), because
   `RenderPassGroupRegistry::FindPassGroupIndexForTags()` itself only ever
   resolves to AT MOST ONE registered label (the first-registered match) by
   its own documented, permanent contract — a vector field would silently
   promise a capability (showing every matched tag group) this engine's own
   registry does not provide, and a future implementer who trusted the
   Investigation's shape literally would either under-fill it or invent
   fictitious extra entries. `std::nullopt` (rendered as a JSON `null`, never
   an empty-string placeholder) means "untagged, or tagged but nobody
   registered a heading for it" — the common case today.
6. **`RenderGraphMetadata.h`'s new `to_json` overloads for
   `gte::GpuDrivenBatchDebugInfo` and `gte::RenderFeatureDebugEntry` are
   written directly against those EXISTING structs (no new duplicate
   wrapper type)** — both are already plain, dependency-free,
   already-resolved-scalar structs (confirmed, Step 2), so re-wrapping them
   into a NEW `RenderGraphGpuDrivenBatchMetadata`-style struct (as the
   Investigation's §5.2 sketch does) is unnecessary duplication. These two
   `to_json` free functions live in `namespace gte` (matching each struct's
   own namespace, for correct ADL), physically inside
   `RenderGraphMetadata.cpp`, clearly separated by a comment from
   `RenderGraphMetadata`'s own `namespace gte::rg` `to_json` — this is the
   ONE deliberate exception to "one namespace per file" in this campaign,
   justified because both structs are otherwise-homeless small POD types
   with no natural `to_json` home of their own, and neither
   `GpuDrivenBatchDebugInfo.h` nor `RenderFeatureDebugEntry.h` should gain a
   new `nlohmann::json` include purely for this (keeping both headers
   exactly as dependency-free as their own doc comments already promise).
7. **`RenderFeatureCompositor::DebugSnapshot()` is called AT MOST ONCE per
   `EditorHost::Run()` iteration, exactly like today** — Phase 4's new
   metadata-building code REUSES the `renderFeatureEntries` local variable
   the existing call site already computes (Step 2), it never adds a second
   `DebugSnapshot()` call. This is a hard constraint, not a style choice —
   `DebugSnapshot()`'s own header comment states the "at most once per
   Editor frame" rule explicitly.
8. **No new query parameters on `GET /render_graph` for v1** (no
   `?regime=`/`?pass=` filter) — a clean, additive extension once real usage
   shows it is wanted, matching this whole codebase's own documented
   discipline of not building speculative surface
   (`RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`).
9. **`GET /render_graph` is COMPLETELY INDEPENDENT of `RenderGraphPanel`'s own
   "Pause" checkbox.** Pause is a panel-local, ImGui-only freeze of what that
   ONE window displays (unchanged by this campaign) — the published bridge
   metadata always reflects the truly latest `EditorHost::Run()` iteration's
   real `Execute()` results, every frame, whether or not any Editor panel is
   even open. An AI/external caller polling `GET /render_graph` while a human
   has the panel paused still sees fresh, live data — this is intentional,
   not a bug to reconcile.
10. **`RenderGraphResourceMetadata` carries the referenced pass's NAME
    (`firstUsePassName`/`lastUsePassName`, `std::optional<std::string>`)
    directly, in addition to keeping the raw
    `firstUsePassIndex`/`lastUsePassIndex` integers** — the Investigation's
    gap #4 (indices are only meaningful within the SAME snapshot/response,
    never diffable across two captures by index alone) is fixed by ALWAYS
    resolving the name at build time (pure, cheap — the pass list is right
    there), while the raw index is kept too (harmless, occasionally useful
    for a caller cross-referencing `passes[i]` positionally within the SAME
    response). Never break `RenderGraphSnapshot`'s own existing
    index-only shape to do this — only the NEW `RenderGraphMetadata` layer
    adds the resolved name, `RenderGraphSnapshot` itself is untouched.
11. **`RenderGraphMetadata.h/.cpp` is a NEW, separate file, never folded into
    `RenderGraphSnapshot.h/.cpp`.** `RenderGraphSnapshot` already has its own
    narrow, well-understood, Tier-1-tested contract ("one regime's worth of
    already-executed passes/resources") — growing it to also know about
    JSON, GPU-driven batches, and plugin render features would be real scope
    creep into an already-proven, already-tested file. `RenderGraphMetadata`
    sits one level above it, `#include`-ing it, never the reverse.
12. **The shared presentation-formatting helpers (Phase 1) move into a NEW
    sibling file, `src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h/.cpp`
    — never merged directly into `RenderGraphSnapshot.h/.cpp` itself.** This
    keeps `RenderGraphSnapshot.h/.cpp`'s own existing, already-tested surface
    completely untouched (zero risk to its existing 15+ call sites in
    `RenderGraphSnapshotTests.cpp`) while still giving both
    `RenderGraphPanel.cpp` (ImGui) and `RenderGraphMetadata.cpp` (JSON) one
    single, shared, Tier-1-tested place to call for "turn a `GpuTimingSample`/
    empty-name-list/raw index into a human string" — resolving the
    Investigation's own §5.1 `[DECISION NEEDED]` explicitly in favor of the
    "keep `RenderGraphSnapshot.h/.cpp` itself free of any 'how a string
    should look' opinion" option.
13. **"Export DOT" writes a real `.dot` (Graphviz) file to a fixed, dependent
    working-directory path** (`render_graph_export.dot`, next to wherever the
    process's current working directory is — the SAME directory `GET
    /get_swapchain`-style debugging already implicitly assumes, no path
    picker) **and logs the resolved path via `GTE_LOG_INFO`** (never
    `printf`/`std::cout`) so a human OR an AI agent polling `GET /get_logs`
    can discover exactly where it landed. This repo has NO existing native
    file-save-dialog library (`search_in_dir` for `tinyfd`/`SaveFileDialog`
    under `src/Editor/` — zero hits) — inventing one is explicitly out of
    scope for this campaign; a fixed, predictable, always-overwritten path is
    the simplest correct behavior and is what an AI-driven debugging
    workflow actually wants (a stable, script-fetchable location) more than a
    human file-picker would.
14. **`RenderGraphDotExport.h/.cpp` (PHASE3) lives under `src/Editor/`
    (compiled into `gte_editor`), NOT under `src/Renderer/RenderGraph/`
    (`gte_core`) alongside `RenderGraphMetadata.h/.cpp`.** `RenderGraphMetadata.h/.cpp`
    itself MUST live in `gte_core` (Locked Design Decision #4/#11 — Phase 4's
    `FrameCaptureBridge`, a `gte_core`-tier file, needs to include it), but
    `RenderGraphDotExport.h/.cpp` has exactly ONE consumer, forever, by this
    campaign's own explicit scope: `RenderGraphPanel::Build()`'s "Export DOT"
    button (Phase 3) — no `gte_core`-tier code (`FrameCaptureBridge`/
    `NetworkRoutes.h`/`NetworkServer.cpp`) ever calls
    `BuildRenderGraphDot()`/`ExportRenderGraphDotToFile()` (confirmed: Phase 4's
    own "What this phase does NOT do" section). This engine already has a
    well-established, repeatedly-used precedent for EXACTLY this shape — a
    pure-or-near-pure, non-ImGui, Editor-button-triggered debug/validation/
    export tool that consumes `rg::`/`Renderer` types but has no consumer
    outside the Editor: `src/Editor/ComputeBlurValidation.h/.cpp`,
    `src/Editor/GBufferValidation.h/.cpp`,
    `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`, and
    `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp` are ALL
    real, current examples of exactly this pattern, and ALL four live under
    `src/Editor/`, compiled into `gte_editor`, per `AGENTS.md`'s own "Editor
    Module Structure"/"`gte_core`/`gte_editor` Library Separation" sections
    (`gte_editor` "owns... every panel" and the tooling that exists purely to
    serve one). `GreatTamanaEngineTests` already links `gte_editor` (not just
    `gte_core`), so a Tier-1 test for a `gte_editor`-tier pure function
    compiles and runs exactly like `tests/Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp`
    already does — there is no technical reason to place this file in
    `gte_core` at all, and doing so would be a real, avoidable layering
    inconsistency with this repo's own established convention.

### Phase map

- **`PHASE1_EXTRACT_FORMATTING_AND_LABEL_HELPERS.md`** — new file,
  `RenderGraphSnapshotFormatting.h/.cpp`: relocates `FormatGpuTiming()`,
  `JoinNames()`, a renamed `ResolvePassNameAtSurvivingIndex()`, plus TWO
  genuinely NEW small helpers this campaign needs that do not exist
  anywhere yet — `ToString(ResourceKind)` and `ToString(ViewScope)` (both
  confirmed missing from `RenderGraphTypes.h`, Step 2). Zero observable
  behavior change to `RenderGraphPanel` (byte-identical ImGui output) — a
  pure, safe, git-history-provable extraction, verified by a live
  before/after screenshot comparison, with real, NEW Tier-1 tests added for
  every relocated/new function (they had none before).
- **`PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md`** — new file,
  `RenderGraphMetadata.h/.cpp`: the `RenderGraphMetadata`/
  `RenderGraphRegimeMetadata`/`RenderGraphPassMetadata`/
  `RenderGraphResourceMetadata` types, `BuildRenderGraphMetadata()` (pure,
  Tier-1-testable with hand-fabricated `RenderGraphSnapshot`/
  `GpuDrivenBatchDebugInfo`/`RenderFeatureDebugEntry` inputs — no live
  `RenderGraph`), and every `to_json` overload (including the two ADL
  overloads for the borrowed structs, Locked Design Decision #6). Nothing
  calls this yet from production code — matches this repo's own repeated
  "phase N builds the pure logic, phase N+1 wires it up" pattern.
- **`PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md`** —
  `RenderGraphPanel::Build()` refactored to build one `RenderGraphMetadata`
  per frame (or reuse the frozen one, unchanged Pause semantics) and draw
  its ImGui tables from THAT, never from the three raw sources directly.
  Ships the real "Export DOT" implementation (`BuildRenderGraphDot()`, a new
  pure function living in `src/Editor/RenderGraphDotExport.h/.cpp` — NOT
  `src/Renderer/RenderGraph/`, see Locked Design Decision #14) wired to the
  previously-disabled button. Verified by the same "visually unchanged"
  screenshot standard this whole campaign line already applies, PLUS a real
  `.dot` file produced and eyeballed for sane Graphviz syntax.
- **`PHASE4_CROSS_THREAD_BRIDGE_AND_GET_RENDER_GRAPH_ENDPOINT.md`** — extends
  `FrameCaptureBridge` (Locked Design Decision #4), wires the publish call
  into `EditorHost.cpp`'s `Run()` loop (Locked Design Decision #3, reusing
  the already-computed `renderFeatureEntries`, Locked Design Decision #7),
  adds `NetworkRoutes.h`'s `BuildRenderGraphMetadataResponseJson()` +
  `NetworkServer.cpp`'s `GET /render_graph` route registration, with
  `NetworkRoutesTests.cpp` Tier-1 coverage plus a live, HTTP-driven smoke
  test (`run_app_background` + `gte_send_request`) confirming the endpoint's
  JSON shape matches what the Editor panel is simultaneously showing for the
  same frame.
- **`PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`** — updates
  `docs/conventions/networking.md` (one new bullet for `GET /render_graph`)
  and `AGENTS.md` (the existing "Render Graph" panel mentions, if any need
  updating to reflect the new data-driven shape), runs the ONE full clean
  build + full `ctest` pass + live HTTP smoke test this whole campaign is
  allowed to run, confirms every pre-existing consumer of the old
  `RenderGraphPanel`/`FrameCaptureBridge`/`NetworkRoutes.h` behavior still
  works identically, and writes `CAMPAIGN_COMPLETION_REPORT.md`.

### Campaign-wide rules every phase must follow ("Workflow Rule 1" – "Workflow Rule 10" below — later phase files cite these exact numbers)

1. **No full build, no full regression test, for Phases 1–4.** Only a fast,
   targeted, incremental build/compile check (see each phase's own
   "Verification" section) — this machine's full clean build and full
   `ctest` pass are slow and reserved for Phase 5 alone.
2. **Use the engine's own internal logging + `GET /get_logs`** for anything
   inside `GreatTamanaEditor.exe`'s own run — never
   `printf`/`std::cout`/`OutputDebugString`/raw C++ logging for anything new
   added under `src/`. Use `GTE_LOG_INFO`/`GTE_LOG_WARNING`
   (`src/Core/Logging.h`), exactly like every existing call site this
   campaign touches already does.
3. **Use the engine's own network debugging endpoints for visual/behavioral
   verification** — `run_app_background` the real `GreatTamanaEditor.exe`,
   then `gte_send_request` against `http://127.0.0.1:8080`:
   `GET /get_logs?limit=N`, `GET /list_tabs`, `GET /activate_tab?name=Render%20Graph`,
   `GET /get_swapchain`, `GET /get_game_view`, `GET /render_graph` (once
   Phase 4 lands), `POST /clear_logs`. Always `stop_app_background` the PID
   when finished with a check.
4. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own verification passes, documenting exactly what
   changed, any real deviation from this phase's own plan, and the exact
   verification evidence gathered.
5. **`git_add` + `git_commit` at the end of every phase** — one commit per
   phase, message referencing the phase number and its one-line summary.
6. **If a phase hits a genuine design ambiguity not already resolved by
   this master strategy or its own phase file, use `ask_questions` before
   guessing.** This applies transitively to anything a phase itself
   delegates further.
7. **Implementation phases (1–5) must NOT call `delegate_task` themselves.**
   Only the double-check/orchestration step of this campaign (run
   separately, after all phase files are scaffolded and reviewed) is
   allowed to use `delegate_task` to hand off each phase's real
   implementation. The one exception: Phase 5's own mandatory final full
   regression pass may invoke `delegate_task` ONLY if that regression pass
   surfaces a real, newly-broken, unexplained test failure that needs a
   dedicated fix — never for any other reason, and never by any phase other
   than Phase 5.
8. **Every phase's code must follow `AGENTS.md`'s existing coding
   guidelines** (Clean Architecture, RAII, `namespace gte`) and this repo's
   own established render-graph/networking conventions
   (`docs/conventions/networking.md`, the existing
   `RenderGraphSnapshot.h/.cpp`/`FrameCaptureBridge.h/.cpp`/
   `NetworkRoutes.h/.cpp` files) — never invent a new pattern where an
   existing file already shows the exact shape to copy.
9. **Never change any EXISTING observable behavior.** Every current
   `RenderGraphPanel` pixel, every current `FrameCaptureBridge`/
   `NetworkRoutes.h` route's response shape, every current log line, stays
   byte-for-byte identical before and after every phase in this campaign,
   UNLESS that phase's own plan explicitly says otherwise (Phase 3's ImGui
   refactor and Phase 4's new endpoint are the only two phases that add new
   OBSERVABLE surface — both are strictly ADDITIVE, never a removal/rename of
   something that already existed). Diff against the original file (read it
   first, before editing) to confirm this by hand before moving on.
10. **Run `git_status` at the very START of every phase** — confirm the
    branch still reads `feature/editor-core-separation` and the working
    tree is either clean or contains only the exact diff the immediately-
    prior phase already committed — **and run it AGAIN immediately before
    that phase's own final commit**, to confirm the about-to-be-staged diff
    touches ONLY the files this phase's own plan says it may touch.

### What this campaign explicitly does NOT do (Non-Goals)

- No data-driven graph *authoring* system — hand-written `AddPass()` C++
  remains the only way to *declare* passes; this campaign only concerns
  exposing what already ran (see Step 1's closing paragraph, and the Phase 9
  backlog distinction it references).
- No schema for round-tripping metadata back into a live `RenderGraph` — this
  is read-only introspection, never a way to mutate or replay a graph from
  JSON.
- No general-purpose "every Editor panel becomes data-driven + JSON-exposed"
  initiative — scoped exactly to the "Render Graph" panel.
- No security-hardening pass on the embedded HTTP server itself
  (loopback-only binding, no auth remains unchanged, an already-locked
  decision elsewhere in this codebase).
- No new query-parameter filtering on `GET /render_graph` (Locked Design
  Decision #8).
- No native file-save dialog for "Export DOT" (Locked Design Decision #13).
- No change to any OTHER Editor panel, any OTHER network route, or any
  `_v1`/`_v2` plugin render-feature ABI surface — this campaign touches
  `RenderGraphPanel`, `RenderGraphSnapshot`'s new `gte_core` sibling files
  (`RenderGraphSnapshotFormatting.h/.cpp`, `RenderGraphMetadata.h/.cpp`), a
  new `gte_editor`-tier sibling of `RenderGraphPanel` itself
  (`src/Editor/RenderGraphDotExport.h/.cpp` — Locked Design Decision #14),
  `FrameCaptureBridge`, `NetworkRoutes.h/.cpp`, `NetworkServer.cpp`, and
  `EditorHost.cpp`'s `Run()` loop only.

### Reference commands

- Main build tree (existing, already configured): `cmake --build build`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`,
  Ninja/MinGW) — use this for every phase's own incremental build/compile
  check; a Ninja incremental build only recompiles what actually changed.
- Full regression test (Phase 5 ONLY): `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Run the live Editor for HTTP-driven smoke checks:
  `run_app_background` on `build\GreatTamanaEditor.exe`, then
  `gte_send_request` against `http://127.0.0.1:8080` (default port). Always
  `stop_app_background` the PID when finished.

### Every child phase file in this folder

1. `PHASE1_EXTRACT_FORMATTING_AND_LABEL_HELPERS.md`
2. `PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md`
3. `PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md`
4. `PHASE4_CROSS_THREAD_BRIDGE_AND_GET_RENDER_GRAPH_ENDPOINT.md`
5. `PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`

Read this master file FIRST, then the source Investigation document
(`RenderGraph_DataDriven_Editor_And_AI_Debug_Endpoint_Investigation_2026-09-25.md`),
then the one phase file you are working on, then (if it exists yet) the
previous phase's own `PHASEn_COMPLETION_REPORT.md` for continuity clues,
before writing any code.
