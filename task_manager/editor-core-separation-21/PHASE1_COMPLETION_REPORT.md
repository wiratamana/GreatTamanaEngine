# PHASE1 COMPLETION REPORT — Live Reproduction & Root Cause Diagnosis of the `AtmosphereAerialPerspectiveCompositePass` Lie

Campaign: `editor-core-separation-21` ("The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign)
Phase: PHASE1 (`PHASE1_LIVE_REPRODUCTION_AND_ROOT_CAUSE_DIAGNOSIS.md`)
Branch: `feature/editor-core-separation` (unchanged, as required)

---

## Summary of the finding (read this first)

**The render-pass toggle mechanism itself is completely honest and correct.**
Mechanically confirmed, with log evidence, that:

1. `RenderPassToggleRegistry::SetEnabled()` (driven by `GET /render_graph/set_pass_enabled`)
   applies instantly and correctly.
2. `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`'s own declare-time
   guard (`ShouldDeclareAtmospherePassThisFrame()`) correctly stops declaring the
   pass into the Render Graph on the **very next real engine frame** after the
   toggle — confirmed for ~100+ consecutive frames with `willDeclare=false`.
3. The Frame Debugger's own tree-builder (`BuildRealFrameDebuggerSnapshot()`)
   correctly excludes the pass's leaf + its "Compute Dispatch" child (2 fewer
   `totalEventCount`) whenever it is handed a graph snapshot that genuinely
   lacks the pass.

**The actual, confirmed root cause is a genuine, reproducible staleness bug in
`FrameDebuggerPanel::CaptureNowFromCommand()`** — the handler behind
`GET /frame_debugger/capture` (the HTTP entry point every scripted/HTTP-driven
verification, and the campaign's own reported screenshot, ultimately depends
on):

```cpp
bool FrameDebuggerPanel::CaptureNowFromCommand()
{
    if (!m_enabled) {
        return false;
    }
    m_pendingCaptureTrigger = true;   // <-- fire-and-forget
    return true;                      // <-- returns BEFORE the real capture runs
}
```

This only **arms** a deferred capture trigger. The real capture
(`FrameDebuggerPanel::TriggerCapture()`, which reads
`RenderGraph::LastSnapshot()` and rebuilds the whole event tree from it) does
not run until the **next** real engine frame, from the top of
`FrameDebuggerPanel::Build()` (`task_manager/frame-debugger-7` campaign,
PHASE3's deliberate "deferred capture" design — see `AGENTS.md`'s "Frame
Debugger" section). `CaptureNowFromCommand()` never waits for that deferred
capture to actually finish before its own HTTP response is built — its JSON
response therefore **always reports whatever the previous, already-completed
capture produced**, i.e. it is always potentially one full capture cycle
stale relative to any render-pass-toggle mutation that was just applied.

Consequence: **the very first `GET /frame_debugger/capture` call issued after
toggling a pass off will always show that pass as if it were still running**
— with real, populated GPU-timing/read/write data — **no matter how long the
caller waits beforehand**. Only a **second** subsequent capture (or any other
state-reading request issued after the first capture's own deferred
completion) correctly reflects the toggle. This reproduces the reported
screenshot's exact symptom (a real, fully-populated, GPU-timed leaf for a pass
whose checkbox is unticked) without any bug at all in the toggle registry,
the declare-time guard, or the tree-builder.

This maps to **PHASE0_MASTER_STRATEGY.md Step 2.1's third hypothesis**
("A stale/frozen Frame Debugger capture") — **CONFIRMED**, refined with an
exact mechanism: it is not merely "the user looked at an old capture without
re-triggering" (though that is also true and independently sufficient to
reproduce the symptom in the interactive UI — see "Two independent
contributing layers" below) — it is a genuine, structural one-response-cycle
staleness baked into `CaptureNowFromCommand()`'s fire-and-forget contract,
which is separately, deterministically reproducible over pure HTTP.

None of PHASE0 Step 2.1's other four hypotheses (a wiring regression in the
toggle chain, an `ImGuiUniqueId` collision on the checkbox, the two-name
outer/inner registry-key confusion, or a bug inside
`RenderGraphCompiler::Compile()`/`FrameDebuggerData.cpp`'s own tree-building
logic) are the cause — each was explicitly ruled out by direct log evidence
(see "Reproduction Log" below).

