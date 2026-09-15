# PHASE1 — Renderer capture instrumentation (the foundation every later phase depends on)

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decisions #1, #5, #6
apply directly to this phase; do not relitigate them here).

**This is the highest-risk phase in the whole campaign** (see PHASE0's Step 3.5) — it is the only
one that touches the real, per-frame, performance-sensitive draw path. Take the extra care its
own flag implies.

## Step 1: The Goal

Give the engine a real, but completely OPT-IN and zero-overhead-when-disarmed, way to observe
"what did `RenderSystem::Draw()`/`Renderer::Submit()` actually do, for the Game View, this exact
frame" — which real `Pipeline`(s)/shader identities were used, which real `MaterialTexture`
debug names were bound, what the real view-projection matrix was, and what this engine's real
(constant) blend/Z/stencil configuration is — WITHOUT touching gameplay code, WITHOUT slowing down
a single frame where nothing is watching, and WITHOUT inventing any per-material variation this
engine doesn't already have.

## Step 2: The Situation

- `RenderSystem::Draw(Registry&, Renderer&, ...)` (`src/Game/RenderSystem.h/.cpp`) resolves every
  `DrawCommand` (mesh/pipeline/texture handles + world matrix) against its own
  `ResourcePool<Mesh, MeshHandle>`/`ResourcePool<Pipeline, PipelineHandle>`/
  `ResourcePool<MaterialTexture, TextureHandle>`, then calls `renderer.Submit(pipeline, mesh, model,
  viewProj, materialDescriptorSet)` once per resolved command — this is called ONCE per visible
  render target per frame (Game View, Scene View), each with its own `aspectWidthOverHeight`/
  `viewProjection`. Only the Game-View-driving call (from `AddGameViewPass()`'s `execute` callback,
  via `Game::Render()`) matters to this campaign (Locked Design Decision #7).
- `Renderer::Submit()` (`src/Renderer/Renderer.cpp`) either queues into the legacy
  `m_frameRecorder` (unused by the Game View path today) or, while
  `BeginGraphPassRecording()`/`EndGraphPassRecording()` bracket it (which `AddGameViewPass()`
  always does), calls `FrameRecorder::IssueDrawCommand()` directly and invokes
  `m_currentGraphPassRecordDrawStats` (which is how `PassGpuStats::drawStats` gets fused per pass
  today, per `AGENTS.md`'s "Profiling" section) — THIS is the single, already-existing, correct
  call site to also feed a NEW capture context, for the exact same "fuse the accounting into the
  real call site, never a second separate pass" correctness reason `DrawStats.h`'s own header
  comment already documents for triangle counting.
- `Pipeline` (`src/Renderer/Pipeline.h/.cpp`) carries NO name/identity today — its constructor
  takes `vertexShaderSpirvPath`/`fragmentShaderSpirvPath`/`vertexLayout`/`materialSetLayout` and
  builds a real `VkPipeline`, but nothing about which shader files/vertex layout it was built with
  survives past construction. Every real `Renderer::CreatePipeline()` call site already KNOWS this
  information at the moment it calls it (it's right there in the arguments) — this phase's job is
  purely to ALSO retain a small, cosmetic, human-readable copy of it, mirroring
  `RenderGraphTypes.h`'s own explicit, documented "a resource's human-readable name is threaded as
  its OWN separate parameter, never folded into an equality-compared desc struct" precedent
  (`TextureDesc`/`BufferDesc`'s own header comment) — applied here to `Pipeline` instead of a
  render-graph resource desc.
- `Pipeline.cpp` hardcodes exactly one blend/depth/stencil configuration
  (`colorBlendAttachment.blendEnable = VK_FALSE`, `VK_COMPARE_OP_LESS`, no stencil struct populated
  anywhere) — there is genuinely nothing to "capture" here per draw call; it is a compile-time
  constant fact about this whole engine today, so it belongs behind a pure, free, always-correct
  function, not a per-frame recording mechanism.
- `MaterialTexture` (`src/Renderer/MaterialTexture.h`) — confirm during implementation whether it
  already carries (or can cheaply be given) a debug-name string mirroring `Buffer`/`RenderTexture`'s
  own existing `GpuMemoryTracker`-facing debug-name convention (`AGENTS.md`, "GPU Resource Memory
  Tracking") — if it does not yet, extend it the same way those two already work (an
  optional/nullable `const char* debugName` constructor parameter, retained as an owned
  `std::string`), rather than inventing a second, parallel naming mechanism.
- **Zero-overhead-when-disarmed is a hard requirement, not a nice-to-have.** Every frame where the
  Frame Debugger window has never been opened (the overwhelming common case, including every
  release/non-Editor build) must pay ZERO extra cost — not an extra branch inside the innermost
  per-vertex/per-draw path beyond one cheap, predictable boolean check. Model this exactly like
  `GpuTimingService::SetCaptureEnabled()`'s own two-layer on/off gate (`AGENTS.md`, "Profiling") —
  a single, cheap, already-resolved bool checked once per `Submit()` call, not per vertex/per
  pipeline bind.

## Step 3: The Plan

### 3.1 New type: `FrameDebuggerCaptureContext`

Home: `src/Editor/FrameDebuggerCapture.h`/`.cpp` (Editor-only, `GTE_ENABLE_EDITOR`-gated, exactly
like every other `FrameDebuggerData.h`-adjacent file) — NOT `src/Renderer/`, even though it is fed
FROM `Renderer`/`RenderSystem`. Reasoning: this is a debugging/inspection concern the Renderer
itself must stay unaware of as a first-class feature (mirrors `AGENTS.md`'s "Clean Architecture"
rule — `Renderer` never depends on `Editor`); the real mechanism is a small, plain, ImGui-free,
pure-data "recorder" object that `Renderer`/`RenderSystem` accept a reference/pointer to (nullable,
default `nullptr` — "not armed", exactly like `GpuTimingSlot`'s own `std::optional` "opt out"
convention in `Renderer::RenderOffscreen()`) and record real facts into, WITHOUT `Renderer`/
`RenderSystem` themselves needing to `#include` anything from `src/Editor/`. Concretely:

- `RenderSystem::Draw(Registry&, Renderer&, float aspectWidthOverHeight, FrameDebuggerCaptureContext*
  capture = nullptr)` (and the explicit-view-projection overload) — a new, defaulted, LAST
  parameter, so every existing call site (Scene View's own `Draw()` call) is completely
  unaffected and stays `nullptr` forever (Scene View is out of scope — Locked Design Decision #7).
  Only the Game-View-driving call site (wherever `Game::Render()`'s Game-View branch invokes
  `RenderSystem::Draw()`) is ever handed a real, non-null pointer, and only while the capture
  context is armed.
- `FrameDebuggerCaptureContext` itself: a plain, `GTE_ENABLE_EDITOR`-only class with a small,
  explicit API — e.g. `void RecordDraw(const std::string& pipelineDebugName, const std::string&
  materialTextureDebugName, const Mat4& viewProj)` — appending into small, deduplicated internal
  sets/vectors (distinct pipeline names, distinct texture names) plus remembering the LAST
  `viewProj` it saw (real and correct, since every draw within one Game-View pass this frame uses
  the SAME view-projection matrix by construction — confirm this invariant holds by reading
  `RenderSystem::Draw()`'s own per-call-site `viewProj` argument before relying on it). `Reset()`
  clears it at the top of every armed frame (mirrors `FrameRecorder::BeginFrame()`'s own
  per-frame-clear convention).
- **Arming**: a single, explicit bool the CALLER (Application/`Game::Render()`'s Game-View branch)
  decides for itself once per frame — e.g. `Game::Render()` receives an optional
  `FrameDebuggerCaptureContext*` (already threaded down from `Application::Run()`, which is the one
  place that actually knows whether the Frame Debugger is currently armed for THIS frame, per
  PHASE3's capture-trigger logic) and passes it straight through to `RenderSystem::Draw()`. When
  that pointer is `nullptr` (every ordinary frame), the ENTIRE new code path inside
  `RenderSystem::Draw()`/`Renderer::Submit()` collapses to one already-taken "is this pointer
  null" branch per draw call — no string formatting, no vector `push_back`, no extra work of any
  kind happens.

### 3.2 `Pipeline`'s new cosmetic debug name

Add an optional `const char* debugName = nullptr` constructor parameter to `Pipeline`
(`src/Renderer/Pipeline.h/.cpp`), stored as an owned `std::string m_debugName` (empty string when
not supplied), with a `const std::string& DebugName() const noexcept` accessor. Every REAL
`Renderer::CreatePipeline()` call site in `src/Game/Game.cpp` (or wherever pipelines are actually
constructed for the Game View — confirm exact call sites during implementation) supplies a real,
hand-authored, human-readable name describing exactly what it built, e.g. `"Mesh.vert/Mesh.frag
(PositionNormal)"` or `"TexturedMesh.vert/TexturedMesh.frag (PositionNormalUv)"` — mirrors
`RenderGraphTypes.h`'s own explicit rule that a cosmetic name is threaded as a SEPARATE parameter,
never folded into anything equality-compared (`Pipeline` has no equality operator to begin with,
so there is no risk here, but the SPIRIT of that precedent — "a debug label is not physical
identity" — is exactly why this is a plain constructor parameter, not baked into any cache/pool
key). `RenderSystem::Draw()` reads `pipeline.DebugName()` (via its already-resolved
`Pipeline*`/`Pipeline&` from `m_pipelines.TryGet()`/equivalent) when a non-null capture context is
armed, and calls `capture->RecordDraw(pipeline.DebugName(), ..., viewProj)`.

### 3.3 `DescribeStandardPipelineState()` — the real, constant blend/Z/stencil facts

A small, pure, free function (home: alongside `FrameDebuggerCaptureContext`, in
`FrameDebuggerCapture.h/.cpp`, or directly in `Pipeline.h` if that reads more naturally once
written — implementer's call, document the choice) returning a small plain struct (or directly
populating the relevant `FrameDebuggerEventDetails` string fields — `blendMode`/`zClip`/`zTest`/
`zWrite`/`cull`/`stencilRef`/`stencilComp`/`stencilPass`/`stencilFail`/`stencilZFail`) with this
engine's REAL, hardcoded values, cross-checked against `Pipeline.cpp`'s actual
`VkPipelineColorBlendAttachmentState`/`VkPipelineDepthStencilStateCreateInfo` construction at
implementation time (do not guess — read the real values out of that file) — e.g. `blendMode =
"Opaque (no blend)"`, `zTest = "LEqual"`, `zWrite = "On"`, `cull = "None"` (confirm cull mode from
`Pipeline.cpp`'s `VkPipelineRasterizationStateCreateInfo`), `stencilRef`/`stencilComp`/etc. = `"n/a
(no stencil test)"`. This function takes NO parameters and needs no live `VkDevice` — it is purely
"transcribe these already-known compile-time facts into display strings" — and must therefore be
directly Tier-1-testable with a trivial "call it, assert the returned strings" test.

### 3.4 What this phase explicitly does NOT do

- Does not touch `Mesh`, `MaterialTexture`'s core shape (beyond an optional debug-name addition if
  missing — verify first, only add if genuinely absent), `FrameRecorder`, or any Scene-View/Present
  code path.
- Does not build the real `FrameDebuggerSnapshot` yet (that is PHASE2's job — this phase only
  produces the raw ingredients: an armed/disarmed `FrameDebuggerCaptureContext` plus
  `DescribeStandardPipelineState()`).
- Does not wire ANY UI, ANY capture trigger, or ANY ring buffer (PHASE3/PHASE4).
- Does not add a stencil/blend VARIANT to `Pipeline` — there is exactly one configuration in this
  engine today, and this phase reports that honestly rather than inventing a second one.

### 3.5 Tier-1 testing

`FrameDebuggerCaptureContext::RecordDraw()`/`Reset()` and `DescribeStandardPipelineState()` must
both be directly testable with zero live `VkDevice`/`Renderer` (plain strings/`Mat4` values in,
plain observable state out) — add `tests/Editor/FrameDebuggerCaptureTests.cpp` covering: recording
zero draws leaves an empty/default state; recording the same pipeline/texture name twice produces
exactly one distinct entry each (deduplication); recording two DIFFERENT pipeline/texture names
produces two distinct entries; `Reset()` clears everything back to the empty state;
`DescribeStandardPipelineState()`'s returned strings are non-empty and match the real values read
directly out of `Pipeline.cpp` at the time this test is written (so a future accidental change to
`Pipeline.cpp`'s hardcoded state is caught here too — this is a genuine regression-safety net, not
busywork).

### 3.6 Compile check

Fast compile check only (per `AGENTS.md`'s Tier-1 discipline): build `gte_core` and
`GreatTamanaEngineTests`, run `--gtest_filter=*FrameDebuggerCapture*`. No full build, no `ctest`
regression yet (that's PHASE8's job).

### 3.7 File-change inventory

New: `src/Editor/FrameDebuggerCapture.h`, `src/Editor/FrameDebuggerCapture.cpp`,
`tests/Editor/FrameDebuggerCaptureTests.cpp`.
Modified: `src/Renderer/Pipeline.h`/`.cpp` (debug name), `src/Renderer/Renderer.h`/`.cpp` (thread
the capture pointer through `Submit()`'s already-armed-graph-pass path), `src/Game/RenderSystem.h`/
`.cpp` (new defaulted parameter on `Draw()`), `src/Renderer/MaterialTexture.h`/`.cpp` (only if it
genuinely lacks a debug name today), `CMakeLists.txt`/`tests/CMakeLists.txt` (new files), plus
whichever `Game.cpp`/`Game.h` call sites actually construct Game-View pipelines (to supply real
`debugName` arguments).

Write `PHASE1_COMPLETION_REPORT.md` once done, following the exact template every prior
`frame-debugger-1`/`frame-debugger-2` phase report already used.
