# editor-core-separation-8 — PHASE0 MASTER STRATEGY

**Branch:** `feature/editor-core-separation` (stay on it — do not create a new branch).

**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

**This folder:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\task_manager\editor-core-separation-8`

**Source finding/proposal document (read this IN FULL before starting ANY phase):**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\better-render-graph-editor\INVESTIGATION_REPORT.txt`
(referred to below as "the Investigation"). **This master strategy CORRECTS and
NARROWS the Investigation in several real, load-bearing places**, based on
live inspection of the actual current source AND on explicit product
decisions the human product owner made when asked (see "Locked Product
Decisions" below — these came from a real `ask_questions` round, not a guess).
Where this document and the Investigation disagree, **this document wins**.

**IMPORTANT — the Investigation is PARTIALLY STALE.** It was written as if the
"Render Graph" panel were still 100% read-only. It is not: a full prior
campaign, `task_manager/editor-core-separation-7/`, ALREADY SHIPPED a
data-driven `rg::RenderGraphMetadata` model, a real `GET /render_graph` HTTP
endpoint, and a real "Export DOT" exporter. Confirmed by direct
`read_file`/`browse_dir` of the current repository (2026-09-25):
  - `src/Renderer/RenderGraph/RenderGraphMetadata.h/.cpp` — EXISTS.
  - `src/Editor/RenderGraphDotExport.h/.cpp` — EXISTS.
  - `GET /render_graph` (via `FrameCaptureBridge::PublishRenderGraphMetadata()`/
    `GetPublishedRenderGraphMetadata()`) — EXISTS, already shipping real JSON.
  - `src/Editor/Panels/RenderGraphPanel.cpp` already builds ONE
    `rg::RenderGraphMetadata` per frame and draws every table FROM it.
This whole campaign is the natural NEXT layer on top of that: campaign-7 made
the render graph **observable**; this campaign makes it **controllable**
(the 4 requirements from the story: pass on/off, event/priority control,
Blur/GBuffer toggle, HTTP control of all of it).

This document is the ORCHESTRATOR for this whole campaign. Every child phase
file (`PHASE1_*.md` .. `PHASE6_*.md`) in this same folder must be read
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

The story asked for 4 things. Here is the FINAL, locked scope for each, after
weighing the Investigation's own risk analysis against the product owner's
explicit decisions:

1. **Render pass ON/OFF** — from the "Render Graph" Editor panel AND from
   HTTP. Covers BOTH kinds of pass this engine has:
   - **Built-in passes** (`RenderOpaque`, `DrawSkyBackground`,
     `AtmosphereComposite`, `GpuSkinning`, `GpuDrivenBatches`,
     `RenderTransparent`, `Present`, ...) — ON/OFF ONLY. `"Present"` can never
     be turned off (a small, permanent, hardcoded deny-list — it is the pass
     that finally puts pixels on the swapchain; disabling it bricks the
     Editor with no in-process recovery).
   - **Plugin "scriptable render features"** (`IRenderFeatureModule_v2`) —
     ON/OFF, exactly the same shape.
2. **Switch a pass's ordering / adjust its priority** — **SCOPED DOWN from the
   Investigation's own proposal, on purpose, by explicit product decision**:
   - Built-in passes: **NO runtime `RenderPassEvent` reassignment at all.**
     The Investigation itself found a REAL crash risk here (moving
     `"RenderOpaque"` to a different tier while `"AtmosphereComposite"` — which
     reads its output — stays put trips `RenderGraphCompiler`'s own
     `DetectRenderPassEventContradictions()` safety net, which can `assert()`
     in a debug build). The product owner chose the safe option: built-in
     passes only ever get ON/OFF from this campaign, never event
     reassignment.
   - Plugin render features: **priority IS live-adjustable at runtime**
     (`RenderFeatureCompositor::SetFeaturePriority()`, new this campaign) —
     safe, because every `_v2` plugin already renders into its own fully
     private, isolated offscreen target (no shared-resource read/write hazard
     between two plugins the way there is between built-in passes). Stage
     (`PostComposite`/`PreUI`) itself stays fixed — only ordering WITHIN a
     stage changes.
3. **Compute Blur / GBuffer Validation ON/OFF** — these two debug passes are
   ALREADY toggleable today, but only from the "Scene" panel's own two
   checkboxes (`EditorContext::showBlurredSceneOutput`/
   `showGBufferValidationOutput`). This campaign adds: (a) matching
   checkboxes in the "Render Graph" panel too (one-stop control, per product
   decision), and (b) HTTP endpoints for both.
4. **HTTP control of 1, 2, 3** — 6 new endpoints total (final shape below),
   all `GET` + query parameters (no JSON body needed anywhere — see Locked
   Product Decision #3 for why), following this codebase's own
   `FrameDebuggerCommandBridge`/`/frame_debugger/*` pattern byte-for-byte.

**Explicitly OUT OF SCOPE for this campaign** (see Locked Product Decisions
below for the full reasoning of each):
  - No `RenderPassEvent` reassignment for built-in passes, ever, in this
    campaign.
  - No persistence to disk — every toggle/override introduced by this
    campaign is **in-memory only**, reset to defaults on every Editor
    restart.
  - No new query-parameter filtering beyond what each endpoint's own contract
    below states.
  - No change to the plugin ABI (`GtePluginRenderFeatureDescriptor` stays
    byte-for-byte unchanged — the new `enabled`/priority-override state is
    100% HOST-SIDE, matching this file's own existing doc-comment rule: "The
    HOST NEVER trusts a plugin to self-order at runtime").

## Step 2: The Situation (Where are we now?)

Confirmed by direct inspection of the REAL, CURRENT source (every path below
was actually opened and read during this campaign's planning — not guessed):

### 2.1 — Built-in passes have NO enable/disable mechanism today, and NOT ALL of them reach the same choke point

- Every built-in pass is registered as a named provider lambda in
  `src/Core/Core.cpp`'s `Core::RegisterOffscreenRenderPipelineProviders()`
  (9 `m_offscreenRenderPipeline.Register("Name", ...)` calls: `AtmosphereSharedLut`,
  `GpuSkinning`, `AtmosphereViewLut`, `RenderOpaque`, `GpuDrivenBatches`,
  `DrawSkyBackground`, `RenderTransparent`, `AtmosphereComposite`,
  `PluginRenderFeatures`) and `Core::RegisterPresentRenderPipelineProvider()`
  (1 call: `"Present"`).
- Every provider lambda ALWAYS pushes/declares its pass unconditionally today
  — confirmed by reading every one of the 10 bodies in full, not just
  skimming two of them. There is genuinely no existing "is this pass
  enabled" check anywhere.
- **CORRECTED FINDING (this master strategy's own re-verification caught a
  real error the Investigation itself made and never checked) — NOT every
  provider defers its pass through `RenderPassDesc`/`out.push_back(...)`.**
  Two genuinely different declaration styles coexist inside these 10
  provider lambdas, confirmed by reading every lambda body's own real code:
  - **Deferred style** (pushes one or more `rg::RenderPassDesc` into the
    `out` parameter, later flushed by `RenderPipeline::DeclareOnePhase()`):
    `RenderOpaque` (literal debugName `"RenderOpaque"`), `DrawSkyBackground`
    (literal `"DrawSkyBackground"`), `RenderTransparent` (literal
    `"RenderTransparent"`), `GpuDrivenBatches` (dynamic, per-batch names —
    `batch.resetPassName`/`batch.cullingPassName`/`batch.indirectDrawPassName`,
    never the literal string `"GpuDrivenBatches"`), and `GpuSkinning`
    (dynamic, per-dispatch `request.name`, never the literal string
    `"GpuSkinning"`).
  - **Immediate style** (calls `frame.builder.AddRenderPass(...)` — or a
    helper function that does — DIRECTLY, inside the provider lambda itself,
    pushing NOTHING into `out`): `AtmosphereSharedLut` (calls
    `AddAtmosphereSharedLutPasses(frame.builder, ...)`), `AtmosphereViewLut`
    (calls `AddAtmosphereViewLutPasses(frame.builder, ...)`),
    `AtmosphereComposite` (calls `AddAtmosphereCompositePass(frame.builder,
    ...)` — its own lambda's trailing parameter is literally left unnamed,
    `std::vector<rg::RenderPassDesc>&`, proving nothing is ever pushed to
    it), and `"Present"` (calls `AddGpuSkinningPasses(frame.builder, ...)`/
    `AddPresentPass(frame.builder, ...)` directly).
  - **This matters enormously**: a check inserted ONLY inside
    `RenderPipeline::DeclareOnePhase()`'s own flush loop (the Investigation's
    own §2a proposal) can NEVER see, and therefore can NEVER disable,
    `AtmosphereSharedLut`/`AtmosphereViewLut`/`AtmosphereComposite`/`Present`
    — none of them ever reach that loop at all. Since **`AtmosphereComposite`
    is explicitly named in this campaign's own Step 1 goal** as a pass this
    feature must be able to turn on/off, PHASE1 cannot rely on the
    `DeclareOnePhase()` check alone — see PHASE1's own Step 3.4b for the
    small, additional, surgical fix this requires (one added early-return
    guard line each inside the `"AtmosphereComposite"` and `"GpuSkinning"`
    provider lambda bodies specifically — not a rewrite of the choke-point
    strategy, just two confirmed-necessary exceptions to it).
  - `AtmosphereSharedLut`/`AtmosphereViewLut` deliberately do NOT get this
    same treatment — neither is named in this campaign's own Step 1 goal,
    and disabling either would starve `AtmosphereViewLut`/
    `AtmosphereComposite`/`DrawSkyBackground` of data they hard-depend on
    with no compiler-level protection (see the "Known risk" note below).
- `rg::RenderPassDesc::id` (a typed `RenderPassId`, `RenderPipeline.h` line
  ~181) exists but is **NEVER SET** by any of the ~10 Register() call sites —
  confirmed by reading every one. **This campaign does NOT fix that gap and
  does NOT touch `RenderPassId` at all** (Locked Product Decision #5 below) —
  the new registry is keyed by the pass's existing `debugName` string
  instead, which is ALREADY unique and ALREADY present on every
  `RenderPassDesc` (e.g. `desc.debugName = "RenderOpaque";`, set inside the
  SAME lambda that calls `Register("RenderOpaque", ...)` with the identical
  literal). This means **8 of the ~10 existing `Register()` lambda bodies in
  `Core.cpp` need zero changes**; exactly 2 (`"AtmosphereComposite"`,
  `"GpuSkinning"`) each need one added early-return guard line (see PHASE1's
  Step 3.4b) — a small, confirmed, narrowly-scoped exception, not a reversal
  of the overall strategy.
- The one choke point EVERY DEFERRED-STYLE built-in pass funnels through,
  exactly once per frame, is `RenderPipeline::DeclareOnePhase()`
  (`src/Renderer/RenderGraph/RenderPipeline.h`, private method): it collects
  every provider's `RenderPassDesc` list into `m_scratchCollected`,
  `std::stable_sort`s it by `.order`, then loops over it calling
  `builder.AddRenderPass(...)` once per desc. **This is the place a generic
  "skip this desc entirely" check needs to be added** — it already has an
  established injection precedent to mirror:
  `RenderPipeline::SetLegacyViewScopeTranslator(std::function<ViewScope(RenderViewId)>)`,
  set once by `Core`'s constructor-time wiring, consulted inside this exact
  same loop, defaulting to "do the old, unchanged thing" when never set.
  This choke point alone is sufficient for `RenderOpaque`/
  `DrawSkyBackground`/`RenderTransparent`/every individual dynamically-named
  `GpuDrivenBatches`/`GpuSkinning` pass; it is NOT sufficient by itself for
  `AtmosphereComposite` (see above).
- **A real, load-bearing shared-name consequence, confirmed by reading every
  provider's own body**: `RenderOpaque`/`DrawSkyBackground`/
  `RenderTransparent`/`AtmosphereComposite` are each declared with the exact
  SAME literal `debugName` for BOTH the Game view and the Scene view (their
  provider is `ProviderScope::PerActiveView`, looping `frame.activeViews`,
  and the literal string assigned to `desc.debugName` never varies by
  view). Since the new registry is keyed purely by that string, with no view
  dimension, **disabling `"RenderOpaque"` disables it for BOTH Game View AND
  Scene View simultaneously — there is no way to disable it in one view
  only**. This is an intentional, accepted consequence of keeping the
  registry dead simple (Locked Product Decisions #5/#6), not a bug — but it
  is a real, visible fact the "Render Graph" panel's own UI must not hide
  (PHASE4 adds a short tooltip note for this), and every phase's own
  completion report must state it plainly rather than let a future reader
  discover it by surprise.
- **Known risk, honestly stated rather than silently smoothed over**: this
  campaign's own ON/OFF mechanism has NO dependency-awareness at all (unlike
  a `RenderPassEvent` reassignment, which the Investigation already flagged
  as unsafe and Locked Product Decision #1 removed entirely). Disabling a
  pass whose output another STILL-ENABLED pass reads (e.g. disabling
  `"RenderOpaque"` while `"AtmosphereComposite"`/`"DrawSkyBackground"` stay
  on) is not compiler-checked or prevented anywhere — the reading pass keeps
  running and will see whatever stale/previous-frame content already sits in
  that imported render target (both Game/Scene view color targets are
  persistent, host-owned `RenderTexture` objects imported into the graph
  every frame, never freshly (re)allocated, so this degrades to a visibly
  stale/wrong-looking frame, not a crash or validation error — confirmed by
  reading how `viewData->colorTarget` is sourced). PHASE4's and PHASE5's own
  live verification steps must include at least one deliberate "disable a
  pass with a known downstream reader, take a screenshot, confirm it looks
  wrong but the Editor keeps running with no crash/hang" check, and must
  record the actual observed result honestly in their own completion
  reports rather than only testing an isolated, no-downstream-reader pass
  like `"RenderTransparent"`.

### 2.2 — Plugin render features have NO enable/disable/live-priority mechanism today

- `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`'s
  `GtePluginRenderFeatureDescriptor` (`name`, `stage`, `priority`,
  `blendMode`) has NO `enabled` field — and per its own doc comment ("The
  HOST NEVER trusts a plugin to self-order at runtime") and
  `plugins/gte_plugin_abi/PublicSurface.md`'s ABI-stability rules, **this
  campaign does not touch this struct at all**.
- `src/Core/Plugins/RenderFeatureCompositor.h/.cpp` collects every loaded
  `IRenderFeatureModule_v2`'s descriptor exactly ONCE, at
  `OnPluginsLoaded()` time, groups into `m_postComposite`/`m_preUi`
  (`std::vector<Entry>`, `Entry` = `{ IRenderFeatureModule_v2* module;
  GtePluginRenderFeatureDescriptor descriptor; }`), sorts each vector by
  `descriptor.priority` ascending (stable, with a documented lexical
  tie-break for a same-priority collision), and **never re-sorts again** —
  confirmed by reading `OnPluginsLoaded()`'s full body.
- `ContributeRenderGraphPasses()` builds `combinedList` fresh EVERY FRAME by
  concatenating `m_postComposite` then `m_preUi` (in their already-sorted
  order) and runs the real seed/blend GPU chain over it — this is genuinely
  rebuilt from scratch every single frame, so a live re-sort of the
  underlying vectors is 100% safe (confirmed: `RenderFeatureNamePool`'s
  per-(plugin,view) interned names are keyed by PLUGIN NAME + VIEW NAME, never
  by position/index in the list — re-ordering the vector never invalidates
  any interned name).
- `DebugSnapshot()` walks the SAME combined order and produces
  `std::vector<RenderFeatureDebugEntry>` (`name`, `stage`, `priority`,
  `blendMode` — all plain, already-resolved strings/ints) — this is what
  backs the "Render Graph" panel's "Plugin Render Features" section AND is
  ALREADY embedded inside `rg::RenderGraphMetadata::renderFeatures`
  (campaign-7), which means it is ALREADY served by `GET /render_graph`
  today. Adding an `enabled` bool to `RenderFeatureDebugEntry` this campaign
  means **`GET /render_graph` automatically starts reporting plugin-feature
  enabled state with ZERO new endpoint needed** — a disabled plugin feature
  is NEVER removed from these vectors (only skipped inside
  `ContributeRenderGraphPasses()`), so it never disappears from
  `DebugSnapshot()`/`GET /render_graph` the way a disabled BUILT-IN pass
  disappears from `RenderGraphSnapshot` entirely (see 2.4 below — this is
  the one real asymmetry between the two subsystems, and it is why only
  built-in passes need a brand-new discovery endpoint, not plugin features).
- `Core::GetRenderFeatureCompositor() const noexcept` currently returns
  `const RenderFeatureCompositor*` — this campaign changes the DECLARED
  return type to a non-`const` `RenderFeatureCompositor*` (the method itself
  stays `const` — it does not mutate `Core`; only what it returns a pointer
  TO becomes mutable). This is a **safe, 1-line, backward-compatible header
  change**: `std::unique_ptr<RenderFeatureCompositor>::get()` already returns
  a non-`const` pointer internally regardless of the accessor method's own
  `const`-ness; every existing call site assigns the result into a
  `const RenderFeatureCompositor*` local (confirmed, `EditorHost.cpp` line
  ~587) — assigning a non-`const` pointer into a `const`-pointer-typed local
  is always a legal implicit conversion, so **zero existing call sites
  break**.

### 2.3 — Blur/GBuffer toggles already exist, just not wired to the Render Graph panel or HTTP

- `src/Editor/EditorContext.h`: `bool showBlurredSceneOutput = false;` (line
  ~181), `bool showGBufferValidationOutput = false;` (line ~204).
- `src/Editor/Panels/ScenePanel.cpp`: two `ImGui::Checkbox(...)` calls flip
  them directly (lines ~52, ~68).
- `src/Editor/ImGuiEditorLayer.cpp`: `AddBlurValidationPass()`/
  `AddGBufferValidationPass()` (lines ~458-489) both start with
  `if (!m_ctx.showXxx || !m_ctx.sceneViewVisible) return std::nullopt;` — when
  off, the pass is **never declared into the graph at all** (not "declared
  then culled" — genuinely never submitted), which is exactly the ON/OFF
  behavior requirement #3 wants; it's just only reachable today from one
  checkbox in one specific panel, and not from HTTP at all.
- `src/Editor/Panels/RenderGraphPanel.cpp`'s `Build()` signature ALREADY
  takes `EditorContext& ctx` as its first parameter, but the body currently
  reads `EditorContext& /*ctx*/` (commented out, genuinely unused) — this
  campaign finally uses it.

### 2.4 — The real asymmetry a "known but currently OFF" list must account for

A DISABLED built-in pass is **never declared**, so it leaves **zero trace**
in `rg::RenderGraphSnapshot`/`rg::RenderGraphMetadata` — the panel's existing
pass tables (built from `metadata.offscreenRegime.passes`/
`presentRegime.passes`) literally cannot show a currently-off built-in pass at
all, because nothing about it exists in that frame's data. This is WHY the
new toggle registry itself (not the snapshot) must be the single source of
truth for "which built-in pass names are known, and are they currently on or
off" — and why a genuinely NEW discovery endpoint (`GET /render_graph/passes`)
is needed for built-in passes specifically (Locked Product Decision #8).
Plugin render features do NOT have this problem (see 2.2 above — they always
stay in `DebugSnapshot()`'s output, disabled or not), so **no equivalent
discovery endpoint is needed for plugin features** — `GET /render_graph`'s
existing `render_features` array already carries the new `enabled` field once
PHASE2 lands.

### 2.5 — The established bridge/HTTP pattern this campaign must mirror exactly

`src/Application/FrameDebuggerCommandBridge.h/.cpp` +
`src/Network/NetworkServer.cpp`'s `/frame_debugger/*` routes are the closest,
freshest, most complete precedent in this codebase for "one bridge class,
several command kinds, several `GET .../*?param=value` routes, one shared
response-mapping helper" — read `FrameDebuggerCommandBridge.h` in full before
starting PHASE5. Its exact shape (mutex + `std::condition_variable` + one
pending-request slot; `SubmitAndWait()` called from the network thread;
`TryPeekPendingCommandRequest()`/`FulfillCommand()` called from the main
thread's own frame loop, pumped once per frame inside
`src/Editor/EditorHost.cpp`'s `Run()`) is EXACTLY what this campaign's new
`RenderGraphControlCommandBridge` mirrors.

### 2.6 — Core/Editor separation is a REAL, load-bearing constraint here (not a style preference)

Confirmed by reading `src/Editor/ImGuiEditorLayer.cpp`'s own top-of-file
include list and header comment: **`ImGuiEditorLayer` never includes
`Core.h` and holds no `Core&`/`Core*` member at all.** `IEditorLayer::BuildUI()`
only ever receives already-resolved pieces (`Game&`, `Renderer&`,
`const rg::RenderGraph&`, `AtmosphereSettings&`, `AtmosphereLutRenderer&`,
plain data vectors) as PARAMETERS, supplied every frame by
`src/Editor/EditorHost.cpp` (the one composition root that owns BOTH `Core`
and the concrete `IEditorLayer`). This means:
  - A Core-owned mutation triggered by HTTP (built-in pass toggle, plugin
    feature enable/priority) is applied DIRECTLY by `EditorHost.cpp`'s own
    bridge-pump code calling `m_core.GetRenderPassToggleRegistryMutable()...`/
    `m_core.GetRenderFeatureCompositor()->...` — **never** routed through
    `IEditorLayer` (there is no existing precedent for HTTP mutating
    Core-owned state via `IEditorLayer`, and inventing one here would be a
    real, unjustified architecture violation).
  - A Core-owned mutation triggered by the Editor UI (a checkbox in the
    "Render Graph" panel) needs the SAME Core-owned objects handed to it as
    plain, mutable, non-owning parameters threaded through `BuildUI()` — the
    exact same way `AtmosphereSettings& atmosphereSettings` is already handed
    to `BuildUI()` today and mutated live, in place, by `AtmospherePanel`,
    with zero bridge/thread-safety machinery needed (both the render
    declare-loop and every Panel's `Build()` call happen on the SAME thread —
    the main thread — so there is no data race to guard against here, unlike
    the genuinely cross-thread HTTP path).
  - A Blur/GBuffer toggle triggered by HTTP IS Editor-owned state
    (`EditorContext`), so it correctly DOES go through 2 new `IEditorLayer`
    virtual methods (`SetShowBlurredSceneOutput`/`SetShowGBufferValidationOutput`),
    mirroring `FrameDebuggerSetEnabled()`'s exact existing shape.

## Step 3: The Plan (How do we get there?)

### Locked Product Decisions (resolved via a real `ask_questions` round with the human — do not re-litigate; challenge via `ask_questions` only if real code contradicts one)

1. **Built-in passes get ON/OFF ONLY, never `RenderPassEvent` reassignment.**
   Plugin render features get ON/OFF AND live priority reassignment (never
   stage reassignment). This is the single biggest scope correction versus
   the Investigation's own §3/§5 proposal — chosen specifically to eliminate
   the real crash risk the Investigation itself flagged (moving a built-in
   pass's tier can silently break a real data dependency and trip
   `RenderGraphCompiler`'s own contradiction-detector assert).
2. **Plugin feature priority is LIVE-adjustable at runtime** (re-sorts the
   affected stage's vector immediately, reusing the exact same
   sort+collision-tie-break logic `OnPluginsLoaded()` already has) — confirmed
   safe because the compositing chain is rebuilt fresh from these vectors
   every single frame and interned target names are keyed by plugin name, not
   position.
3. **Every new HTTP endpoint is `GET` + query parameters — no JSON POST body
   anywhere in this campaign.** Originally this campaign considered a mixed
   GET/POST shape (simple bools via GET, anything needing a string/enum via
   POST+JSON) — but because Locked Decision #1 above removes ALL
   string/enum-valued mutations (no `RenderPassEvent` string to parse
   anywhere), every remaining field is a plain bool or a plain int, both of
   which fit a query parameter perfectly. This lets the WHOLE campaign mirror
   `/frame_debugger/*`'s exact existing `GET .../*?param=value` shape with no
   exception.
4. **All 6 new mutation/discovery endpoints share ONE new bridge class**,
   `RenderGraphControlCommandBridge` (`src/Application/`), mirroring
   `FrameDebuggerCommandBridge`'s own "7 kinds under 1 bridge" precedent —
   these are all conceptually "control this session's render-graph runtime
   composition", one cohesive kind of request with several enum values, not
   6 unrelated concerns.
5. **The new built-in-pass-toggle registry is keyed by the pass's existing
   `debugName` STRING — `RenderPassDesc::id`/`RenderPassId` is left
   completely untouched by this campaign.** This means **8 of the ~10
   existing `Register()` lambda bodies in `Core.cpp` need zero changes** — a
   deliberate, evidence-based simplification versus the Investigation's own
   §2a proposal (which suggested stamping `desc.id = "X"_passId` at every
   call site). Lower risk, smaller diff, identical practical effect. The
   remaining 2 (`"AtmosphereComposite"`, `"GpuSkinning"`) each need one added
   early-return guard line, confirmed necessary by this document's own Step
   2.1 (they call `frame.builder.AddRenderPass(...)` immediately and never
   reach `DeclareOnePhase()`'s own flush loop at all) — a small, narrowly-
   scoped, evidence-based exception to the original plan, not a reversal of
   it.
6. **The registry auto-discovers a built-in pass's name the first time it is
   actually declared this session** (default `enabled = true` — i.e. zero
   observable behavior change for any pass nobody has touched yet) — there is
   NO hardcoded list of built-in pass names anywhere in this campaign's new
   code. A pass that has not run even once yet this session (e.g. a
   conditional provider that returned early) will not appear as a toggle
   option in the panel/discovery endpoint until it runs at least once — an
   accepted, self-resolving, extremely narrow edge case, and the ONLY design
   that keeps Core's own long-standing "Core has no enumerated knowledge of
   what a pass is" philosophy (`RenderPassDesc`'s own header comment) intact.
7. **Everything this campaign adds is IN-MEMORY ONLY — nothing persists to
   disk.** A fresh Editor launch always starts with every built-in pass
   enabled, every plugin feature enabled at its own author-declared priority,
   and both debug-pass checkboxes off (`EditorContext`'s own existing
   defaults, unchanged). Matches the "AI-in-the-loop debugging session"
   use case this whole campaign exists for far better than a persisted
   config file would, and is dramatically lower risk/effort for v1.
8. **`GET /render_graph` (existing, campaign-7) gains an `enabled` field on
   every `render_features[]` entry (PHASE2) — no new endpoint needed for
   plugin features (see Step 2.4 above for why).** Built-in passes DO get a
   brand-new, dedicated discovery endpoint, `GET /render_graph/passes`
   (PHASE5), since a disabled built-in pass is otherwise completely invisible
   to any existing endpoint.
9. **A small, permanent, hardcoded deny-list refuses to ever disable
   `"Present"`.** `RenderPassToggleRegistry::SetEnabled("Present", false)`
   (and therefore both the HTTP route and the panel's own checkbox) always
   fails (`false`/`409`) for this one name. Extensible (a
   `static bool IsDenyListed(name)` function, not a single hardcoded `if`) —
   but only `"Present"` is deny-listed for v1; nothing else in this codebase
   is similarly load-bearing enough to warrant it.
10. **The "Render Graph" panel ALSO gets its own Blur/GBuffer checkboxes**
    (bound to the exact SAME `EditorContext` bools `ScenePanel.cpp` already
    uses) — two UI places to toggle the same thing, by explicit product
    choice (one-stop control from the panel this whole campaign is about).
    `ScenePanel.cpp`'s own existing checkboxes are UNTOUCHED.

### Locked Architecture Decisions (resolved by direct code reading, not product choice — see Step 2 for the evidence)

11. **`RenderPassToggleRegistry` is a NEW, plain, `gte_core`-tier class living
    at `src/Renderer/RenderGraph/RenderPassToggleRegistry.h/.cpp`** (a sibling
    of `RenderPipeline.h`, `RenderGraphMetadata.h`, etc.) — NOT under
    `src/Core/Plugins/` as the Investigation's §2a/§6 proposed (that location
    made sense under the Investigation's OWN now-superseded `RenderPassId`-
    stamping plan; a plain `debugName`-keyed registry belongs directly beside
    `RenderPipeline.h`, the ONE class that consults it every frame, injected
    the exact same way `SetLegacyViewScopeTranslator()` already is). `Core`
    owns exactly ONE instance (`m_renderPassToggleRegistry`), shared by BOTH
    `m_offscreenRenderPipeline` and `m_presentRenderPipeline`.
12. **No mutex/thread-safety of any kind inside `RenderPassToggleRegistry`
    or `RenderFeatureCompositor`'s new methods** — both are touched EXCLUSIVELY
    from the main thread (the render declare-loop, the Editor UI's own
    `Build()` calls, AND the new bridge's pump — see Step 2.6) — the
    cross-thread-safety concern is fully handled ONE level up, by
    `RenderGraphControlCommandBridge`'s own mutex/condvar (PHASE5), exactly
    like every other bridge in this codebase already keeps its OWN payload
    types (`FrameDebuggerStateOutcome`, etc.) plain and un-synchronized.
13. **`IEditorLayer::BuildUI()` gains exactly 2 new trailing parameters**:
    `rg::RenderPassToggleRegistry& renderPassToggleRegistry` (never null — a
    plain owned `Core` member, not a pointer) and
    `RenderFeatureCompositor* renderFeatureCompositor` (nullable — mirrors
    `Core::GetRenderFeatureCompositor()`'s own existing nullability exactly).
    `EditorLayer.h` forward-declares both types (`namespace gte::rg { class
    RenderPassToggleRegistry; }` / `namespace gte { class
    RenderFeatureCompositor; }`) rather than including their real, heavier
    headers — mirroring this file's own existing forward-declare-only
    convention for `rg::RenderGraph`. `NullEditorLayer`'s override
    ignores both new parameters (a safe no-op, matching every other method in
    that file).
14. **2 new plain `IEditorLayer` virtual setters for the Blur/GBuffer
    booleans** (`SetShowBlurredSceneOutput(bool)`/
    `SetShowGBufferValidationOutput(bool)`), mirroring
    `FrameDebuggerSetEnabled(bool)`'s exact existing shape — these mutate
    `EditorContext`-owned state, so (unlike the two paragraphs above) they
    correctly DO go through `IEditorLayer`, never a direct `Core` call.
15. **`RenderGraphPanel::Build()` gains the exact same 2 new trailing
    parameters as `BuildUI()`**, forwarded verbatim by
    `ImGuiEditorLayer::BuildUI()` — this is the FIRST time a Panel in this
    codebase receives a mutable, non-owning pointer/reference to Core-tier
    state directly (every other Panel either owns its own state or mutates
    only `EditorContext`) — a deliberate, narrowly-scoped, well-justified
    exception, NOT a new general pattern other Panels should start copying
    without their own equally strong justification.
16. **`RenderGraphControlCommandBridge` (PHASE5) is a NEW bridge, never a
    new enum value bolted onto `FrameDebuggerCommandBridge` or
    `EditorUiCommandBridge`** — per this codebase's own explicit,
    repeatedly-stated rule ("a genuinely new KIND of request gets its own
    bridge, never a new enum value bolted onto an existing bridge built for
    an unrelated purpose").

### Phase map

- **`PHASE1_BUILTIN_PASS_TOGGLE_REGISTRY_AND_CHOKEPOINT.md`** — the new
  `RenderPassToggleRegistry.h/.cpp`, `RenderPipeline.h`'s new consult-point
  inside `DeclareOnePhase()`, `Core.h/.cpp`'s wiring + 2 new public accessor
  methods, PLUS one added early-return guard line each inside the
  `"AtmosphereComposite"` and `"GpuSkinning"` provider lambda bodies (Step
  2.1's corrected finding — these two bypass `DeclareOnePhase()`'s own flush
  loop entirely and need their own direct consult). Zero observable
  production behavior change (nothing calls the new mutator yet outside
  tests) — pure, additive plumbing, Tier-1 tested.
- **`PHASE2_PLUGIN_RENDER_FEATURE_ENABLE_DISABLE_AND_PRIORITY.md`** —
  `RenderFeatureCompositor`'s new `SetFeatureEnabled()`/`SetFeaturePriority()`,
  `Entry::enabledOverride`, `ContributeRenderGraphPasses()`'s new filter,
  `DebugSnapshot()`'s new `enabled` field, `RenderFeatureDebugEntry.h`'s new
  field, `RenderGraphMetadata.cpp`'s `to_json()` update (so `GET
  /render_graph` starts reporting it automatically), `Core.h`'s
  `GetRenderFeatureCompositor()` return-type change. Zero observable
  production behavior change until PHASE4/5 wire up a real caller.
- **`PHASE3_IEDITORLAYER_SURFACE_AND_BUILDUI_WIRING.md`** — `EditorLayer.h`'s
  2 new `BuildUI()` parameters + 2 new Blur/GBuffer setters,
  `ImGuiEditorLayer.cpp`/`NullEditorLayer.cpp` implementations,
  `EditorHost.cpp`'s updated `BuildUI()` call site. Pure plumbing — the panel
  does not use the new parameters yet this phase.
- **`PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md`** — the FIRST phase with real,
  observable UI behavior change: `RenderGraphPanel`'s new "Enabled" checkbox
  column, its new "known but currently disabled" built-in-pass section, its
  new per-plugin-feature enable checkbox + priority `DragInt`, and its new
  Blur/GBuffer checkboxes (finally using the panel's own previously-unused
  `EditorContext&` parameter). Verified with a real, live screenshot-based
  before/after check.
- **`PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md`** — the new
  `RenderGraphControlCommandBridge.h/.cpp`, `EditorHost.cpp`'s new pump
  block, `NetworkRoutes.h/.cpp`'s new parse/build functions, and
  `NetworkServer.cpp`'s 6 new route registrations. Verified end-to-end with a
  live `run_app_background` + `gte_send_request` session.
- **`PHASE6_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`** — updates
  `docs/conventions/networking.md` (6 new bullets), re-checks `AGENTS.md`'s
  existing "Render Graph"/"Plugin"-related mentions, runs the ONE full clean
  build + full `ctest` pass + final live HTTP smoke test this whole campaign
  is allowed to run, and writes `CAMPAIGN_COMPLETION_REPORT.md`.

### The final, locked HTTP endpoint contract (PHASE5 ships all 6)

All `GET`, all query-parameters, all through the ONE new
`RenderGraphControlCommandBridge`:

| Endpoint | Query params | Success (200) body | Failure |
|---|---|---|---|
| `GET /render_graph/set_pass_enabled` | `name` (string), `enabled` (`true`/`false`) | `{"success":true,...}` + resulting state | `400` bad params, `409` deny-listed name (e.g. `"Present"`), `503` bridge unavailable, `504` timeout |
| `GET /render_graph/passes` | none | `{"passes":[{"name":...,"enabled":...,"ever_declared_this_session":...}, ...]}` | `503`/`504` |
| `GET /render_graph/set_feature_enabled` | `name`, `enabled` | `{"success":true,...}` | `400`, `409` unknown feature name, `503`, `504` |
| `GET /render_graph/set_feature_priority` | `name`, `priority` (int) | `{"success":true,...}` | `400`, `409` unknown feature name, `503`, `504` |
| `GET /render_graph/set_blur_enabled` | `enabled` | `{"success":true,"enabled":...}` | `400`, `503`, `504` |
| `GET /render_graph/set_gbuffer_enabled` | `enabled` | `{"success":true,"enabled":...}` | `400`, `503`, `504` |

(Exact JSON field names are finalized in PHASE5 — this table is the
CONTRACT, not the literal wire format; PHASE5 mirrors
`BuildFrameDebuggerCommandResponseJson()`'s own established shape exactly.)

### Campaign-wide rules every phase must follow ("Workflow Rule 1" – "Workflow Rule 10" below — later phase files cite these exact numbers)

1. **No full build, no full regression test, for Phases 1–5.** Only a fast,
   targeted, incremental build/compile check (see each phase's own
   "Verification" section) — this machine's full clean build and full
   `ctest` pass are slow and reserved for Phase 6 alone.
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
   `GET /get_swapchain`, `GET /get_game_view`, `GET /render_graph`,
   `GET /render_graph/passes` (once PHASE5 lands), `POST /clear_logs`. Always
   `stop_app_background` the PID when finished with a check.
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
7. **Implementation phases (1–6) must NOT call `delegate_task` themselves.**
   Only the double-check/orchestration step of this campaign (run
   separately, after all phase files are scaffolded and reviewed) is
   allowed to use `delegate_task` to hand off each phase's real
   implementation. The one exception: Phase 6's own mandatory final full
   regression pass may invoke `delegate_task` ONLY if that regression pass
   surfaces a real, newly-broken, unexplained test failure that needs a
   dedicated fix — never for any other reason, and never by any phase other
   than Phase 6.
8. **Every phase's code must follow `AGENTS.md`'s existing coding
   guidelines** (Clean Architecture, RAII, `namespace gte`) and this repo's
   own established render-graph/networking conventions — never invent a new
   pattern where an existing file already shows the exact shape to copy
   (`FrameDebuggerCommandBridge.h/.cpp` and `/frame_debugger/*` in
   particular — read them in full before PHASE5).
9. **Never change any EXISTING observable behavior**, except where a phase's
   own plan explicitly says otherwise (PHASE4's panel UI change and PHASE5's
   new endpoints are the only two phases that add new OBSERVABLE surface —
   both strictly ADDITIVE). Diff against the original file (read it first,
   before editing) to confirm this by hand before moving on.
10. **Run `git_status` at the very START of every phase** — confirm the
    branch still reads `feature/editor-core-separation` and the working
    tree is either clean or contains only the exact diff the immediately-
    prior phase already committed — **and run it AGAIN immediately before
    that phase's own final commit**, to confirm the about-to-be-staged diff
    touches ONLY the files this phase's own plan says it may touch.

### What this campaign explicitly does NOT do (Non-Goals)

- No `RenderPassEvent` reassignment for built-in passes (Locked Product
  Decision #1).
- No plugin ABI change (`GtePluginRenderFeatureDescriptor` untouched).
- No persistence to disk of any new toggle/override state (Locked Product
  Decision #7).
- No new discovery endpoint for plugin features (Locked Product Decision #8
  — already covered by `GET /render_graph`'s existing `render_features[]`).
- No `RenderPassId` stamping anywhere (Locked Product Decision #5).
- No JSON POST body anywhere in this campaign (Locked Product Decision #3).
- No change to `ScenePanel.cpp`'s own existing Blur/GBuffer checkboxes (they
  stay exactly as-is — this campaign only ADDS a second place to toggle the
  same state).
- No security-hardening pass on the embedded HTTP server itself
  (loopback-only binding, no auth, unchanged — an already-locked decision
  elsewhere in this codebase).
- No change to any OTHER Editor panel, any OTHER network route, or any
  OTHER `_v1`/`_v2` plugin capability surface.

### Reference commands

- Main build tree (existing, already configured): `cmake --build build`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`,
  Ninja/MinGW) — use this for every phase's own incremental build/compile
  check; a Ninja incremental build only recompiles what actually changed.
- Full regression test (Phase 6 ONLY): `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Run the live Editor for HTTP-driven smoke checks:
  `run_app_background` on `build\GreatTamanaEditor.exe`, then
  `gte_send_request` against `http://127.0.0.1:8080` (default port). Always
  `stop_app_background` the PID when finished.

### Every child phase file in this folder

1. `PHASE1_BUILTIN_PASS_TOGGLE_REGISTRY_AND_CHOKEPOINT.md`
2. `PHASE2_PLUGIN_RENDER_FEATURE_ENABLE_DISABLE_AND_PRIORITY.md`
3. `PHASE3_IEDITORLAYER_SURFACE_AND_BUILDUI_WIRING.md`
4. `PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md`
5. `PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md`
6. `PHASE6_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`

Read this master file FIRST, then the source Investigation document (for
background context only — remember it is PARTIALLY STALE, see the warning at
the top of this file), then the one phase file you are working on, then (if
it exists yet) the previous phase's own `PHASEn_COMPLETION_REPORT.md` for
continuity clues, before writing any code.
