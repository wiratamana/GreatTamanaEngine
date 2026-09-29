# PHASE3 — Regression Tests, Full Build/Test, and Final Live Verification

Parent: `PHASE0_MASTER_STRATEGY.md` — **read it first.**
Also read `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md`'s and `PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md`'s own completion reports before starting — both phases must already be landed and committed.

This is an **implementation task**. Do not use `delegate_task`. If a test fails and the fix is non-obvious, or you find a genuine design ambiguity, use `ask_questions` to ask the user directly, in plain, easy English, before improvising a fix. If a test fails here and needs non-trivial rework, that rework should itself become a fresh `delegate_task` from the ORCHESTRATING session that called you, not something this implementation task spawns itself.

---

## Step 1: The Goal

1. Add real, permanent Tier-1 regression coverage for the two fixes so nobody silently re-breaks them later.
2. Run the full build + full `ctest` regression suite for the first time this whole campaign (every prior phase deliberately skipped this — PHASE0 LDD-8).
3. Re-run every one of PHASE0's Success Criteria end-to-end, live, against the fully-rebuilt engine, and record the final result.
4. Write the campaign's `CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation

- PHASE1 added a new deny-listed pass (`"ClearViewTarget"`) and changed `RenderPassToggleRegistry::IsDenyListed()`.
- PHASE2 threaded a toggle-registry pointer through 5 Atmosphere pass-adding methods plus 2 wrapper functions plus 4 `Core.cpp` call sites, with cascading `IsValid()`-based skip logic.
- PHASE1 has not had its own dedicated Tier-1 test added yet. PHASE2 already added and locally verified its own new, dedicated Tier-1 test file (see below). Neither phase has been checked against the FULL existing test suite (`GreatTamanaEngineTests`) yet — only incremental compiles + live HTTP smoke tests so far.
- `Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp` (Tier-1, existing) is the natural home for a new `IsDenyListed` test. `Renderer/RenderGraph/RenderPipelineTests.cpp` (Tier-1, existing) is the natural home for confirming `RenderPassEvent::BeforeEverything` sorts correctly relative to other events (if not already covered — check first).
- `AtmosphereLutRenderer`'s own 5 changed methods are Tier-2 (need a live `VkDevice`/`Renderer`, per `tests/CMakeLists.txt`'s own documented Tier-1/Tier-2 split, `AGENTS.md`'s "Testability & Regression Safety" section) — they cannot get a direct unit test. PHASE2 extracts the one genuinely pure slice of their own guard logic — "is this pass enabled this frame AND are all of its upstream handles valid this frame" — into a small, standalone, Tier-1-testable helper, `gte::ShouldDeclareAtmospherePassThisFrame()` (`src/Renderer/Atmosphere/AtmospherePassToggleLogic.h`), with its own dedicated test file (`tests/Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp`) added and locally verified as part of PHASE2 itself — this is genuinely NEW branching logic with its own real test, not merely a new call site of `RenderPassToggleRegistry`'s own already-tested public API. This phase's own job for that one test file is only to confirm it is included in, and passes as part of, the FULL suite run in 3.3 below — never to re-add it. Live, HTTP-driven verification (already done once per-phase, repeated here at full scale) remains the correct and sufficient proof for the actually-Tier-2 half of this fix (the GPU-touching bodies of the 5 `AddXxxLutPass()` methods themselves), exactly as `AGENTS.md`'s own testability section states should be expected for `Renderer/Vulkan/`-adjacent code.

## Step 3: The Plan

### 3.1 — New/updated Tier-1 tests

Open `tests/Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp`. Add a new test case confirming the PHASE1 deny-list change:

```cpp
TEST(RenderPassToggleRegistryTest, SetEnabledFalseOnClearViewTargetIsRefusedByTheDenyList)
{
    RenderPassToggleRegistry registry;
    const bool applied = registry.SetEnabled("ClearViewTarget", false);
    EXPECT_FALSE(applied);
    EXPECT_TRUE(registry.IsEnabled("ClearViewTarget"));
}
```

(Mirror the exact style/naming of the existing, adjacent `SetEnabledFalseOnPresentIsRefusedByTheDenyList` test in the same file — read it first, copy its shape precisely rather than inventing a new style.)

Open `tests/Renderer/RenderGraph/RenderPipelineTests.cpp`. Confirm whether a test already exists proving `RenderPassEvent::BeforeEverything` sorts strictly before `RenderPassEvent::Opaques` inside `CollectedPassesAreSortedByOrderRegardlessOfRegistrationOrProviderScope` (search for `BeforeEverything` in that file first). If `BeforeEverything` is not yet one of the values that existing parameterized/enumerated test already exercises, extend that same test (do not create a whole new, separately-named test if the existing one is a natural, minimal-diff place to add one more sample point) to include a pass declared with `RenderPassEvent::BeforeEverything` and confirm it always sorts first regardless of registration order — this is a real, load-bearing property PHASE1's fix depends on (LDD in PHASE1, section 3.2's own comment: *"RenderPassEvent::BeforeEverything makes... this pass... before every other pass... regardless of provider registration order - this is genuinely load-bearing here"*).

