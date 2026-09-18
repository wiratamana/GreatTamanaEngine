# PHASE1 — Render Pass Draw-Kind Vocabulary

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). Read
that file first — it is the single source of truth for every fact quoted
below and for every Locked Design Decision this phase implements._

## Step 1: The Goal (Where are we going?)

Give every Graphics-kind Render Graph pass a real, structural, purely
descriptive way to declare WHAT KIND of draw operation it issues —
`DrawMesh` (a real per-object mesh draw — the implicit default, matching
every existing pass's real behavior today), `DrawQuad` (a full-screen/
screen-space triangle-or-quad trick with no per-object identity — e.g.
`"DrawSkyBackground"`), or `Blit` (a raw image copy/blit — a real, currently
UNUSED scaffold value, per PHASE0's Locked Design Decision #4). This new
`rg::RenderPassDrawKind` enum is the fact PHASE2's Frame Debugger tree
rework reads to label a Graphics-kind pass's new child event row correctly,
without ever hardcoding a pass-name string comparison.

This phase touches ONLY the Render Graph's own pure vocabulary/builder
layer (`src/Renderer/RenderGraph/`) plus one single real call site
(`AddDrawSkyBackgroundPass()`). It does NOT touch
`src/Editor/FrameDebuggerData.cpp` at all — that is PHASE2's job. This
phase's own deliverable is fully inert/unused by the Frame Debugger until
PHASE2 lands; it must still compile and pass its own new tests entirely on
its own.

## Step 2: The Situation (Where are we now?)

1. `src/Renderer/RenderGraph/RenderGraphTypes.h` already has TWO precedents
   for exactly this kind of addition, both worth copying verbatim in shape:
   - `enum class PassKind : std::uint8_t { Graphics, Compute };` (lines
     342-345) + `const char* ToString(PassKind kind) noexcept;` (line 347).
   - `enum class RenderPassCategory : std::uint8_t { General,
     AtmosphereLut, GpuSkinning, Debug };` (lines 358-363) + `const char*
     ToString(RenderPassCategory category) noexcept;` (line 365) — this one
     is explicitly documented as "PURELY DESCRIPTIVE metadata... nothing in
     `RenderGraph.cpp`/`RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`
     ever reads this field" (lines 349-357) — the new `RenderPassDrawKind`
     enum this phase adds follows this EXACT same rule.
   Both `ToString()` free functions are implemented in
   `src/Renderer/RenderGraph/RenderGraphTypes.cpp` (lines 63-91) as
   deliberately `default:`-free exhaustive switches (see that file's own
   header comment for why — a future enumerator must fail to compile here
   until every consumer is updated).
2. `struct PassRecord` (`RenderGraphTypes.h`, lines 447-524) already carries
   `RenderPassCategory category = RenderPassCategory::General;` as its LAST
   field (line 523), appended at the end per this file's own "never insert
   a field in the middle" convention (see the struct's own comments). The
   new `drawKind` field is appended immediately after it, same convention.
3. `struct RenderGraphPassSnapshot` (`src/Renderer/RenderGraph/RenderGraphSnapshot.h`,
   lines 89-134) mirrors `PassRecord`'s own `kind`/`category`/`viewScope`
   fields (lines 105, 111, 118) for downstream (Frame Debugger) consumption.
   `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()`
   (lines 85-117) copies each of those straight through, unconditionally, for
   BOTH a surviving AND a culled pass (see lines 91-93: `snapshot.kind =
   pass.kind;` / `snapshot.category = pass.category;` / `snapshot.viewScope
   = pass.viewScope;`) — a culled pass must still truthfully report these
   facts, per this file's own established rule.
4. `RenderGraphBuilder::AddRenderPass()` (`src/Renderer/RenderGraph/RenderGraphBuilder.h`,
   lines 423-455) has exactly two overloads:
   - The full, 6-argument form (lines 434-444):
     `AddRenderPass(const char* name, PassKind kind, ViewScope viewScope,
     RenderPassCategory category, SetupFn&& setup, ExecuteFn&& execute)`.
   - The 4-argument convenience form (lines 450-455), which defaults
     `viewScope`/`category` and forwards to the form above.
   Both are function TEMPLATES on `SetupFn`/`ExecuteFn` — a new, TRAILING,
   non-template parameter with a default value can be added after `execute`
   in both without touching template argument deduction at all (deduction
   only ever looks at the actual lambda arguments supplied; a trailing
   defaulted plain-type parameter is completely independent of that,
   standard C++ default-argument/template-deduction interaction). This is
   the ONLY safe insertion point — inserting a new parameter BEFORE
   `setup`/`execute` would break every existing 6-argument positional call
   site in the engine (there is no way to "skip" a middle parameter even
   with a default).
5. `src/Application/RenderPasses.cpp`'s `AddDrawSkyBackgroundPass()` (lines
   142-183) is the ONE real call site that needs an explicit, non-default
   tag this phase: its `builder.AddRenderPass(...)` call (lines 158-168)
   passes `"DrawSkyBackground"`, `rg::PassKind::Graphics`,
   `rg::ViewScope::GameView`, `rg::RenderPassCategory::General`, then the
   setup/execute lambdas — the new trailing `drawKind` argument goes right
   after the execute lambda.
