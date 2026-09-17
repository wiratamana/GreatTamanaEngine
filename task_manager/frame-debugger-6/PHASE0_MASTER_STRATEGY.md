# PHASE0 — MASTER STRATEGY — `frame-debugger-6` campaign

_Orchestrator document. Every child phase document (`PHASE1_*.md` ...
`PHASE5_*.md`) MUST be read alongside this one before that phase starts work.
This document is the one source of truth for WHY the campaign exists, WHAT is
locked/decided already, and WHICH order phases run in. Do not re-litigate a
"Locked Design Decision" below without first asking the user via
`ask_questions` — these were already confirmed with the user during this
campaign's own planning session._

## 0. How this campaign was scoped (do not skip this)

This campaign started from one vague user report: *"load scene existing scene
from project, the terrain seems not get registered on frame debugger"*. Before
writing a single line of strategy, a live, HTTP-driven visual-debugging pass
was performed against the real, running `GreatTamanaEngine.exe`:

1. Launched `build/GreatTamanaEngine.exe`, loaded `Project/TestScene.gtscene`
   (`POST /load_scene`) — a scene containing a `terrain` entity (imported from
   `terrain.gta`/`terrain.stl`, ~1,045,458 real triangles), a `Directional
   Light`, a `Camera`, and a `SmokeTestCube`.
2. Confirmed via `GET /get_swapchain` that terrain renders correctly in both
   the Scene and Game views.