---

## Two independent contributing layers (both real, both worth PHASE2 seeing)

- **(A) Structural / by design**: The Frame Debugger holds exactly **one**
  frozen captured frame (`FrameDebuggerCurrentCapture`, `frame-debugger-7`
  campaign) that only updates on an explicit re-trigger (Enable-edge / Step /
  the "Capture" button / `GET /frame_debugger/capture`). If a human unticks a
  pass in the "Render Graph" panel and then simply **looks at** an
  already-open Frame Debugger window without clicking "Capture"/"Step" again,
  they will correctly see the OLD, still-populated leaf — by design, this is
  what a frozen frame-capture tool is supposed to do. This alone is a
  plausible, complete explanation for the original screenshot if the user
  never re-captured after toggling.
- **(B) Mechanical / HTTP-specific**: Even when an explicit new capture **is**
  requested, `CaptureNowFromCommand()`'s own response never waits for that
  capture to actually complete (see root-cause section above) — so a
  scripted/HTTP verification workflow of "toggle off, capture once, inspect
  response" will **always** see the stale result on its first capture,
  structurally, regardless of any wait beforehand.

Both layers are real. PHASE2 needs to decide (or `ask_questions` the user
about) which remedy or combination of remedies best serves the iron rule -
e.g. making `GET /frame_debugger/capture` block until the deferred capture
genuinely completes (closing layer B), and/or auto-invalidating/dimming the
currently-frozen capture the instant `RenderPassToggleRegistry` state changes
underneath it so a stale capture is never silently mistaken for a live one
(addressing layer A). This report intentionally does not decide the fix —
PHASE1 is diagnosis-only, per its own Step 1.

---

## Reproduction Log (verbatim, real requests/responses against a live `GreatTamanaEditor.exe`)

Environment: `build\GreatTamanaEditor.exe`, launched via `run_app_background`,
driven purely over `gte_send_request` (`http://127.0.0.1:8080`), per Step 3.1.

### 3.1 — Baseline (pass enabled, default state)

```
GET /frame_debugger/open
  -> {"state":{...,"enabled":false,"hasCapturedFrame":false,...,"totalEventCount":0,...},"success":true}
GET /frame_debugger/enable?value=true
  -> {"state":{...,"enabled":true,"hasCapturedFrame":false,...,"totalEventCount":0,...},"success":true}
GET /render_graph/passes
  -> {"passes":[{"enabled":true,"ever_declared_this_session":true,"name":"AtmosphereAerialPerspectiveCompositePass"}, ...]}
GET /frame_debugger/capture
  -> {"state":{...,"hasCapturedFrame":true,...,"totalEventCount":63,...},"success":true}
```
Baseline confirmed: pass enabled, tree has 63 total events.

### 3.1 continued — toggle off, wait, capture (the exact reported repro)

```
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false
  -> {"success":true}
GET /render_graph/passes
  -> {"passes":[{"enabled":false, ..., "name":"AtmosphereAerialPerspectiveCompositePass"}, ...]}   <- mutation confirmed instant
(waited 3 real seconds)
GET /frame_debugger/capture
  -> {"state":{..., "totalEventCount":63, ...},"success":true}     <- STILL 63 — "the lie", reproduced
```

### 3.2/3.3 — Instrumentation added (category `"RenderPassHonestyDiag"`), rebuilt, repeated cleanly

Temporary `GTE_LOG_DEBUG("RenderPassHonestyDiag", ...)` calls added at:
- `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` / `SetEnabled()`
  (`src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp`) — scoped to the
  two names this phase diagnoses (`"AtmosphereAerialPerspectiveCompositePass"`,
  `"AtmosphereComposite"`) to avoid flooding the 2000-entry ring buffer.
- `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`
  (`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`) — logs
  `passEnabledThisFrame`/`aerialPerspectiveVolumeHandle.IsValid()`/`willDeclare`
  unconditionally, every call.
- `Core.cpp`'s `"AtmosphereComposite"` provider lambda (`src/Core/Core.cpp`) —
  logs the outer `NoteDeclaredAndCheckEnabled("AtmosphereComposite")` guard
  result, and `composited.IsValid()` after calling
  `AddAtmosphereCompositePass()`.
