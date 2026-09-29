# PHASE1 COMPLETION REPORT — Fix the confirmed, reported bug: disabling "DrawSkyBackground" does not stop the sky from being drawn

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (unchanged, as required)
Status: **Complete.** Incremental build succeeded, targeted `ctest` filter (4/4
new tests) passed, and live, HTTP-driven verification confirmed the fix on a
real running `GreatTamanaEditor.exe`, twice (once before discovering an
unrelated engine crash mid-session, once again after relaunching a fresh
instance — both runs produced identical, correct results).

## The fix

### 1. New, generalized, reusable pure helper

`src/Renderer/RenderGraph/RenderPassToggleGuard.h` (new file, `namespace gte::rg`):

```cpp
inline bool ShouldDeclareBuiltInPassThisFrame(RenderPassToggleRegistry* registry, const char* name)
{
    return registry == nullptr || registry->NoteDeclaredAndCheckEnabled(name);
}
```

Deliberately smaller and more generic than
`AtmospherePassToggleLogic.h`'s `ShouldDeclareAtmospherePassThisFrame()` (no
upstream-handle-validity concept folded in) — exactly as PHASE0/PHASE1
specified, so PHASE2/PHASE3's systemic audit/fix can reuse this ONE helper
for every other `Core.cpp`/plugin-adapter provider found to have the same
side-channel-leak bug shape, without inventing a new one-off guard each time.

### 2. Applied to `"DrawSkyBackground"`'s own provider

`src/Core/Core.cpp` — at the very top of the `"DrawSkyBackground"` provider
lambda body (before `FindViewData()` or any other work):

```cpp
if (!rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "DrawSkyBackground")) {
    return;
}
```

This makes the WHOLE lambda body — including `recordSkyBackground`'s
construction and the `frame.blackboard.Publish<...>(kGameSkyBackgroundCallbackKey,
...)` call — never execute at all when disabled, for EITHER Game View or
Scene View invocation (`ProviderScope::PerActiveView` invokes this provider
once per active view, same frame; the guard runs independently, correctly,
at the top of each invocation).

`#include "../Renderer/RenderGraph/RenderPassToggleGuard.h"` was added to
`Core.cpp`'s existing Render Graph include block.

### 3. Downstream consumer (Step 3.3 of the phase file) — confirmed unchanged, no code needed

