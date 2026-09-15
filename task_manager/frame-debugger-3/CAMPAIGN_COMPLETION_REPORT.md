# `frame-debugger-3` — Campaign Completion Report: Real Frame Capture for the Editor "Frame Debugger"

Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`
Phases: 8 (all landed)

## Goal recap

Turn the `frame-debugger-2` campaign's GUI-only scaffolding into a genuinely
working, Unity-style Frame Debugger for the Game View render target: pressing
"Enable" freezes and captures one real rendered frame's worth of real Render
Graph passes, the left-hand tree shows those real passes (not a "No frame
captured yet." message), clicking one shows REAL shader/blend/Z/stencil/
texture/vector/matrix data plus a REAL preview image reconstructed as of that
exact point in the frame, a real multi-frame history ring buffer lets you step
backward/forward through several past captured frames, the Channels/Levels
controls actually affect the preview image, and the whole feature is drivable
end-to-end over the existing embedded HTTP server so an AI agent with no mouse
can open the window, enable it, select an event, and visually verify the
result via `GET /get_swapchain` — closing a gap `frame-debugger-1`/
`frame-debugger-2` both had to leave as an accepted limitation.

## Phase-by-phase summary

### PHASE1 — Renderer capture instrumentation

Gave the engine a real, but completely opt-in and zero-overhead-when-disarmed,
way to observe Game-View draw calls: a new `FrameDebuggerCaptureContext`
(`src/Editor/FrameDebuggerCapture.h/.cpp`) recording every distinct real
`Pipeline`/`MaterialTexture` debug name plus the last real view-projection
matrix used each frame, a cosmetic `debugName` constructor parameter threaded
through `Pipeline`/`GpuResourceFactory::CreatePipeline()`/
`Renderer::CreatePipeline()`, real hand-authored debug names at the three real
`CreatePipeline()` call sites, and a pure `DescribeStandardPipelineState()`
free function transcribing `Pipeline.cpp`'s real, hardcoded blend/Z/stencil
configuration into display-ready strings. `RenderSystem::Draw()` gained a
defaulted, LAST `FrameDebuggerCaptureContext*` parameter, guarded by
`#if GTE_ENABLE_EDITOR` in the `.cpp` (forward-declared, never `#include`d, in
the header) so the `GTE_ENABLE_EDITOR=OFF` configuration compiles and links
cleanly with zero trace of the type. One deviation: `MaterialTexture`'s
"genuinely per-instance debug name" requirement was satisfied by fixing the
one real call site (`MaterialTextureGpuCache::Resolve()`) rather than adding a
new field to `MaterialTexture` itself — a strictly smaller, more surgical fix.
Verified: 7 new Tier-1 tests passed; both Editor configurations compiled and
linked cleanly.

### PHASE2 — The real `FrameDebuggerSnapshot` builder

A new, pure, Tier-1-tested `BuildRealFrameDebuggerSnapshot()`
(`src/Editor/FrameDebuggerData.h/.cpp`) reshapes the ALREADY-REAL
`gte::rg::RenderGraphSnapshot` plus PHASE1's capture context into a real,
non-empty `FrameDebuggerSnapshot`, filtered to ONLY the Game View's own passes
(Locked Design Decision #7) — a root "Game View" group, an optional "GPU
Skinning" child group (one leaf per real matching compute pass, in real
execution order), and a final "GameView" leaf carrying every real aggregated
shader/texture/vector/matrix/blend/Z/stencil fact. Careful attention was paid
to the row-major-vs-column-major matrix transpose requirement (`Mat4` is
column-major; `FrameDebuggerMatrixProperty` is row-major) — a dedicated
regression test with a deliberately non-symmetric matrix confirms no
transpose bug. Zero changes to `Panels/FrameDebuggerPanel.cpp` this phase, per
the campaign's own explicit promise. Verified: 7 new Tier-1 tests passed (19
total Frame-Debugger tests, all green).

### PHASE3 — Frame history ring buffer + the real capture trigger