- `FrameDebuggerPanel::TriggerCapture()`
  (`src/Editor/Panels/FrameDebuggerPanel.cpp`) — logs
  `graphSnapshot.passesInExecutionOrder.size()` plus whether
  `"AtmosphereAerialPerspectiveCompositePass"` is present/culled in the EXACT
  `RenderGraphSnapshot` this specific capture call used to build its tree.

Clean, strictly-sequential re-run (every request issued one at a time, never
batched, to avoid an HTTP-thread race against `POST /clear_logs` — an earlier,
batched attempt during this same investigation produced a false negative for
this exact reason, see "Notes on a mistake" below):

```
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=true
  -> {"success":true}
GET /frame_debugger/capture   -> totalEventCount:61   (still stale from the prior disabled state)
GET /frame_debugger/capture   -> totalEventCount:63   (settled - pass re-enabled, tree correct)

POST /clear_logs -> {"cleared_count":2000,"success":true}
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false
  -> {"success":true}
GET /frame_debugger/capture   -> {"state":{...,"totalEventCount":63,...}}     <- STALE (first capture after toggle)

GET /get_logs?keyword=AerialPerspectiveCompositePass&limit=20
  -> {"category":"RenderPassHonestyDiag","frame":24739,
      "message":"NoteDeclaredAndCheckEnabled(\"AtmosphereAerialPerspectiveCompositePass\") -> enabled=false", ...}
  -> {"category":"RenderPassHonestyDiag","frame":24739,
      "message":"AddAerialPerspectiveCompositePass() - toggleRegistry=non-null, passEnabledThisFrame=false,
                  aerialPerspectiveVolumeHandle.IsValid()=true, willDeclare=false", ...}
  (identical willDeclare=false repeated every frame from 24739 through 24742+, for BOTH Game View and Scene View
   declarations - i.e. the render graph had ALREADY, correctly, stopped declaring this pass for many consecutive
   frames BEFORE and DURING the still-stale /frame_debugger/capture response above)

GET /frame_debugger/capture   -> {"state":{...,"totalEventCount":61,...}}     <- FRESH (second capture - correct!)

GET /get_logs?keyword=TriggerCapture&limit=10
  -> {"category":"RenderPassHonestyDiag","frame":24946,
      "message":"TriggerCapture() - graphSnapshot.passesInExecutionOrder.size()=46,
                  AtmosphereAerialPerspectiveCompositePass found=false", ...}
```

This is the decisive evidence: **at the exact moment the SECOND capture ran**
(frame 24946), `TriggerCapture()`'s own `graphSnapshot` — read directly from
`RenderGraph::LastSnapshot()`, the same source `BuildRealFrameDebuggerSnapshot()`
consumes — genuinely had **zero** entries named
`"AtmosphereAerialPerspectiveCompositePass"` (`found=false`). The tree-builder
correctly produced a 2-events-fewer tree (63 -> 61: the pass's own leaf plus
its "Compute Dispatch" child) from that snapshot. The FIRST capture call
(immediately prior, same toggle, same many-frames-elapsed wait) had already
missed this by one full capture cycle, for the structural reason described
above.

### Bidirectional confirmation (re-enable behaves the same way, symmetrically)

```
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=true -> {"success":true}
GET /frame_debugger/capture -> totalEventCount:61   (stale - first capture after re-enabling)
GET /frame_debugger/capture -> totalEventCount:63   (fresh - settled, pass correctly back)
```

Confirms the staleness is a general, always-one-capture-behind property of
`CaptureNowFromCommand()`, not something special to the disable direction.

### Final sanity: engine still renders correctly, no crash, no magenta

```
GET /render_graph/passes -> AtmosphereAerialPerspectiveCompositePass: enabled=true (left in the default, working state)
GET /get_game_view -> HTTP 200, image/png, 158923 bytes (real rendered frame, sane blue/atmosphere-tinted image)
```

---

## Notes on a mistake made and corrected during this investigation