3. Opened the Frame Debugger (`GET /frame_debugger/open` +
   `/frame_debugger/enable?value=true`), captured a frame, and confirmed (via
   the "Render Graph" panel, side-by-side) that the single `"GameView"` leaf's
   own aggregate Draw Stats correctly report **1,045,470 triangles** (terrain's
   ~1,045,458 + the small cube's 12) — so terrain's geometry genuinely IS being
   counted. This ruled out "terrain's triangles are silently dropped".
4. Found a DIFFERENT, concretely confirmed, reproducible bug while comparing
   the Frame Debugger's event tree against the "Render Graph" panel's raw pass
   list side by side: the Frame Debugger's `"Compute Dispatches
   (Post-GameView)"` group showed **duplicate, identically-named, visually
   indistinguishable leaves** (two `"AtmosphereSkyViewLutPass"` entries, two
   `"AtmosphereAerialPerspectiveCompositePass"` entries) — because the engine
   genuinely runs a SEPARATE copy of several Atmosphere compute passes for the
   Editor's own Scene-View camera (writing to `"..._SceneView"`-suffixed
   textures) in addition to the Game-View ones (`"..._GameView"`-suffixed
   textures), but BOTH copies are registered under the exact same hardcoded
   pass name string (e.g. `"AtmosphereSkyViewLutPass"`), and the Frame
   Debugger's generic compute-pass discovery
   (`FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()`) has no concept
   at all of "which view does this pass instance actually belong to" — it
   just includes *any* surviving compute pass positioned before/after
   `"GameView"`'s own index in the WHOLE render graph, Scene-View-only passes
   included, even though this feature's own documented, permanent rule is
   **"Scope is Game View ONLY, permanently."** (`docs/conventions/
   frame-debugger.md`). The SAME root cause also mis-includes a Scene-View-only
   debug tool (`"ComputeBlurValidation"`, confirmed by reading
   `Application.cpp` — it reads the Scene View's OWN texture handle, never the
   Game View's) whenever the "Show Compute Blur (debug)" toggle is on, and
   mis-includes `"AtmosphereAerialPerspectiveVolumeDebugSlicePass"` if a future
   engineer ever adds a Scene-View equivalent of it.
5. Asked the user (`ask_questions`) to confirm this WAS the bug they meant.
   The user confirmed the duplicate-pass bug needs fixing, AND clarified their
   real, underlying ask in their own words: **"frame debugger dont have exact
   step where terrain got drawn"** — i.e. they want to be able to tell, inside
   the Frame Debugger, EXACTLY which step/leaf is "where terrain got drawn",
   not just a merged, anonymous `"GameView"` pass. The user explicitly
   confirmed they want this treated as a genuine NEW FEATURE in this same
   campaign (breaking the old "one leaf per PASS, never one leaf per mesh"
   locked decision from `frame-debugger-2`/`frame-debugger-5` on purpose), not
   just a bug fix.
6. The user also confirmed: fix the duplicate/mis-scoped pass bug using a
   ROBUST, generic mechanism (never a hardcoded pass-name/suffix string
   filter), a medium phase count (4-5, matching `frame-debugger-5`'s own
   shape), and a LIGHT final-phase verification (incremental build + a couple
   of targeted screenshots, no full `ctest` run).

**This campaign therefore has two real, independent-but-sequenced workstreams
that both trace back to the same user report:**

- **Workstream A (bug fix):** stop Scene-View-only (and any other
  wrong-view-scope) compute passes from ever appearing inside the
  Game-View-scoped Frame Debugger tree, and stop two different real passes
  from ever displaying under one indistinguishable shared name.
- **Workstream B (new feature):** give the user a real, per-entity,
  individually selectable tree leaf under `"GameView"` — e.g. literally
  something the user can click that is labeled with the `terrain` entity's own
  name — answering "which step drew terrain" directly and permanently for
  every future entity too, not just this one report.

## 1. The Goal (Where are we going?)

By the end of this campaign:

1. Loading `TestScene.gtscene`, enabling the Frame Debugger, and capturing a
   frame produces an event tree where **every leaf that is NOT genuinely part
   of the Game View's own real render/compute chain is absent** — no
   Scene-View-only Atmosphere LUT/Volume/Composite pass instance, no
   Scene-View-only `ComputeBlurValidation`, ever appears under
   `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"`
   again. Two passes that happen to share a literal name because one is the
   Game-View copy and one is the Scene-View copy of the same logical
   computation are NEVER both shown as if they were the same, single, ambiguous
   thing.
2. This filtering is driven by a genuine, structural, engine-wide concept —
   "which view (if any) does this pass conceptually belong to" — stamped once
   at the same kind of "choke point" `PassRecord::isComputePass` already is
   (per `frame-debugger-5`'s own precedent), NOT a hardcoded pass-name/suffix
   string comparison living inside the Editor. A brand-new future compute pass
   that is genuinely Game-View-scoped needs zero Frame-Debugger-specific code
   to show up correctly; one that is genuinely Scene-View-scoped needs zero
   Frame-Debugger-specific code to stay excluded.
3. Expanding the `"GameView"` node in the tree reveals one real, selectable
   child leaf PER real draw call recorded that frame — e.g.
   `"terrain (Entity 2)"`, `"SmokeTestCube (Entity 3)"` (PHASE4's actual naming
   convention — the entity's own resolved display name plus its `(Entity N)`
   suffix, with NO extra "Draw Mesh:" prefix; see PHASE4's own Step 3.1) —
   each with its own correct shader/material-texture/triangle-count detail
   panel. Clicking the `terrain` leaf is now, permanently, "the exact step
   where terrain got drawn" the user asked for. This is an explicit,
   user-approved SECOND breaking change to the historically "locked"
   one-leaf-per-pass rule (the same category of deliberate, precedented
   breaking change `frame-debugger-5` itself already made once).
4. None of this regresses any already-shipped Frame Debugger behavior
   (history ring buffer, Channels/Levels, HTTP automation, the
   `"GameView"`/`compositedPreview` preview-selection rule, GPU Skinning's
   existing correct Pre-GameView-only appearance) — every existing Tier-1 test
   file for this feature still passes, with new cases added, never loosened.

## 2. The Situation (Where are we now?)

- The engine has a real, working Frame Debugger (`frame-debugger-2/3/4/5`
  campaigns) — see `docs/conventions/frame-debugger.md` and `AGENTS.md`'s
  "Frame Debugger" section for the full, current, ground-truth description of
  what exists today. **Read that file in full before touching any code.**
- `FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()` currently decides
  which compute passes go in the Pre-/Post-GameView groups using ONLY: (a)
  `pass.isComputePass == true`, (b) `pass.isCulled == false`, and (c) the
  pass's own POSITION in `graphSnapshot.passesInExecutionOrder` relative to
  `"GameView"`'s own index. There is **no concept of "view ownership"
  anywhere in the render graph today** — `RenderGraphPassSnapshot`
  (`src/Renderer/RenderGraph/RenderGraphSnapshot.h`) has no such field, and
  `PassRecord` (`src/Renderer/RenderGraph/RenderGraphTypes.h`) has no such
  field either.
- Confirmed by reading `src/Application/Application.cpp` and
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` directly: the Sky-View
  LUT / Aerial Perspective Volume / Aerial Perspective Composite passes are
  each called TWICE per frame — once inside the `if (gameTarget != nullptr)`
  block (writing `"..._GameView"`-suffixed resources) and once inside the
  `if (sceneTarget != nullptr)` block (writing `"..._SceneView"`-suffixed
  resources) — but **the literal pass NAME passed to `builder.AddComputePass(
  "AtmosphereSkyViewLutPass", ...)` is hardcoded identically both times**
  (confirmed at `AtmosphereLutRenderer.cpp` line 458 and equivalent call
  sites for the other passes). Only the WRITTEN RESOURCE name differs
  (`outputTextureName` parameter), never the pass name itself.
- Also confirmed: the render graph's culling model keeps a pass alive by
  adding its output handle to an explicit `outputs`/`KeepVolumeTextureOutput()`
  ROOT SET (`Application.cpp`'s big `AddPass`/`Execute` lambda), not purely by
  "does something else declare a read of it" — and the `"GameView"` pass
  itself (`RenderPasses.cpp::AddGameViewPass()`) does **not** declare a
  `pass.ReadTexture(...)`/`pass.ReadVolumeTexture(...)` dependency on the
  Sky-View LUT / Aerial Perspective Volume it visually depends on at all (that
  sampling happens via a raw Vulkan draw inside a `recordSkyBackground`
  callback, entirely outside the render graph's own dependency-declaration
  system — the exact same "phantom untracked dependency" pattern this codebase
  already has precedent for with GPU Skinning's vertex-buffer read, see
  `RenderPasses.cpp::DeclareGpuSkinningReads()`'s own doc comment). **This
  means a naive "walk backward from `GameView`'s own declared `readNames`"
  reachability algorithm would find NOTHING and is not viable as-is** — this
  was seriously considered and is explicitly REJECTED below in favor of a
  direct "view scope" tag (see Locked Design Decision #2).
- `FrameDebuggerCaptureContext` (`src/Editor/FrameDebuggerCapture.h/.cpp`)
  today only accumulates DEDUPLICATED lists of distinct Pipeline/MaterialTexture
  debug names plus a draw-call counter and the last view-projection matrix —
  it has **no concept of "which entity" issued which draw** at all.
  `RenderSystem::DrawCommand` (`src/Game/RenderSystem.h`) does not carry an
  `Entity` either — `CollectRenderables()` throws that information away as
  soon as it builds the `DrawCommand` list.
- Everything above was independently re-confirmed by reading the actual
  production source files listed, not guessed from documentation alone.

## 3. The Plan (How do we get there?) — Phase Index

| Phase | Title | Workstream | Touches |
|---|---|---|---|
| PHASE1 | RenderGraph ViewScope Choke-Point Infrastructure | A (foundation) | `RenderGraphTypes.h`, `RenderGraphSnapshot.h/.cpp`, `RenderGraphBuilder.h`, `AtmospherePassSequence.h/.cpp`, `AtmosphereLutRenderer.h/.cpp`, `RenderPasses.h/.cpp`, `Application.cpp`, `Editor/ComputeBlurValidation.h/.cpp` |
| PHASE2 | Frame Debugger Consumes ViewScope — Fix The Duplicate/Mis-Scoped Leak | A (consumer) | `FrameDebuggerData.h/.cpp`, `docs/conventions/frame-debugger.md`, `AGENTS.md`, tests |
| PHASE3 | Per-Draw-Call Entity Attribution Capture Infrastructure | B (foundation) | `RenderSystem.h/.cpp`, `FrameDebuggerCapture.h/.cpp`, tests |
| PHASE4 | GameView Per-Entity Draw Tree Leaves (the actual user-facing feature) | B (consumer) | `FrameDebuggerData.h/.cpp`, `Editor/Panels/FrameDebuggerPanel.h/.cpp` (the `RenderEventNode()` fix, Step 3.3 — required, not optional), `docs/conventions/frame-debugger.md`, `AGENTS.md`, tests |
| PHASE5 | Incremental Build, Light Live Verification, Docs & Completion Report | Both | build check, `gte_send_request` screenshots, `CAMPAIGN_COMPLETION_REPORT.md` |

Phases are strictly ordered and MUST be implemented in this order:
PHASE1 → PHASE2 → PHASE3 → PHASE4 → PHASE5. PHASE2 depends on PHASE1's new
`ViewScope` field existing; PHASE4 depends on PHASE3's new per-draw capture
data existing. PHASE1 and PHASE3 are otherwise independent of each other (they
touch almost entirely disjoint files) but must still land in this numeric
order to keep the campaign's own git history easy to review/bisect.

## 4. Locked Design Decisions (do not re-litigate without asking the user)

1. **This campaign explicitly BREAKS the "one leaf per PASS, never one leaf
   per mesh/entity" rule** that `frame-debugger-2`'s own
   `PHASE0_MASTER_STRATEGY.md` originally locked, and that `AGENTS.md`/
   `docs/conventions/frame-debugger.md` currently describe as "a PERMANENT,
   locked design fact". The user explicitly approved this breaking change
   (see Section 0, point 5/6 above) for exactly one thing: **the `"GameView"`
   leaf gains real, per-entity CHILD leaves; the underlying pass-level capture
   scope (Game View only, snapshot-on-demand, etc.) is otherwise unchanged.**
   PHASE4 must update every doc file that states the old rule so it no longer
   contradicts the shipped behavior (mirrors how `frame-debugger-5` itself
   rewrote `frame-debugger-2`'s "no hardcoded GPU Skinning special case" rule
   once already — this is a recognized, precedented category of change in
   this codebase, not a first-of-its-kind risk).
2. **The duplicate/mis-scoped compute-pass bug is fixed via a genuine,
   structural "ViewScope" tag threaded through the render graph's own
   choke-point APIs (`RenderGraphBuilder::AddPass()`/`AddComputePass()`),
   never via a resource-name/suffix string comparison living inside
   `FrameDebuggerData.cpp`.** A pure dependency-reachability graph walk (the
   OTHER "robust" option originally proposed to the user) was seriously
   evaluated and is explicitly REJECTED — see Section 2's own explanation of
   why `"GameView"`'s own declared reads are empty (a "phantom untracked
   dependency" via `recordSkyBackground`) and why this engine's render graph
   keeps passes alive via an explicit root-output set rather than a
   provably-connected consumer graph. The `ViewScope` tag is the closest
   available equivalent to the user's originally-requested "Option A"
   robustness: it is stamped ONCE, generically, at the same small set of
   Application-layer call sites that already know unambiguously which view
   they are building passes for, and every future pass author who correctly
   calls the new 4-argument `AddPass`/`AddComputePass` overload with an
   explicit `ViewScope` gets correct Frame Debugger scoping automatically,
   with zero Frame-Debugger-specific code ever required — this is the same
   "automatically discovered, never a hand-maintained special case" spirit
   `frame-debugger-5`'s own `PassRecord::isComputePass` already established,
   applied to a second, orthogonal question ("is this compute pass, and if so,
   which view is it for").
3. **New parameters/overloads only — every existing call site of `AddPass()`/
   `AddComputePass()` must keep compiling completely unchanged.** `ViewScope`
   defaults to a `Shared` value everywhere it is not explicitly overridden
   (mirrors `FrameDebuggerCaptureContext* capture = nullptr`'s own
   "new parameter with a safe default, zero forced churn at every existing
   call site" precedent used throughout this codebase). Only the handful of
   call sites PHASE1 explicitly identifies as genuinely per-view need to pass
   an explicit `ViewScope::GameView`/`ViewScope::SceneView`.
4. **Per-entity draw-attribution leaves reuse the EXISTING whole-frame
   preview-selection fallback rule (`ChooseFrameDebuggerPreviewSource()`) —
   this campaign does NOT attempt to render an isolated, masked/cropped image
   of just that one entity's own pixels.** Selecting a per-entity leaf shows
   the same `compositedPreview`/`preview` image every other non-"GameView"
   leaf already falls back to today. This is an explicit, honest, documented
   scope limit — isolating one mesh's own rendered pixels would need a real
   stencil/ID-buffer or a full draw-call-level command-buffer replay, which is
   a much larger, separate, NOT-yet-approved feature (see PHASE4's own
   "What We Will NOT Do" section). The value delivered is textual/data
   identification ("this exact tree row is where `terrain` was drawn, here is
   its own triangle count and shader"), which is exactly what the user asked
   for in their own words.
5. **`ViewScope::Shared` is the correct tag for GPU Skinning dispatches and
   the two Atmosphere passes computed once per frame (Transmittance,
   Multi-Scattering)** — these are NOT duplicated per view today and must
   keep appearing in the Frame Debugger tree exactly as they already correctly
   do; PHASE1/PHASE2 must not regress this. `ViewScope::SceneView` is the
   correct tag for `ComputeBlurValidation` (confirmed by reading
   `Application.cpp`: it is declared only inside the `sceneTarget != nullptr`
   block, reading the Scene View's own texture) — this pass currently
   incorrectly appears in the Frame Debugger's `"Compute Dispatches
   (Post-GameView)"` group whenever the "Show Compute Blur (debug)" toggle is
   on; PHASE2 fixes this as a natural side effect of the same mechanism, not
   as a separate special case.
6. **No full build or full `ctest` run except in PHASE5, and only if that
   phase's own instructions say so.** Every earlier phase does an incremental
   build only (`cmake --build build --target <the one target that changed>`
   or, if unsure which target, `cmake --build build` is still far cheaper than
   a full regression run — never invoke `ctest` before PHASE5). If an earlier
   phase's incremental build fails, fix it within that same phase before
   moving on — do not defer compile errors to PHASE5.
7. **Every Tier-1-tested file this campaign touches must get its test file
   updated in the SAME phase**, per `AGENTS.md`'s "Testability & Regression
   Safety" rule — never leave a new code path with zero coverage. Each phase
   document below names the exact test file(s) to update.

## 5. Cross-Phase Risk Notes

- PHASE1 is the highest-risk phase in this campaign: it touches
  `RenderGraphTypes.h`/`RenderGraphSnapshot.h`/`RenderGraphBuilder.h` (core,
  always-compiled, non-Editor rendering infrastructure used by EVERY pass in
  the engine) and `Application.cpp`'s real per-frame graph-building lambda.
  A mistake here can silently change real GPU synchronization/culling
  behavior, not just Editor display data. PHASE1's own document has an
  explicit, narrow "what must NOT change" checklist for exactly this reason.
  Given this risk, an EXTRA, dedicated double-check pass is scheduled for
  PHASE1 specifically, before this campaign's own full second-iteration
  double-check runs over every phase document — see this campaign's own
  orchestration notes (outside these `.md` files, in the delegation prompts
  themselves) for exactly when that extra check happens.
- PHASE3/PHASE4 touch `RenderSystem.h/.cpp` (`DrawCommand`, `CollectRenderables()`)
  which is genuinely Tier-1-tested, always-compiled Game-layer code (not
  Editor-only) — `tests/Game/RenderSystemTests.cpp` must be updated in PHASE3,
  not skipped.
- Every `#if GTE_ENABLE_EDITOR` boundary already established for
  `FrameDebuggerCaptureContext` (see `FrameDebuggerCapture.h`'s own top-of-file
  comment) must be preserved exactly — a `GTE_ENABLE_EDITOR=OFF` build must
  still compile AND LINK after every phase in this campaign.

## 6. Definition of Done for the whole campaign

- All five phase documents' own "Definition of Done" checklists are satisfied.
- `docs/conventions/frame-debugger.md` and `AGENTS.md`'s "Frame Debugger"
  section accurately describe the shipped end-state (no stale prose left
  describing the old, now-fixed duplicate-pass behavior or the old
  "permanently one leaf per pass" rule).
- A `CAMPAIGN_COMPLETION_REPORT.md` exists in this same folder (written in
  PHASE5), mirroring `frame-debugger-4`/`frame-debugger-5`'s own completion
  report shape and level of evidence.
- An incremental build succeeds with `GTE_ENABLE_EDITOR=ON` (the default dev
  configuration already in `build/`). PHASE5 alone additionally performs a
  light, targeted live HTTP/screenshot check (not a full `ctest` run) proving
  the duplicate-pass bug is gone AND the new per-entity `terrain` leaf is
  selectable and shows correct data.