A real, 8-slot ring buffer, `FrameDebuggerHistory`
(`src/Editor/FrameDebuggerHistory.h/.cpp`), each slot holding one real
`FrameDebuggerSnapshot` plus a retained GPU copy of that historical frame's
real Game View output texture (`CaptureFrame()` mirrors
`Renderer::CaptureImagePixels()`'s own transition-copy-transition-back
discipline via `Renderer::ImmediateSubmit()`), plus the actual capture TRIGGER
wiring: the "Enable" checkbox's false→true edge, a new "Capture" button, and a
Step-while-Enabled path (split into a `NotifyFrameDebuggerStepConsumed()` flag
recorded at the documented call site plus the real `TriggerCapture()` call
serviced later the same frame, once real data actually exists — a necessary
correction to the phase document's own physically-impossible literal wording).
`Game::Render()`/`AddGameViewPass()` grew new, defaulted, LAST
`FrameDebuggerCaptureContext*` parameters, using PHASE1's own forward-declare/
`#if`-guard pattern so neither needs a guard at their own call sites.
Per this phase's own explicit scope limit, `FrameDebuggerPanel::Build()`'s
DISPLAYED content still read the placeholder snapshot — wiring the UI to real
data was left for PHASE4. Verified: 8 new Tier-1 tests passed (25 total); both
Editor configurations compiled and linked; a live runtime smoke test confirmed
no regression (HTTP automation for this window didn't exist yet, so the real
trigger paths couldn't be clicked through end-to-end yet — deferred to
PHASE4/PHASE7's own later verification, an accepted, documented limitation at
the time).

### PHASE4 — Panel wiring: real tree, real selection, and the new Frame-History UI

The Frame Debugger window showed something REAL for the first time:
`FrameDebuggerPanel::Build()` now reads `m_history.CurrentEntry()` instead of
unconditionally calling the placeholder builder, a new Frame-History
mini-toolbar (`BuildFrameHistoryToolbarRow()`, Prev/Next arrows calling
`m_history.StepCursor()`) was added as a SEPARATE control from the
pre-existing event stepper (Locked Design Decision #4), the RenderTarget
preview box now displays the real retained texture via a new,
`FrameDebuggerPanel`-OWNED ImGui descriptor (mirroring `BoneViewerWindow`'s
own self-contained descriptor-ownership precedent, with a matching explicit
destructor + `ReleasePreviewDescriptor()` call from `ImGuiEditorLayer`'s own
destructor before `ImGui_ImplVulkan_Shutdown()`), and the event-stepper row
now shows real numbers. A small, judged-correct addition beyond the phase
document's own literal text: `m_selectedEventIndex` now also resets to `-1`
whenever the VIEWED history entry changes (not just on a brand-new capture),
since a leaf `eventIndex` is only meaningful relative to the specific
snapshot it was assigned in. Verified: 26 total Frame-Debugger tests passed;
both Editor configurations compiled ("no work to do" for the OFF
configuration, confirming perfect Editor-gating); a live smoke test confirmed
overall engine stability (still no HTTP automation to click through the
actual Enable/Capture/tree-selection paths end-to-end — again deferred,
per this phase's own explicit permission to do so).

### PHASE5 — Event-details section: verify + finish against real data

**Verification passed clean — no genuine bug was found; zero production
source files changed this phase.** Rather than settle for code review alone
(no mouse/HTTP-automation tool existed yet to click through the UI), this
phase used a temporary, fully-reverted instrumentation patch plus the
existing `POST /instantiate_primitive` endpoint to populate the scene with a
real, untextured cube, then hand-cross-checked the resulting real
`FrameDebuggerEventDetails` dump against independently-computed expected
values — including a from-first-principles hand-derivation of the real
view-projection matrix (six independently-verified non-trivial matrix
entries, all matching bit-for-bit) that specifically confirms
`BuildGameViewLeaf()`'s matrix copy is NOT transposed and carries genuinely
live camera data. All temporary instrumentation was fully reverted and
confirmed via a clean `git status`/`git diff` before this phase's own report
was written. Verified: the existing 26-test Frame-Debugger Tier-1 suite,
unchanged, still passed 26/26.

### PHASE6 — Real Channels (All/R/G/B/A) + Levels preview processing

Made the Channels row and Levels slider functionally real via a brand-new,
dedicated preview-compositing mechanism: a pure, Tier-1-tested CPU oracle,
`ApplyFrameDebuggerPreviewTransform()`
(`src/Editor/FrameDebuggerPreviewProcessing.h/.cpp`), applying the Levels
remap first then Channel isolation (replicated grayscale, alpha forced
opaque), mirrored by a small GLSL compute shader
(`Shaders/FrameDebuggerPreview.comp`) dispatched by a new
`FrameDebuggerPreviewRenderer` (copying `VolumeTexturePreviewRenderer`'s own
dispatch SHAPE, using a safe, storage-capable `Texture2D` scratch output
rather than a `RenderTexture`, since `RenderTexture`'s negotiated BGRA format
is not guaranteed to support storage-image usage on every driver) — explicitly
NOT reusing `/get_texture`'s own, semantically-unrelated `channel=color|depth`
parameter (Locked Design Decision #8). `EnsurePreviewDescriptor()` was
rewritten to recompute ONLY when something (channel/levels/viewed history
entry) actually changed, never every ImGui frame, and to fall back to the
raw, zero-dispatch retained texture whenever the Channels/Levels state is
neutral. A previously-latent bug this phase's own change would have
introduced (a stale-selection-reset comparison silently breaking once a
processed preview view could differ from the raw retained one) was caught and
fixed with a dedicated, separate tracking field before it ever shipped.
Verified: 9 new Tier-1 tests passed (35 total, across 7 suites); a live smoke
test confirmed overall engine stability (the window itself wasn't open this
session, since no mouse/HTTP automation existed yet — again PHASE7's own job).

### PHASE7 — HTTP automation + pinning the window to the main viewport

Flagged as the SECOND-HIGHEST-RISK phase in the whole campaign
(`PHASE0_MASTER_STRATEGY.md`'s Step 3.5). Built and verified the
main-viewport pin FIRST, in isolation, then added a brand-new, fully
independent sibling bridge, `FrameDebuggerCommandBridge`
(`src/Application/FrameDebuggerCommandBridge.h/.cpp` — mirroring
`EditorUiCommandBridge`'s exact shape field-for-field, never extending
`EditorUiCommandKind`, per `AGENTS.md`'s own explicit rule), and eight new
HTTP routes (`GET /frame_debugger/open|enable|capture|select_event|
step_history|set_channel|set_levels|state`) wired through
`NetworkRoutes.h/.cpp` (pure parsing/response building) and
`NetworkServer.cpp` (the only file touching `httplib::Server`/the bridge
directly). The main-viewport pin (`FrameDebuggerPanel::RequestOpenWindow()`
setting a one-shot flag ONLY on a genuine programmatic
`false -> true` transition, never on the manual "Window" menu item) guarantees
the window is visible to `GET /get_swapchain` the very first captured frame
after an HTTP-driven open. A deliberate, well-justified deviation: `GET
/frame_debugger/state` goes through the SAME bridge as an 8th command kind
(a `GetState` kind) rather than a lighter-weight direct read, since the
Frame Debugger's live state is genuinely mutable, main-thread-owned data with
no atomics of its own — reading it from the network thread without the bridge
would be a real, if narrow, data race, and no other stateful read-only
endpoint in this codebase takes that shortcut either. Verified: 62 total
tests across 16 suites passed (7 new `FrameDebuggerCommandBridgeTest` cases,
18 new `NetworkRoutesTests`-family cases, every pre-existing PHASE1–PHASE6
Frame Debugger test — 35 — unaffected); both Editor configurations compiled;
a full live smoke test (the first one able to actually click through
Enable/Capture/select-event/step-history/set-channel/set-levels, since the
HTTP surface finally existed) confirmed the whole chain worked end-to-end,
with `GET /get_swapchain` visually confirming every meaningful step.

### PHASE8 — Integration, build wiring, docs, full regression, live automated end-to-end verification (this phase)

Closed out the campaign. Re-confirmed every build-system wiring point named in
its own Step 3.1 checklist already existed correctly (nothing was missing —
zero build-system files needed changes, across `CMakeLists.txt`,
`tests/CMakeLists.txt`, and `cmake/CompileShaders.cmake`'s handling of
`FrameDebuggerPreview.comp`). Wrote the full documentation sweep: a completely
rewritten `docs/conventions/frame-debugger.md` retiring the "GUI SCAFFOLDING
ONLY"/"glue seams" framing entirely in favor of describing the real system
(while explicitly preserving the still-true "no per-draw-call granularity"/
"Game-View-only scope" warnings as PERMANENT design facts, cross-referencing
`PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decisions #1/#7 so a future
reader never mistakes them for unfinished work), a rewritten "Frame Debugger"
section in `AGENTS.md` (word "scaffolding" dropped from both heading and body,
mirroring "Time and Playback Pause"'s own tone/structure), a new top-of-
"Status" bullet in `README.md`, an updated index entry in `docs/README.md`,
and a restructured "Frame Debugger" section in `TODO.md` (a "now DONE" list
covering everything the `frame-debugger-2` campaign had explicitly deferred,
plus a "still genuinely deferred" list of permanent design choices/real future
work). Then ran the full validation: a **full clean build** for both
`GTE_ENABLE_EDITOR=ON` (`build/`, 441/441 steps) and `=OFF`
(`build-editor-off/`, freshly configured, 366/366 steps), both with **zero
errors** — confirming every Editor-only Frame Debugger file/shader is
correctly excluded entirely from the Editor-OFF configuration while every
always-compiled file (the command bridge, the eight new stub overrides in
`NullEditorLayer.cpp`, the network routes/server, `Application.cpp`) compiles
correctly in both regimes; a **full `ctest` regression pass** — **100% of
1402 tests passed** (the same single pre-existing, machine-gated
`PmxLoaderRealModelSmokeTest` skip this repository has always had), plus an
extra, not-strictly-required `build-editor-off` `ctest` pass (**100% of
1180 tests passed**) for additional confidence; and — the closing achievement
of the whole campaign — a genuine, fully-automated, HTTP-driven,
screenshot-verified end-to-end smoke test exercising the EXACT 9-step
sequence this phase's own Step 3.4 lays out (open → enable/auto-capture →
state → select_event → capture → step_history → set_channel → set_levels →
clean-up), with every step's real, observed result (not just "it worked")
recorded in `PHASE8_COMPLETION_REPORT.md`. See that report for the full
per-step evidence and the one small self-caught-and-fixed documentation
editing mistake (never committed).

## Final architecture (as landed)

```
Renderer::Submit() / RenderSystem::Draw()              (PHASE1)
   |  (only when FrameDebuggerCaptureContext is ARMED - zero overhead otherwise)
   v
FrameDebuggerCaptureContext                              (PHASE1: aggregates real shader/texture/
   |                                                      vector/matrix facts for the CURRENT frame)
   v
gte::rg::RenderGraphSnapshot (ALREADY REAL, existing) + FrameDebuggerCaptureContext
   |
   v
BuildRealFrameDebuggerSnapshot(...)                      (PHASE2: pure reshape, Game-View-only filter)
   |
   v
FrameDebuggerHistory (8-slot ring buffer)                (PHASE3: capture trigger + retained texture copy)
   |
   v
FrameDebuggerPanel::Build()                               (PHASE4/5/6: real tree, real details,
   |                                                        real preview + Channels/Levels via
   |                                                        FrameDebuggerPreviewProcessing.h/.cpp)
   v
FrameDebuggerCommandBridge -> /frame_debugger/*            (PHASE7: HTTP automation + main-viewport pin)
   |
   v
GET /get_swapchain                                        (PHASE8: the final, live, screenshot-verified
                                                             proof every layer above works end-to-end)
```

`FrameDebuggerData.h/.cpp` remains the pure, ImGui-free data model at the
center of it all - now populated with genuinely real data end-to-end, from a
real per-frame GPU-adjacent capture context all the way to a real, HTTP-
drivable, screenshot-verifiable Editor window.

## File-change inventory (final, as actually landed across all 8 phases)

New files:
- `src/Editor/FrameDebuggerCapture.h`, `src/Editor/FrameDebuggerCapture.cpp`
  (PHASE1)
- `src/Editor/FrameDebuggerHistory.h`, `src/Editor/FrameDebuggerHistory.cpp`
  (PHASE3)
- `src/Editor/FrameDebuggerPreviewProcessing.h`,
  `src/Editor/FrameDebuggerPreviewProcessing.cpp` (PHASE6)
- `src/Shaders/FrameDebuggerPreview.comp` (PHASE6)
- `src/Application/FrameDebuggerCommandBridge.h`,
  `src/Application/FrameDebuggerCommandBridge.cpp` (PHASE7)
- `tests/Editor/FrameDebuggerCaptureTests.cpp` (PHASE1)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE2)
- `tests/Editor/FrameDebuggerHistoryTests.cpp` (PHASE3)
- `tests/Editor/FrameDebuggerPreviewProcessingTests.cpp` (PHASE6)
- `tests/Application/FrameDebuggerCommandBridgeTests.cpp` (PHASE7)
- `task_manager/frame-debugger-3/PHASE1_COMPLETION_REPORT.md` ..
  `PHASE8_COMPLETION_REPORT.md`
- `task_manager/frame-debugger-3/CAMPAIGN_COMPLETION_REPORT.md` (this file)

Modified files:
- `src/Renderer/Pipeline.h`/`.cpp` (PHASE1 — cosmetic debug-name parameter)
- `src/Renderer/GpuResourceFactory.h`/`.cpp`, `src/Renderer/Renderer.h`/`.cpp`
  (PHASE1 — `debugName` threaded through `CreatePipeline()`)
- `src/Game/Instantiation/MeshAssetGpuCatalog.cpp`,
  `src/Game/Instantiation/PrimitiveGpuCatalog.cpp`,
  `src/Game/Instantiation/MaterialTextureGpuCache.cpp` (PHASE1 — real debug
  names at the three real `CreatePipeline()` call sites + the one
  `MaterialTexture` naming fix)
- `src/Game/RenderSystem.h`/`.cpp` (PHASE1 — new defaulted
  `FrameDebuggerCaptureContext*` parameter, forward-declared + `#if`-guarded)
- `src/Editor/FrameDebuggerData.h`/`.cpp` (PHASE2 — `BuildRealFrameDebuggerSnapshot()`;
  PHASE4 — `FormatFrameHistoryLabel()`)
- `src/Editor/EditorLayer.h` (PHASE3 — `PrepareFrameDebuggerCaptureContext()`/
  `NotifyFrameDebuggerStepConsumed()`; PHASE7 — `FrameDebuggerStateSnapshotView`
  + eight new pure-virtual HTTP-automation entry points)
- `src/Editor/NullEditorLayer.cpp` (PHASE3/PHASE7 — inert no-op overrides)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE3/PHASE4/PHASE7 — real wiring,
  destructor descriptor release, eight new HTTP-automation overrides)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE3–PHASE7 — the bulk of
  this campaign's real logic: capture trigger, real tree/history/preview
  wiring, real Channels/Levels, HTTP-automation entry points, main-viewport
  pin)
- `src/Game/Game.h`/`.cpp` (PHASE3 — new defaulted
  `FrameDebuggerCaptureContext*` parameter on `Render()`)
- `src/Application/RenderPasses.h`/`.cpp` (PHASE3 — new parameter on
  `AddGameViewPass()`)
- `src/Application/Application.h`/`.cpp` (PHASE3 — capture-trigger wiring;
  PHASE7 — bridge construction + per-frame pump)
- `src/Network/NetworkRoutes.h`/`.cpp`, `src/Network/NetworkServer.h`/`.cpp`
  (PHASE7 — five new parsers, two new response builders, eight new routes)
- `CMakeLists.txt`, `tests/CMakeLists.txt` (PHASE1/PHASE3/PHASE6/PHASE7 — new
  source files; re-confirmed complete with zero gaps in PHASE8)
- `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`,
  `docs/conventions/frame-debugger.md` (all PHASE8)

`src/Editor/EditorPanelCatalog.h`/`tests/Editor/EditorPanelCatalogTests.cpp`
were **never** touched by any phase of this campaign — the Frame Debugger
window remains deliberately absent from `GET /activate_tab`'s catalog, per
`frame-debugger-2`'s own Locked Design Decision #6, unchanged across the
entire `frame-debugger-3` campaign — its own dedicated `/frame_debugger/*`
route family is a completely separate, purpose-built automation surface.

## Final verification evidence (PHASE8)

- **Full clean build, `GTE_ENABLE_EDITOR=ON`**: `cmake --build build
  --clean-first` — 441/441 steps, zero errors.
- **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: fresh configure
  (`-DGTE_ENABLE_EDITOR=OFF`, zero network access needed) +
  `cmake --build build-editor-off --clean-first` — 366/366 steps, zero
  errors; confirmed every Editor-only Frame Debugger source/shader file is
  entirely excluded.
- **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure` (from
  `build/`) — **100% tests passed, out of 1402** (1 pre-existing machine-gated
  skip), 98.89 sec total; also 100% of 1180 tests passed from
  `build-editor-off/` (extra, not strictly required).
- **Live, HTTP-automation-driven end-to-end smoke test**: the exact 9-step
  sequence from `PHASE8_INTEGRATION_BUILD_DOCS_AND_FULL_VERIFICATION.md`'s own
  Step 3.4 — `open` (window pinned to main viewport, confirmed via
  `/get_swapchain`) → `enable?value=true` (auto-capture fires,
  `historyCount` 0→1, real tree appears) → `state` (`enabled: true`,
  `totalEventCount: 1`, `historyCount: 1`) → `select_event?index=0` (real
  Shader/Pass/Blend/Z-state/Stencil details appear) → `capture`
  (`historyCount` 1→2) → `step_history?direction=prev` (`historyCursor` 1→0,
  viewed frame visibly changes, "Frame 1 of 2") → `set_channel?value=r`
  (preview turns into an isolated-red grayscale gradient, "R" highlighted) →
  `set_levels?black=0.2&white=0.8` (contrast visibly stretches, "Black
  0.20"/"White 0.80") → `enable?value=false` + `stop_app_background` (clean
  shutdown) — every step confirmed with a real screenshot from a real running
  `GreatTamanaEngine.exe` instance, with zero mouse/keyboard involved at any
  point.

## Manual-verification limitation: CLOSED

Every prior campaign touching this feature (`frame-debugger-1`,
`frame-debugger-2`, and PHASE1–PHASE6 of this very campaign) had to accept, as
a documented, unavoidable limitation, that actually opening the "Frame
Debugger" window, checking "Enable", clicking into the tree, and visually
confirming the result could not be exercised remotely in this environment —
there was no mouse-control tool available, and no HTTP command endpoint
existed anywhere capable of driving this specific window. **PHASE7 built that
endpoint surface, and PHASE8 (this phase) is the first time in this feature's
entire history that the FULL feature — open, enable, auto-capture, select an
event, explicit capture, step through history, isolate a channel, remap
levels — was driven end-to-end with zero mouse/keyboard involved, and visually
confirmed via a real screenshot at every single step.** This gap is now
CLOSED, not merely documented as accepted — see the live smoke-test evidence
above and in `PHASE8_COMPLETION_REPORT.md` for the exact, reproducible
sequence.

## Outstanding / deferred (see `TODO.md` for the full, current list)

- Per-individual-draw-call event granularity — a deliberate, PERMANENT design
  choice (Locked Design Decision #1), not a gap.
- Scene View or Present-pass capture — a clean, well-isolated future
  extension point (a single filter line), not attempted this campaign
  (Locked Design Decision #7).
- A full, generic shader-reflection system (real SPIR-V reflection data) —
  this engine has exactly one Pipeline configuration today, so there is
  nothing to reflect beyond the small, hand-authored, constant-valued
  `DescribeStandardPipelineState()`; a genuine future campaign would only be
  needed once real per-material blend/Z/stencil variation exists to reflect.
- A `RenderGraphPanel`/`ProfilerPanel`-style Pause/frozen-snapshot control on
  this panel — redundant with the already-real snapshot-on-demand mechanism.
- Publishing the retained preview texture into
  `RenderGraphDebugTextureRegistry` — explicitly optional, never needed, since
  the dedicated `/frame_debugger/*` routes are already a complete,
  sufficient automation surface.

## Conclusion

All eight phases of `frame-debugger-3` landed exactly per
`PHASE0_MASTER_STRATEGY.md`'s plan, with every deviation along the way judged,
justified, and explicitly documented in each phase's own completion report
(never a silent shortcut). The Editor's "Frame Debugger" window is now a
genuinely working, Unity-style tool for the Game View: real pass-level
capture, a real 8-slot frame-history ring buffer with retained GPU preview
textures, real pass-scoped shader/blend/Z/stencil/texture/vector/matrix
reflection, a real Channels/Levels compositing pipeline, and a complete,
independent `/frame_debugger/*` HTTP automation surface with a main-viewport
pin guaranteeing the window is always visible to `GET /get_swapchain`.
Verified by a full clean build (both Editor configurations, 441/441 and
366/366 steps respectively, zero errors), a full `ctest` regression pass
(100% of 1402 tests), and — for the first time in this feature's entire
three-campaign history — a genuine, fully-automated, HTTP-driven,
screenshot-verified end-to-end smoke test of the ENTIRE feature with no
mouse/keyboard involved at all, finally closing the manual-verification gap
both `frame-debugger-1` and `frame-debugger-2` had to accept.