An early attempt to correlate `POST /clear_logs` with an immediately-following
`GET /frame_debugger/capture` **in the same batched tool-call block** produced
a false negative (zero `TriggerCapture()` log entries found anywhere in the
whole session, despite `totalEventCount` visibly changing over time) — caused
by the two HTTP requests racing each other across the server's own per-route
background threads (`AGENTS.md`, "Networking": "every route handler runs on
its own background thread"), sometimes finishing in a different order than
issued. Re-running every step as strictly sequential, single tool calls (one
request, wait for its own response, THEN the next) eliminated this and
produced the clean, decisive log correlation quoted above. This was a
tool-usage mistake on this phase's own part, not a bug in any tool — no
`bug_report` was filed for it.

---

## Files changed (temporary diagnostic instrumentation, category `"RenderPassHonestyDiag"`)

- `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp` (+ `#include "../../Core/Logging.h"`)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` (+ `#include "../../Core/Logging.h"`)
- `src/Core/Core.cpp` (+ `#include "Logging.h"`)
- `src/Editor/Panels/FrameDebuggerPanel.cpp` (+ `#include "../../Core/Logging.h"`)

All four call sites' instrumentation directly contributed to the final
root-cause finding (none were dead ends), per Step 3.6 — all are kept, still
temporary, still under `"RenderPassHonestyDiag"`, for PHASE2 to either remove
or permanently fold in once the real fix lands.

## Verification performed

- **Incremental compile check**: `cmake --build build` — succeeded (see build
  log; one real mistake was made and self-corrected during this phase, an
  accidental deletion of `#include "../../Renderer/Renderer.h"` in
  `FrameDebuggerPanel.cpp` caused by `edit_line`'s auto-dedup interacting with
  a manual duplicate-line insertion — caught immediately by the very next
  build failure and fixed before proceeding).
- **Live HTTP verification**: full reproduction log above, run twice
  (initial exploratory pass + a final, clean, strictly-sequential
  confirmation pass), both showing the identical stale-then-fresh pattern.
- Engine was launched via `run_app_background`, driven entirely via
  `gte_send_request`, and cleanly closed via `stop_app_background` before
  finishing — no stray instance left running.
- No `printf`/`std::cout`/`fprintf`/`OutputDebugString` used anywhere —
  every diagnostic added uses `GTE_LOG_DEBUG`, retrieved exclusively via
  `GET /get_logs`.

## Hypothesis mapping (PHASE0_MASTER_STRATEGY.md Step 2.1)

| Hypothesis | Verdict |
|---|---|
| A regression broke the toggle-registry wiring somewhere after `editor-core-separation-20` | **Ruled out** — `SetEnabled()`/`NoteDeclaredAndCheckEnabled()` both proven correct via log evidence |
| An `ImGuiUniqueId` collision makes one row's checkbox toggle a different entry | **Ruled out** — not exercised (this repro used the HTTP route directly, bypassing ImGui widgets entirely, and still reproduced the exact symptom) — a UI-level collision cannot be the ROOT cause since the bug reproduces with zero ImGui interaction at all |
| Two different registry names (`"AtmosphereComposite"` outer / `"AtmosphereAerialPerspectiveCompositePass"` inner) causing a UX mismatch | **Ruled out as the root cause** — both are correctly, independently read every frame (see logs); the user's screenshot targeted the inner name, which is exactly the one this report's repro toggled |
| **A stale/frozen Frame Debugger capture** | **CONFIRMED** — refined to an exact, reproducible mechanism: `FrameDebuggerPanel::CaptureNowFromCommand()`'s fire-and-forget one-frame-deferred capture trigger, whose own HTTP response never waits for the deferred capture it just armed |
| A genuinely different bug inside `RenderGraphCompiler::Compile()` or `FrameDebuggerData.cpp`'s tree-building logic | **Ruled out** — `TriggerCapture()`'s own log directly confirms the `RenderGraphSnapshot` it consumes is already 100% correct (`found=false`) at the exact moment of a fresh capture, and the tree-builder correctly reflects it (61 vs 63 events, exactly a 2-leaf difference) |

## For PHASE2

PHASE2 (`PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md`) should treat
this as its confirmed root cause and pick a concrete remedy for
`FrameDebuggerPanel::CaptureNowFromCommand()` (and, per this report's "Two
independent contributing layers" section, decide whether the interactive-UI
frozen-capture behavior also needs a companion fix, e.g. auto-invalidating a
currently-displayed capture the instant the toggle registry changes
underneath it) — using `ask_questions` first if the exact shape of that fix
is a genuine design decision, per this campaign's own Locked Decision #5.