6. `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp` line 290
   confirms, by direct source read, `vkCmdDraw(cmd, 3, 1, 0, 0)` — a real
   3-vertex, 1-instance draw call with no index buffer and no per-object
   vertex data — the standard "one oversized triangle covering the whole
   screen" full-screen-quad technique. This is the concrete, hand-verified
   fact backing the `DrawQuad` tag (PHASE0's Locked Design Decision #3).
7. No other real pass declaration in the engine needs a non-default tag
   this phase — every other existing Graphics-kind pass (`"RenderOpaque"`,
   the N `"FrameDebuggerReplayStepN"` debug replay passes, `"SceneView"`,
   `"Present"`) is a genuine per-object/whole-scene mesh draw (or, for
   `"SceneView"`/is out of this campaign's Game-View-only tree scope
   entirely) — `RenderPassDrawKind::DrawMesh`, the default, is already
   correct for every one of them with zero code changes.

## Step 3: The Plan

### 3.1 New enum + `ToString()` declaration (`RenderGraphTypes.h`)

Immediately after the existing `RenderPassCategory` enum + its `ToString()`
declaration (after line 365), add:

```cpp
// Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE1 - which STRUCTURAL kind of draw operation a Graphics-kind pass
// issues - PURELY DESCRIPTIVE metadata for the Editor Frame Debugger's own
// child-event-labeling purposes (PHASE2 of this campaign) ONLY. Nothing in
// RenderGraph.cpp/RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp ever
// reads this field - mirrors RenderPassCategory's own identical rule
// (immediately above). Meaningless for a Compute-kind pass (PassRecord::kind
// == PassKind::Compute) - a compute dispatch's own Frame Debugger child
// event is always labeled "Compute Dispatch" regardless of this field's
// value; every Compute-kind pass simply leaves this at its own default.
//
// `Blit` is a REAL, currently COMPLETELY UNUSED scaffold enumerator - no
// pass in this engine is tagged with it today, and none can be until a
// FUTURE campaign teaches RenderGraph::Execute() a genuinely new recording
// path (a real vkCmdBlitImage/vkCmdCopyImage cannot be issued inside a
// vkCmdBeginRendering/vkCmdEndRendering bracket, which every Graphics-kind
// pass uses today) - see PHASE0_MASTER_STRATEGY.md's Locked Design Decision
// #4 for the full reasoning. Adding it now, unused, means that future
// campaign needs zero further Frame Debugger changes once it lands.
//
// Deliberately an exhaustive-switch-friendly small enum, mirroring
// PassKind's/RenderPassCategory's own "no default: case, ever" convention in
// this same file.
enum class RenderPassDrawKind : std::uint8_t {
    DrawMesh, // The default - a real per-object mesh draw (e.g. "RenderOpaque"'s
        // own per-entity children, the N FrameDebuggerReplayStepN debug
        // replay passes, a future real "RenderTransparent").
    DrawQuad, // A full-screen/screen-space triangle-or-quad draw with no
        // per-object identity (e.g. "DrawSkyBackground" - see
        // AtmosphereSkyBackgroundRenderer.cpp's own vkCmdDraw(cmd, 3, 1, 0, 0)).
    Blit, // A raw image copy/blit - see this enum's own header comment
        // above for why this is a real-but-unused scaffold value today.
};

const char* ToString(RenderPassDrawKind drawKind) noexcept;
```

### 3.2 New field on `PassRecord` (`RenderGraphTypes.h`)

Immediately after the existing `RenderPassCategory category =
RenderPassCategory::General;` field (line 523), append:

```cpp
    // Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
    // PHASE1 - which structural kind of draw operation this pass issues
    // (see RenderPassDrawKind's own doc comment above) - purely descriptive
    // metadata for the Editor Frame Debugger's child-event-labeling purposes
    // (PHASE2), read by NOTHING in RenderGraph.cpp/RenderGraphCompiler.cpp/
    // RenderGraphBarrierPlanner.cpp. Appended at the END of the struct
    // (never inserted in the middle) - see this file's own header comment.
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;
```

### 3.3 `ToString()` definition (`RenderGraphTypes.cpp`)

Immediately after the existing `ToString(RenderPassCategory category)`
definition (after line 91), add:

```cpp
// Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE1 - see RenderGraphTypes.h's own comment on RenderPassDrawKind for
// why this exists.
const char* ToString(RenderPassDrawKind drawKind) noexcept
{
    // Deliberately NO `default:` case - see this file's own header comment.
    switch (drawKind) {
    case RenderPassDrawKind::DrawMesh:
        return "DrawMesh";
    case RenderPassDrawKind::DrawQuad:
        return "DrawQuad";
    case RenderPassDrawKind::Blit:
        return "Blit";
    }
    return "Unknown";
}
```

### 3.4 New field on `RenderGraphPassSnapshot` + copy-through (`RenderGraphSnapshot.h`/`.cpp`)

In `RenderGraphSnapshot.h`, immediately after the existing `RenderPassCategory
category = RenderPassCategory::General;` field (line 111), append:

```cpp
    // Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
    // PHASE1 - copied straight through for BOTH a surviving AND a culled
    // pass, exactly like `category` above (a culled pass must still
    // truthfully report what kind of draw it WOULD have issued).
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;
```

In `RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()`, immediately after the
existing `snapshot.category = pass.category; // Render Pass campaign PHASE1`
line (line 92), append:

```cpp
    snapshot.drawKind = pass.drawKind; // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE1
```

### 3.5 New trailing parameter on both `AddRenderPass()` overloads (`RenderGraphBuilder.h`)

Rewrite the full 6-argument overload (lines 434-444) to:

```cpp
    template <typename SetupFn, typename ExecuteFn>
    void AddRenderPass(const char* name, PassKind kind, ViewScope viewScope, RenderPassCategory category,
        SetupFn&& setup, ExecuteFn&& execute, RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh)
    {
        if (kind == PassKind::Compute) {
            AddComputePass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        } else {
            AddPass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        }
        m_passes.back().category = category;
        m_passes.back().drawKind = drawKind; // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE1
    }
```

Rewrite the 4-argument convenience overload (lines 450-455) to:

```cpp
    template <typename SetupFn, typename ExecuteFn>
    void AddRenderPass(const char* name, PassKind kind, SetupFn&& setup, ExecuteFn&& execute,
        RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh)
    {
        AddRenderPass(name, kind, ViewScope::Shared, RenderPassCategory::General,
            std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute), drawKind);
    }
```

Add a short doc comment above each mirroring the "purely descriptive,
defaulted, existing call sites unaffected" reasoning already documented for
`category`/`viewScope` immediately above them. **Verify, by grep, that
EVERY existing call site of both overloads across the whole `src/` tree
still compiles unmodified** — every one of them passes exactly `name`,
`kind`, (`viewScope`, `category`,) `setup`, `execute` positionally, with
nothing after `execute`, so the new trailing defaulted parameter must not
require touching any of them except the one below.

### 3.6 Tag `"DrawSkyBackground"` (`src/Application/RenderPasses.cpp`)

In `AddDrawSkyBackgroundPass()` (lines 142-183), change the
`builder.AddRenderPass(...)` call (lines 158-168) to pass the new trailing
argument explicitly:

```cpp
    builder.AddRenderPass(
        "DrawSkyBackground", rg::PassKind::Graphics, rg::ViewScope::GameView, rg::RenderPassCategory::General,
        [gameViewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(gameViewTarget);
            pass.WriteDepthStencilAttachment(gameViewTarget);
        },
        [&renderer, recordSkyBackground](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            recordSkyBackground(ctx.cmd);
            renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawQuad); // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE1 -
            // see AtmosphereSkyBackgroundRenderer.cpp's own vkCmdDraw(cmd, 3, 1, 0, 0)
            // - a real full-screen-triangle draw, never a per-object mesh draw.
```

Add a one-line doc comment above the function referencing this campaign,
mirroring the file's own existing comment style for prior campaigns.

### 3.7 Tier-1 tests (add to existing test files — do not create new ones
unless a clearly-matching file doesn't already exist)

- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (or wherever
  `ToString(PassKind)`/`ToString(RenderPassCategory)` are already tested —
  grep for `TEST(.*ToString` in `tests/Renderer/RenderGraph/` to find the
  exact file): add a test asserting `ToString(RenderPassDrawKind::DrawMesh)
  == "DrawMesh"`, `...::DrawQuad) == "DrawQuad"`, `...::Blit) == "Blit"`.
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` (confirmed via
  `search_in_dir` for `BuildPassSnapshot` — that string only appears in a
  comment there, but this is still the right file): add a test confirming a
  `PassRecord` with a non-default `drawKind` survives into its
  `RenderGraphPassSnapshot` unchanged, for BOTH a surviving and a culled
  pass. **Correction from this document's own double-check pass:** there is
  NOT already an existing test covering `RenderPassCategory`'s own
  culled-pass copy-through to "mirror" — only `kind`
  (`KindIsCopiedThroughForSurvivingGraphicsAndComputePasses`,
  `CulledComputePassStillReportsComputeKindAndCorrectWriteKind`) and
  `viewScope` (`ViewScopeIsCopiedThroughForSurvivingAndCulledPasses`) have
  dedicated tests today — mirror THOSE two tests' shape instead (one
  surviving-pass assertion, one culled-pass assertion, each additionally
  reading the new `drawKind` field); do not spend time looking for a
  `category`-specific test that does not exist.
- `tests/Renderer/RenderGraph/RenderPassTests.cpp` (the file the grep
  earlier in this campaign's investigation found already has a
  `"DrawSkyBackground"` fixture at line 88 — confirm this exact file name
  via `search_in_dir` before editing): add a test that calls the 6-argument
  `AddRenderPass()` overload BOTH with and without the new trailing
  argument, confirming the omitted-argument call defaults to
  `RenderPassDrawKind::DrawMesh` and the explicit-argument call stores
  exactly the value passed. Also add one for the 4-argument convenience
  overload.

### 3.8 Incremental compile check, completion report, commit

- `cmake --build build --target GreatTamanaEngine` — confirm zero errors.
  Do NOT build/run `GreatTamanaEngineTests` yet if your new tests haven't
  been wired into that target's source list — if they have (check
  `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`), a compile-only check of that
  target is fine too (`cmake --build build --target GreatTamanaEngineTests`),
  but do NOT run the resulting test binary/`ctest` yet (see PHASE0's
  Cross-Cutting Rules).
- Write `task_manager/render-pass-2/PHASE1_COMPLETION_REPORT.md` describing
  exactly what changed, any deviation from this plan, and the exact
  incremental compile command run + its result.
- `git add` + `git commit` the code changes and the report together.

## Definition of Done

- [ ] `rg::RenderPassDrawKind` exists in `RenderGraphTypes.h` with exactly
      three enumerators (`DrawMesh`/`DrawQuad`/`Blit`), plus a
      `default:`-free `ToString()` in `RenderGraphTypes.cpp`.
- [ ] `PassRecord::drawKind` exists, defaults to `DrawMesh`, appended at the
      end of the struct.
- [ ] `RenderGraphPassSnapshot::drawKind` exists, defaults to `DrawMesh`,
      and `BuildPassSnapshot()` copies it through unconditionally (surviving
      AND culled passes alike).
- [ ] Both `RenderGraphBuilder::AddRenderPass()` overloads accept a new,
      trailing, defaulted `RenderPassDrawKind` parameter — every
      pre-existing call site across `src/` compiles completely unmodified.
- [ ] `AddDrawSkyBackgroundPass()` explicitly tags its pass
      `RenderPassDrawKind::DrawQuad`.
- [ ] New Tier-1 tests cover `ToString()`, the snapshot copy-through
      (surviving + culled), and both `AddRenderPass()` overloads'
      default-vs-explicit `drawKind` behavior.
- [ ] `cmake --build build --target GreatTamanaEngine` succeeds with zero
      errors/warnings.
- [ ] `PHASE1_COMPLETION_REPORT.md` written and committed alongside the code.

## What We Will NOT Do

- We will NOT add a third `PassKind` enumerator, and we will NOT teach
  `RenderGraph::Execute()`/`RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`
  anything about `RenderPassDrawKind` at all — it is read by nothing there,
  ever, by design.
- We will NOT implement a real Blit/copy pass anywhere in this phase — the
  `Blit` enumerator stays permanently unused by any real pass until some
  future, separate campaign.
- We will NOT touch `src/Editor/FrameDebuggerData.cpp` or
  `src/Editor/Panels/FrameDebuggerPanel.cpp` in this phase — that is
  entirely PHASE2's job.
- We will NOT run a full build or `ctest` in this phase.