Re-read `Core.cpp`'s `gameSkyBackgroundCallbackForReplay`
(`blackboard.Fetch<...>(...).value_or(std::function<void(VkCommandBuffer)>{})`)
and `FrameDebuggerReplayPasses.cpp`'s `AddReplayPasses()` (`includeSkyStep =
static_cast<bool>(recordSkyBackground)`, `if (isSkyStep && recordSkyBackground)`
at the "sky step" execute lambda) — both already degrade gracefully to "no
sky step this capture" when the callback is empty, exactly as the phase file
predicted. **Zero code change was needed in `FrameDebuggerReplayPasses.cpp`**,
confirmed both by re-reading the code and by live testing (see below — with
`DrawSkyBackground` disabled and zero mesh entities in the demo scene,
`totalStepCount` dropped to `0` and the entire replay-step mechanism
correctly declared nothing at all, no crash).

### 4. Tier-1 test

`tests/Renderer/RenderGraph/RenderPassToggleGuardTests.cpp` (new file, 4
tests, registered in `tests/CMakeLists.txt`):

1. `NullRegistryAlwaysReturnsTrue`
2. `RealRegistryWithNameEnabledReturnsTrue`
3. `RealRegistryWithNameDisabledReturnsFalse`
4. `CallingTwiceSameFrameForSameNameReturnsSameAnswer` — explicitly proves the
   property that makes the `ProviderScope::PerActiveView` double-invocation
   fix correct (calling the guard twice, same frame, same name, never
   flip-flops the answer between the Game View and Scene View invocation).

All 4 pass:

```
ctest -C Debug --output-on-failure -R RenderPassToggleGuard
100% tests passed out of 4 (0.36 sec)
```

## Incremental build

`cmake --build build` succeeded (no full clean build — per Locked Decision
#2, reserved for PHASE7 only). Zero new warnings/errors from this phase's
own changes.

## Live, HTTP-driven verification (Step 3.4)

Performed on a real running `GreatTamanaEditor.exe` via
`run_app_background`/`gte_send_request`/`stop_app_background`.

1. **Baseline**: `GET /render_graph/passes` confirmed `"DrawSkyBackground"`
   enabled by default. `GET /get_texture?texture_name=GameView` (the real,
   live `GameView` render target — see note below on why this was used
   instead of `/get_game_view`) showed a real sky gradient (blue-to-orange
   horizon over black ground), 17109 bytes.
2. `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false` → `{"success":true}`.
3. `GET /render_graph` (the live `RenderGraphSnapshot`) confirmed **zero**
   `"DrawSkyBackground"` entries (both Game View and Scene View instances
   gone) — verified precisely via `(content -split 'DrawSkyBackground').Count == 1`
   (i.e. 0 occurrences), vs. `2` occurrences (Game View + Scene View) when
   enabled. `GET /get_texture?texture_name=GameView` dropped to 1582 bytes —
   a real, pixel-level, byte-size-confirmed disappearance of the sky (flat
   dark image, no gradient).
4. Reproduced the exact replay/paused-capture scenario using the real
   mechanism (no `/frame_debugger/step`/`/pause` endpoint exists, confirmed
   correct per the phase file's own corrected guidance):
   `GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true` →
   `GET /frame_debugger/capture` (`hasCapturedFrame: true`, `totalEventCount: 63`).
   `GET /list_textures` showed only **one** replay-step debug texture
   registered this session, `"FrameDebuggerReplayStep0"` (not
   `"FrameDebuggerReplayStep0Color"` — see Note 1 below), consistent with
   this demo scene's `objectCount == 0` (only a Camera + a hot-reload marker
   entity, no renderable mesh) — so index `0` is the dedicated sky step
   (`isSkyStep = includeSkyStep && (i == objectCount)` → `0 == 0` → true).
   - With `DrawSkyBackground` disabled: `GET /get_texture?texture_name=FrameDebuggerReplayStep0`
     returned **HTTP 504** — `"was never registered (or never rendered
     again) within the timeout"`. This is the CORRECT fixed behavior: with
     `recordSkyBackground` now empty (never published) AND `objectCount == 0`,
     `AddReplayPasses()`'s own `totalStepCount` is `0`, so it returns early
     before declaring ANY replay pass at all — the texture is never
     re-imported/re-rendered, hence the timeout. (Before this fix, the
     cached, unconditionally-published `recordSkyBackground` callback would
     have kept `includeSkyStep` true regardless of `DrawSkyBackground`'s own
     disabled state, still producing a sky-containing step 0.)
   - Re-enabled `DrawSkyBackground`, re-captured:
     `GET /get_texture?texture_name=FrameDebuggerReplayStep0` returned HTTP
     200, 17109 bytes, the same real sky gradient — a positive control
     proving the replay mechanism itself still works correctly when the
     pass is honestly enabled.
5. Re-enabled `"DrawSkyBackground"` a second time (final state) and
   re-confirmed sky returns in `GET /get_texture?texture_name=GameView`
   (17109 bytes, byte-identical to the original baseline).
6. `GET /get_logs?category=RenderPassHonesty` — checked repeatedly across the
   whole sequence, stayed **completely empty** (`"count":0`) throughout —
   the `editor-core-separation-21` detector never fired, confirming this fix
   does not touch the shape of bug that detector already catches, only the
   genuinely different Root-Cause-#3 side-channel leak.

All of the above was independently reproduced TWICE in this session (see
"Anomaly encountered" below for why) with identical results both times.

### Note 1 — texture naming discrepancy (pre-existing, out of scope)

