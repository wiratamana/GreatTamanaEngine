# PHASE1 — Fix the confirmed, reported bug: disabling "DrawSkyBackground" does not stop the sky from being drawn

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first — this phase does
not repeat that file's Step 2.3 root-cause trace, only summarizes it below).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: none — this is the first implementation
phase. Read `task_manager/editor-core-separation-21/CAMPAIGN_COMPLETION_REPORT.md`
instead (required background, per PHASE0).

## Step 1: The Goal (Where are we going?)

Disabling `"DrawSkyBackground"` via the "Render Graph" panel checkbox (or
`GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false`)
must make the sky genuinely, visibly disappear from:
1. The live Game View (`GET /get_game_view`), during ordinary (non-paused,
   non-stepping) play.
2. A Frame Debugger capture taken while the Editor is paused, replaying
   through its own N replay-step passes (`GET /frame_debugger/enable?value=true`
   + `/capture` — see Step 3.4 below for exactly why this is the correct,
   real trigger, and why no separate HTTP "step" endpoint exists or is
   needed) — the EXACT scenario the user's own third screenshot shows.

Both must hold with zero crash, and the Game View must still render a sane
image (matching every prior campaign's own "no magenta" bar).

## Step 2: The Situation (Where are we now?)

Re-read `PHASE0_MASTER_STRATEGY.md`'s Step 2.3 in full — the mechanical
root cause is already traced there with exact file/line citations. Summary:

- `src/Core/Core.cpp`'s `"DrawSkyBackground"` provider (~line 824-874)
  unconditionally builds `recordSkyBackground` and (for Game View)
  `Publish()`es it to the blackboard under `kGameSkyBackgroundCallbackKey`
  (line 847-849) — BEFORE any toggle check. The generic toggle gate
  (`RenderPipeline::DeclareOnePhase()`, `RenderPipeline.h` ~line 588-591)
  only prevents the resulting `RenderPassDesc` from reaching
  `builder.AddRenderPass()` — it has no way to "undo" a side effect the
  provider lambda already performed before returning.
- `Core.cpp` (~line 1211-1233) unconditionally `Fetch()`es this cached
  callback and, whenever `m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()`
  is true, forwards it into `FrameDebuggerCaptureContext::AddReplayPasses()`
  (`src/Editor/FrameDebuggerReplayPasses.cpp` ~line 88-216), which checks
  only its OWN, unrelated `"FrameDebuggerReplay"` toggle (line 102) and then
  unconditionally calls `recordSkyBackground(ctx.cmd)` on its own dedicated
  "sky step" (line 206-208) — reproducing the sky regardless of
  `"DrawSkyBackground"`'s own disabled state.
- The correct, already-proven-in-this-codebase FIX SHAPE is
  `src/Renderer/Atmosphere/AtmospherePassToggleLogic.h`'s
  `ShouldDeclareAtmospherePassThisFrame()` pattern (`editor-core-separation-20`
  campaign, PHASE2): a pure, early, check-then-return guard evaluated BEFORE
  any side effect, inside the provider itself — not only at the generic,
  late, post-hoc gate.

### 2.1 — Why a NEW, generalized helper (not a copy-paste of the Atmosphere one)

`ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, allUpstreamHandlesValid)`
is deliberately Atmosphere-specific (it also folds in an upstream-handle-
validity check that has no meaning for `"DrawSkyBackground"`). This phase
introduces a smaller, genuinely generic sibling with NO atmosphere-specific
concept baked in, so PHASE2/PHASE3 (the systemic audit/fix) can reuse it for
every OTHER `Core.cpp`/plugin-adapter provider found to have the same bug
shape, without inventing a new one-off helper each time.

## Step 3: The Plan (detailed strategy)

### 3.1 — New pure helper

Add a new header, `src/Renderer/RenderGraph/RenderPassToggleGuard.h`
(`namespace gte::rg`), containing a single, trivial, fully-documented,
inline pure function:

```cpp
// Returns true if a `RenderPipeline` provider should proceed declaring its
// pass THIS FRAME, INCLUDING any side effect it performs before returning
// (a RenderPassBlackboard::Publish() call, a cached-callback hand-off, a
// member-variable write another system later reads) - not merely whether
// its own RenderPassDesc should reach the builder. MUST be called at the
// very TOP of any provider body that performs ANY such side effect, BEFORE
// that side effect happens - calling it only immediately before
// `out.push_back(desc)` (mirroring RenderPipeline::DeclareOnePhase()'s own
// late gate) defeats its entire purpose. See
// task_manager/editor-core-separation-22/PHASE1_FIX_DRAWSKYBACKGROUND_TOGGLE_SIDE_CHANNEL_LEAK.md
// for the confirmed bug this exists to prevent a recurrence of.
//
// `registry` may be nullptr (mirrors every other toggle-registry consumer
// in this codebase's own "unset = old, always-enabled behavior" rule) -
// returns true unconditionally in that case. `name` must be the EXACT same
// literal this provider's own RenderPassDesc::debugName uses, so this
// early check and RenderPipeline::DeclareOnePhase()'s own later,
// idempotent re-check of the identical name agree.
inline bool ShouldDeclareBuiltInPassThisFrame(RenderPassToggleRegistry* registry, const char* name)
{
    return registry == nullptr || registry->NoteDeclaredAndCheckEnabled(name);
}
```

Add a Tier-1 test file, `tests/Renderer/RenderGraph/RenderPassToggleGuardTests.cpp`
(mirrors `AtmospherePassToggleLogicTests.cpp`'s/`RenderPassToggleChangeDetectionLogicTests.cpp`'s
own shape): covers (a) `nullptr` registry always returns true, (b) a real
registry with the name enabled returns true, (c) a real registry with the
name disabled returns false, (d) calling it twice in the same frame for the
same name (mirroring the PerActiveView double-invocation shape) returns the
SAME answer both times (this is the exact property that makes the fix in
3.2 below correct — confirm it explicitly, do not assume it).

### 3.2 — Apply it to `"DrawSkyBackground"`'s own provider

In `src/Core/Core.cpp`, at the very top of the `"DrawSkyBackground"`
provider lambda (~line 824-825, immediately after entering the lambda body,
BEFORE `FindViewData()` is even called — the earlier the better, so no
per-view work of any kind happens for a disabled pass):

```cpp
if (!rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "DrawSkyBackground")) {
    return;
}
```

This makes the WHOLE lambda body — including the `recordSkyBackground`
construction and the `frame.blackboard.Publish<...>(kGameSkyBackgroundCallbackKey,
...)` call — never execute at all when disabled, for EITHER view (since the
provider is invoked once per view, and this guard runs at the very top of
EACH invocation). `#include "../Renderer/RenderGraph/RenderPassToggleGuard.h"`
at the top of `Core.cpp` alongside its other Render Graph includes.

**Important correctness note, confirm this explicitly during live
verification, do not assume it**: `RenderPipeline::DeclareOnePhase()`'s own
generic, LATE gate (`RenderPipeline.h` ~line 588-591) will still run
afterward, for whatever `RenderPassDesc` entries THIS provider (and every
other provider) actually pushed this frame. Since a disabled
`"DrawSkyBackground"` invocation now pushes NOTHING into `out` at all (the
early `return` above happens before `out.push_back(std::move(desc))` is
ever reached), the late gate simply never sees a `"DrawSkyBackground"`
desc entry to re-check that frame — no double-decrement, no conflicting
state, this mirrors exactly how `AtmosphereLutRenderer`'s five methods and
`PluginRenderPassBuilderAdapter::AddFullscreenClearPass()` already behave
today.

### 3.3 — Confirm the downstream consumer degrades gracefully (should already be true — verify, do not assume)

`Core.cpp`'s own `gameSkyBackgroundCallbackForReplay` (~line 1211-1212) uses
`blackboard.Fetch<...>(kGameSkyBackgroundCallbackKey)` returning
`std::optional<...>`, and the replay call site (~line 1224-1225) already
does `.value_or(std::function<void(VkCommandBuffer)>{})` — an empty
`std::function` — before forwarding it into `AddReplayPasses()`.
`AddReplayPasses()`'s own `includeSkyStep = static_cast<bool>(recordSkyBackground)`
(`FrameDebuggerReplayPasses.cpp` ~line 114) already correctly treats an
empty callback as "no sky step this capture" (`totalStepCount` simply
excludes it — see `frame-debugger-8` campaign's own PHASE2 design this
mirrors). Read this whole path once more after the PHASE1 fix lands and
confirm no null-callable crash risk was introduced or already existed
(`if (isSkyStep && recordSkyBackground)` at line 206 is already a safe,
double-checked guard) — this should require ZERO code change in
`FrameDebuggerReplayPasses.cpp` itself; if live testing proves otherwise,
`ask_questions` before changing that file's own behavior, since it is a
shared mechanism PHASE2/PHASE3 also depend on staying stable.

### 3.4 — Live, HTTP-driven verification (mandatory, not optional)

Using `run_app_background`/`gte_send_request`/`stop_app_background`:
1. Launch the Editor, confirm `"DrawSkyBackground"` is enabled by default
   (`GET /render_graph/passes`), capture a baseline `GET /get_game_view`
   screenshot showing sky.
2. `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false`.
3. Confirm `GET /render_graph/passes` now shows zero `"DrawSkyBackground"`
   entries in the live snapshot (both Game View and Scene View instances
   gone) and `GET /get_game_view` no longer shows sky (compare against the
   baseline — a real pixel-level difference, not just "looks similar").
4. Reproduce the SPECIFIC screenshot scenario using the REAL mechanism that
   arms replay-pass declaration — **there is no dedicated HTTP "pause" or
   "step" endpoint anywhere in this engine** (`ctx.playbackPaused`/
   `stepOneFrameRequested`, `src/Editor/PlaybackControls.cpp`, are mutated
   ONLY by a mouse click on the "Pause"/"Step" toolbar buttons — confirmed by
   a full `search_in_dir` of `src/Network/`; do not go looking for a
   `/frame_debugger/step` or `/pause` route, neither exists, and do not spend
   time inventing one). Instead: `GET /frame_debugger/enable?value=true`
   BOTH auto-engages Pause (`FrameDebuggerPanel::ApplyEnabledEdge()` sets
   `ctx.playbackPaused = true` on the disabled->enabled edge,
   `src/Editor/Panels/FrameDebuggerPanel.cpp`) AND arms a deferred capture
   trigger that declares this frame's N replay passes
   (`FrameDebuggerCaptureContext::AddReplayPasses()`) on the very next engine
   frame — this alone is sufficient to reach the exact code path the bug
   lives in; no literal "Step" click is structurally required. Follow with
   `GET /frame_debugger/capture` (arms one more explicit capture) then
   `GET /frame_debugger/state` and confirm no sky pixels appear in the
   captured replay-step preview texture either
   (`GET /get_texture?texture_name=FrameDebuggerReplayStepNColor` for
   whichever `N` is the sky step — the query parameter is `texture_name`,
   NOT `name`, per `NetworkRoutes.h`'s `ParseGetTextureQuery()` — using
   `name` instead returns a 400).
5. Re-enable `"DrawSkyBackground"` and confirm sky returns in both places.
6. `GET /get_logs?category=RenderPassHonesty` — confirm it stays completely
   empty throughout (the existing `editor-core-separation-21` detector
   should never fire — this fix does not touch the shape of bug that
   detector already catches, only a genuinely different one).

### 3.5 — Ambiguity checkpoint

If, during live verification, step 3.4's item 4 reveals `"DrawSkyBackground"`'s
disabled state ALSO needs to suppress the sky during a NORMAL (non-replay,
non-paused) live frame through some OTHER path this phase's static trace
did not find, use `ask_questions` before expanding this phase's scope — do
not silently widen a "fix the confirmed side channel" phase into a second,
undiagnosed investigation; that belongs in PHASE2's systemic audit instead,
with its own evidence trail.

### 3.6 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. Tier-1 tests for `RenderPassToggleGuardTests.cpp` pass
   (`ctest -C Debug --output-on-failure -R RenderPassToggleGuard`, a
   TARGETED filter — not the full suite, per PHASE0's Locked Decision #2).
3. Write `PHASE1_COMPLETION_REPORT.md` in this same folder: the confirmed
   fix, the exact live-verification evidence from 3.4 (screenshots/byte
   sizes/JSON snippets), and any ambiguity surfaced via `ask_questions` and
   how it was resolved.
4. `git_add` + `git_commit` covering the new header, the test file, the
   `Core.cpp` change, and the report.
