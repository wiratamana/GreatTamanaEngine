# PHASE5 — Ordering safety net (`RenderPassEvent` consistency) and lifetime/thread-safety hardening

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Steps 6 and 7 (re-read both in full before starting).
Previous phase report to read first: `PHASE4_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

Two separate, real hazards get closed with ACTUAL CODE this phase (per this
campaign's own "no phase without a code change" rule):

1. **Ordering**: a Project Assembly's own callback (PHASE1/PHASE2) MUST tag
   any pass it declares with `rg::RenderPassEvent::AfterEverything` — the
   SAME tag every pass `RenderFeatureCompositor` itself declares
   (`DispatchOps()`/`DispatchBlend()`, confirmed at
   `RenderFeatureCompositor.cpp` lines 499/535). A new, permanent Tier-1
   regression test proves `DetectRenderPassEventContradictions()`
   (`RenderGraphCompiler.h`) reports zero findings for this exact new pass
   shape.
2. **Lifetime/thread-safety**: `RegisterProjectFeature()`/
   `UnregisterProjectFeature()` (PHASE2) must never run concurrently with
   `ContributeRenderGraphPasses()` — both are documented as main-thread-only
   in the design doc, but this phase adds a real, cheap, debug-build
   assertion that catches a future violation immediately and loudly, rather
   than relying on convention/comments alone.

## Step 2: The Situation (Where are we now?)

Re-confirmed fresh:

- `DetectRenderPassEventContradictions()` lives in
  `src/Renderer/RenderGraph/RenderGraphCompiler.h`/`.cpp`, is a pure,
  side-effect-free function taking a `CompiledGraphInput` (built via
  `RenderGraphBuilder`) and an "identity"/execution-order array, returning a
  `std::vector<RenderPassEventContradiction>`. It is already exercised
  DIRECTLY (never only through `Compile()`) by
  `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` (lines
  584-698+) — this is the exact fixture pattern this phase's own new test
  reuses.
- `RenderFeatureCompositor::DispatchOps()`/`DispatchBlend()` both already
  call `builder.AddRenderPass(..., rg::RenderPassDrawKind::DrawMesh,
  rg::RenderPassEvent::AfterEverything)` (confirmed, lines 499/535) — this
  compositor's own passes are ALREADY consistent with the rule this phase
  enforces on a Project Assembly's own callback; nothing about the
  compositor's OWN passes needs to change.
- PHASE2's `RegisterProjectFeature()`/`UnregisterProjectFeature()` currently
  have no thread-affinity check of any kind — this phase adds one.
- `RenderFeatureCompositor::EnsurePrivateTargetState()` (confirmed,
  `RenderFeatureCompositor.cpp` lines 408-423) allocates via
  `m_renderer.CreateRenderTexture()` — an ordinary, engine-owned GPU
  resource with no vtable/code compiled into any Project Assembly `.dll`'s
  own image. This means the private texture's own lifetime survives a
  hot-reload cycle unaffected; ONLY the `projectCallback` `std::function`
  itself (removed by PHASE4's teardown wiring) is dangerous. This phase
  writes this confirmation down explicitly (Step 3.3 below) rather than
  merely asserting it in prose — re-derive it yourself, live, from the real
  header/`.cpp`, do not just copy this paragraph into the report unverified.

## Step 3: The Plan (detailed strategy)

### 3.1 — Permanent regression test: a project-feature-shaped pass is never a contradiction

Add a new `TEST(RenderGraphCompilerTest, ...)` case to
`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`, placed near its
existing `DetectRenderPassEventContradictions()` coverage (after
`SameEventTierNeverProducesAContradiction`, before the `Compile()`-level
tests), reproducing the EXACT shape a Project Assembly's own hand-wired
callback produces (mirrors PHASE6's own live clear-color example, kept
consistent on purpose so a bug found in either place is provably the same
bug): a private-target WRITE tagged `RenderPassEvent::AfterEverything`,
followed by a READ of that same handle (standing in for the compositor's
own subsequent blend dispatch) ALSO tagged `RenderPassEvent::AfterEverything`:

```cpp
TEST(RenderGraphCompilerTest, ProjectRenderFeatureStyleAfterEverythingPassesNeverContradict)
{
    RenderGraphBuilder builder;
    const TextureHandle privateTarget = builder.CreateTexture("ProjectFeatureSlot0_Game_Private", MakeTextureDesc());

    builder.AddRenderPass(
        "MyProject.ScreenTint.Clear", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(privateTarget); },
        NoOpExecute, RenderPassDrawKind::DrawQuad, RenderPassEvent::AfterEverything); // index 0 - the project's own pass.
    builder.AddRenderPass(
        "ProjectFeatureSlot0_Game_Blend", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(privateTarget); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterEverything); // index 1 - stands in for DispatchBlend().

    CompiledGraphInput input = builder.Finish();
    const std::int32_t identity[] = { 0, 1 };

    const std::vector<RenderPassEventContradiction> contradictions =
        DetectRenderPassEventContradictions(input, identity);

    EXPECT_TRUE(contradictions.empty());
}
```

Adjust `PassKind`/`RenderPassDrawKind` arguments to whatever this file's own
existing helpers (`MakeTextureDesc()`, `NoOpExecute`) actually require —
re-read the surrounding existing tests first and match their exact call
shape rather than guessing.

### 3.2 — Debug-build main-thread-only assertion on the two new mutators

In `RenderFeatureCompositor.cpp`, at the very top of BOTH
`RegisterProjectFeature()` and `UnregisterProjectFeature()` (PHASE2's own
methods), add a cheap, debug-build-only thread-affinity check. Reuse an
EXISTING thread-affinity-check convention already in this codebase if one
exists (`search_in_dir` for something like `std::this_thread::get_id()` or a
"main thread ID" helper across `src/` before inventing a new one — mirror
whatever mechanism `HotReloadEngineStateMutex.h`/`ContributeRenderGraphPasses()`'s
own callers already rely on for "must be main thread" claims elsewhere, if a
reusable helper already exists there). If no reusable helper exists anywhere
in this codebase, add the SMALLEST possible one locally to
`RenderFeatureCompositor.h`/`.cpp` (e.g. capture
`std::this_thread::get_id()` once, lazily, the first time
`ContributeRenderGraphPasses()` OR either mutator runs, then `assert()` every
subsequent call from any of the three matches it) — `ask_questions` before
introducing a NEW, permanent, engine-wide thread-affinity utility if the
"smallest possible local one" would otherwise have to be duplicated in a
third place; a genuinely reusable utility might belong in a shared header
instead, but that is a bigger decision than this phase should make silently.

This assertion must be **debug-build only** (never a release-build crash
risk for something that has never yet been proven to happen in practice) and
must **never replace** PHASE0's own Locked Decision #2 (never do a full
build in a non-final phase) — verify it compiles via the SAME incremental
build this phase already needs, then confirm via a quick, deliberate,
throwaway local test (call one of the two mutators from a spawned
`std::thread` in a scratch/manual test, confirm the assertion fires, then
REMOVE that throwaway test before committing — it exists only to prove the
assertion mechanism itself works, it is not a permanent Tier-1 test, since
deliberately triggering an `assert()` inside a real Tier-1 suite would abort
the whole test binary).

### 3.3 — Live-lifetime confirmation, written down explicitly

Re-read `RenderFeatureCompositor::EnsurePrivateTargetState()` yourself, live,
right now, and write into `PHASE5_COMPLETION_REPORT.md` an explicit
confirmation (quoting the exact line(s)) that the private `RenderTexture` is
created via `m_renderer.CreateRenderTexture()` — an engine-owned resource
with zero code/vtable compiled into any Project Assembly `.dll`'s own image.
State explicitly: "only the `projectCallback` `std::function` itself is
dangerous across a hot-reload unload, and PHASE4's teardown wiring already
removes it before `FreeLibrary()` runs." Do not skip this because "it's
probably fine, PHASE0 already said so" — this exact class of unchecked
assumption is what made two separate, real ECS Registry bugs invisible until
a real hot-reload cycle was actually run (the design doc's own Step 6 cites
this precedent explicitly); re-derive it yourself from the CURRENT source,
not from this document's own paraphrase.

### 3.4 — What LIVE, HTTP-driven proof this phase defers to PHASE6

This phase's own thread-affinity assertion is a STATIC safety net, not a
substitute for the design doc's own required LIVE proof (Step 6's
"prove it live, don't just reason about it on paper" items): that
`RegisterProjectFeature()`/`UnregisterProjectFeature()` are never actually
called concurrently with `ContributeRenderGraphPasses()` across a REAL
hot-reload cycle, and that a slot reused across a register -> unregister ->
register (different name) sequence shows correct visual output from its very
first frame, with no stale prior-occupant content ever visible. Both of
these are explicitly PHASE6's job (Step 8's live items) — do not attempt them
in this phase; this phase only builds the STATIC safety nets (the
regression test in 3.1, the assertion in 3.2) those live proofs will run
against.

### 3.5 — Ambiguity checkpoints

  - If no existing main-thread-affinity helper/convention is found anywhere
    in this codebase, `ask_questions` about whether a small, new, local
    helper (scoped to this one class) is acceptable, versus something more
    broadly reusable belonging in a shared header instead.
  - If `RenderGraphCompilerTests.cpp`'s existing helper functions
    (`MakeTextureDesc()`, `NoOpExecute`) do not match the shapes assumed in
    3.1's sketch, adapt to what is actually there — do not invent new
    parallel helpers if existing ones already do the job.

### 3.6 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. `ctest -C Debug --output-on-failure -R RenderGraphCompilerTest` passes
   (targeted filter — includes the new test alongside all pre-existing ones
   in that same file, confirming zero regression to the existing detector).
3. Write `PHASE5_COMPLETION_REPORT.md`: the new regression test's result, the
   thread-affinity assertion's exact mechanism and where it lives, and the
   explicit, freshly-re-derived lifetime confirmation from 3.3.
4. `git_add` + `git_commit` covering the test file change, the
   `RenderFeatureCompositor.h`/`.cpp` assertion change, and the report.