### 3.2 — Full clean build

```
cmake --build build
```
(This IS the first full/clean-equivalent build this campaign runs — if `build/` already has fresh incremental artifacts from PHASE1/PHASE2, this is likely fast; that's fine, "full" here means "build the whole target graph, not just the files you touched," which `cmake --build build` with no target filter already does.) Fix any compile errors.

### 3.3 — Full `ctest` regression run

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Compare the pass/fail/skip counts against the last documented baseline in `AGENTS.md`'s own running history (search that file for the most recent `"...ctest regression pass (N tests, 100% passing..."` sentence to find the exact prior baseline number before this campaign). Expect the total test count to have grown by exactly the number of new test cases added in 3.1 above, with 100% of executed tests passing (same legitimate environment-gated skip count as the prior baseline, never fewer, never more skips unless you can explain exactly why).

**If anything newly fails**: diagnose it fully first (read the exact failing assertion, the exact file/line, and reason about whether PHASE1 or PHASE2's change is the actual cause) — do not guess-fix. If the fix is small and clearly scoped to this campaign's own changed files, fix it directly in this phase and re-run `ctest` to confirm green. If the failure reveals a deeper, unrelated pre-existing issue or a genuinely large rework is needed, stop and report it precisely (exact test name, exact failure text) rather than attempting a large unplanned change inside this already-implementation-only task.

### 3.4 — Final, full live re-verification of every PHASE0 Success Criterion

Launch the freshly-rebuilt engine (`run_app_background`), confirm liveness via `/get_logs`, then run through **every one** of PHASE0's 5 Success Criteria, in order, capturing the actual `gte_send_request` result for each:

1. Disable `RenderOpaque`+`DrawSkyBackground`+`RenderTransparent`+`AtmosphereComposite` (+ every Atmosphere pass, for completeness) → `GET /get_game_view` shows solid `kGameClearColor`, never magenta — AND `GET /render_graph` shows `"ClearViewTarget"` with `"is_culled": false` in this exact scenario (PHASE0's Success Criterion #1/LDD-10 — disabling `AtmosphereComposite` too is what actually exercises this check, since it is otherwise the one pass that would keep the view target reachable on its own).
2. Disable ONLY `RenderOpaque` (leave `DrawSkyBackground` + the 5 Atmosphere passes enabled) → `GET /get_game_view` shows a real, visible sky/atmosphere.
3. Disable `AtmosphereTransmittanceLutPass` alone → confirm via `GET /render_graph` that it (and its cascading downstream passes) are genuinely gone/zeroed this frame.
4. Re-enable everything → confirm `GET /get_game_view` matches the very first baseline screenshot taken back in PHASE1 step 3.7.2 (no lasting regression from either fix).
5. Confirm the `ctest` result from 3.3 above was 100% passing (already satisfied if you reached this step).

Stop the engine (`stop_app_background`) once all 5 are confirmed.

### 3.5 — Campaign completion report

Write `CAMPAIGN_COMPLETION_REPORT.md` in `task_manager/editor-core-separation-20/` — a single, final document (not a per-phase diff log) covering:
- The two confirmed root causes (summarized from PHASE0), in plain language.
- Exactly what changed, file by file, across PHASE1+PHASE2.
- The full-build + full-`ctest` result (exact pass count, up from the prior documented baseline, and the delta explained).
- The final live-verification result for each of the 5 Success Criteria.
- Anything explicitly left out of scope (LDD-5, LDD-6, LDD-7 from PHASE0) restated so a future reader doesn't assume they were silently fixed too.

### 3.6 — Update `AGENTS.md`

Add a short new dated entry (or extend the existing "Render Pass System"/"Atmosphere Scattering" section, whichever is the more natural fit — read both sections first to decide) documenting this campaign's own fix in the same terse, precise style every other campaign entry in that file already uses, ending with the updated `ctest` pass count, so the NEXT campaign's own AGENTS.md read (which every future task in this repository starts with) has an accurate, current picture of the render-graph's guaranteed-clear guarantee and the Atmosphere toggle chain's real (not fake) per-pass control.

### 3.7 — Final commit

`git_add` everything (tests, `AGENTS.md`, both new report files), `git_commit` with a message summarizing the whole campaign, e.g. `"editor-core-separation-20 PHASE3: regression tests, full build/ctest, campaign closeout - pink screen root-caused and fixed"`.