The phase file's own Step 3.4 says to try
`GET /get_texture?texture_name=FrameDebuggerReplayStepNColor`. The actual
registered debug texture name (confirmed via `GET /list_textures`) is
`"FrameDebuggerReplayStepN"` — no `Color` suffix — even though
`FrameDebuggerReplayPasses.cpp`'s own `debugNameBuffer` literal is
`"FrameDebuggerReplayStep%zuColor"`. This is a pre-existing naming
difference between the `RenderTexture`'s own internal debug name and
whatever name the debug texture registry actually publishes it under
(a `RenderGraphDebugTextureRegistry`/import-name detail, not something this
phase's own fix touches). Not a bug introduced by this phase, not something
PHASE1's own scope covers — noted here plainly so PHASE2/PHASE3's audit (or
whoever writes the next `docs/conventions/*.md` covering this mechanism)
does not have to rediscover it. `GET /list_textures` is the reliable way to
find the actual name in any future session.

### Note 2 — `/get_game_view` observed returning a stale/unrelated image (out of scope, not a code change target of this phase)

During verification, `GET /get_game_view` (and the Game panel widget visible
in `GET /get_swapchain` screenshots) consistently returned a byte-identical
image (a blurred blue circular blob on a tan background, 13144 bytes) no
matter what render-graph state was toggled — including a control test where
`"RenderOpaque"` itself was disabled/re-enabled, which had zero visible
effect on it either. Meanwhile, `GET /get_texture?texture_name=GameView`
(the actual live `GameView` render target) correctly, immediately reflected
every toggle change made during this session, both for `"DrawSkyBackground"`
and the `"RenderOpaque"` control test. This strongly suggests the Game
panel's own on-screen ImGui image widget (and whatever `/get_game_view`
reads) is showing a different, likely debug/volume-preview texture, not the
real `GameView` composited output — this is NOT something PHASE1's own root
cause touches or was scoped to fix, so no code change was made for it. Per
PHASE0's Step 2.4 tool list, `GET /get_texture` was already an equally-valid,
explicitly-listed verification mechanism, so this phase used it as the
authoritative source for every pixel-level check above. This observation is
recorded here for whoever runs PHASE2's systemic audit or PHASE7's final
verification, in case it turns out to be a real, separate bug worth its own
investigation — it is explicitly NOT claimed as fixed or diagnosed by this
phase.

### Anomaly encountered — one engine crash mid-session (not attributable to this phase's own code change)

Partway through the first verification pass, after issuing several rapid
`GET /list_textures` / `GET /get_texture` calls, the running
`GreatTamanaEditor.exe` process (PID 14700) stopped responding entirely
(`gte_send_request` began returning a connection-refused error). `tasklist`
showed a zombie process at a DIFFERENT PID (5800) with `HandleCount: 0` and
zero threads — i.e. already fully dead, just not yet reaped by Windows
(`taskkill /F` on it returned "Access is denied", consistent with an
already-dying process). This zombie PID disappeared on its own moments
later. A fresh `GreatTamanaEditor.exe` instance was launched and the ENTIRE
verification sequence (Steps 1-6 above) was independently re-run from
scratch against it, producing IDENTICAL results to the first (aborted) run.
`GET /get_logs?since_id=0` on the fresh instance showed only ordinary
plugin-load/startup log entries — no error/crash-related log entries (as
expected; this in-process logger does not persist across a process crash).
This is recorded here in the interest of brutal honesty, not swept under
the rug — but it is NOT being reported as a bug against any MCP tool
(`run_app_background`/`gte_send_request`/`stop_app_background` all behaved
correctly throughout: they returned exactly the real state of the OS
process/HTTP server at every call), and this phase's own fix is a minimal,
single early-`return` guard with no plausible mechanism for causing a
crash on a different, unrelated code path (`/list_textures`'s own texture
enumeration, unrelated to `DrawSkyBackground`/`RenderPassToggleGuard.h`
entirely). No further action was taken beyond re-verifying cleanly on a
fresh instance; this is flagged here as a fact for later phases (especially
PHASE7's full regression pass) to be aware of, not silently hidden.

## Ambiguity checkpoint (Step 3.5)

No further, undiagnosed live-path was discovered beyond what PHASE0's own
static trace already found. `"DrawSkyBackground"`'s disabled state was
confirmed, live, to suppress the sky in BOTH the normal (non-replay) live
`GameView` render target AND the Frame Debugger's own replay-step capture
preview, via the single fix in `Core.cpp`. `ask_questions` was not invoked —
no genuine ambiguity requiring a user/orchestrator decision was found; the
two "Notes" above are pre-existing, out-of-scope observations, not decision
points this phase needed to resolve.

## Files changed

- `src/Renderer/RenderGraph/RenderPassToggleGuard.h` (new)
- `src/Core/Core.cpp` (new `#include`; early-guard added to the
  `"DrawSkyBackground"` provider lambda)
- `tests/Renderer/RenderGraph/RenderPassToggleGuardTests.cpp` (new, 4 tests)
- `tests/CMakeLists.txt` (new test source registration)
- `task_manager/editor-core-separation-22/PHASE1_COMPLETION_REPORT.md` (this file)

## End of phase checklist (Step 3.6)

1. ✅ Incremental build (`cmake --build build`) succeeded.
2. ✅ Targeted `ctest -C Debug --output-on-failure -R RenderPassToggleGuard` —
   4/4 tests passed (never the full suite — reserved for PHASE7).
3. ✅ This completion report written.
4. Next: `git_add` + `git_commit` covering the new header, the test file,
   the `Core.cpp` change, `tests/CMakeLists.txt`, and this report.
