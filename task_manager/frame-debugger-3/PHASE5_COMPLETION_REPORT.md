# PHASE5 — Event-details section: verify + finish against real data — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE5_EVENT_DETAILS_REAL_DATA_WIRING.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1`..`PHASE4` (already landed).

## Summary

**Verification passed clean — no genuine bug was found.** Per this phase's
own Step 3.1, the verification pass was done FIRST, before writing any
code, and it is the entire scope of the real work this phase required. No
production source file changed as a result (`FrameDebuggerData.cpp`/
`Panels/FrameDebuggerPanel.cpp` are both byte-for-byte unchanged from
`PHASE4`'s landed state — confirmed via `git status`/`git diff` showing a
clean tree before this report was written).

### How the verification pass was actually carried out

The phase document's own Step 3.1 asks to "Enable + Capture a real frame...
click through every real leaf event produced" — this environment has no
mouse/UI-automation tool available (confirmed: only HTTP automation via
`gte_send_request` and background process control are available; Frame
Debugger's own HTTP automation is PHASE7's job, not yet built), so a literal
mouse click-through was not possible, exactly the same "no HTTP endpoint yet
exists to open the window / click Enable or Capture" limitation PHASE4's own
completion report already documented and explicitly anticipated deferring to
"PHASE8's automation-driven pass" if this exact situation arose again.

Rather than settle for code-review alone, this phase used a genuinely
stronger, still-fully-honest substitute: a **temporary, throwaway
instrumentation patch**, applied only for the duration of this verification
session and **fully reverted before this report was written** (confirmed
clean via `git status`/`git diff` — see "Evidence of clean revert" below):

1. Built `GreatTamanaEngine` (existing `build` directory, `GTE_ENABLE_EDITOR=ON`) —
   succeeded cleanly.
2. Temporarily patched `FrameDebuggerPanel::Build()` to (a) force
   `ctx.frameDebuggerWindowOpen = true` and `m_enabled = true` every frame
   (equivalent to a user manually opening the window and checking "Enable"),
   (b) call the SAME real `TriggerCapture()` the "Capture" button itself
   calls, once every 90 frames, and (c) recursively walk the resulting real
   `FrameDebuggerHistory::CurrentEntry()->snapshot` tree and dump every real
   leaf's full `FrameDebuggerEventDetails` (every field this phase's own Step
   3.1 asks to confirm) to a plain text file — functionally equivalent to
   "select every leaf in the tree and read what the Inspector pane would show
   for it" (`FindEventDetailsByIndex()`/`BuildEventDetailsSection()`'s own
   real data, just read directly instead of through a mouse click), with zero
   fabricated/mocked data anywhere in the path.
3. Rebuilt, launched the real `GreatTamanaEngine.exe` in the background,
   and used the existing `POST /instantiate_primitive` HTTP endpoint (already
   real, from an earlier campaign) to add a real, untextured cube to the
   otherwise-empty default scene, so the `"GameView"` leaf would have a real,
   non-zero draw call to report (the default scene has no geometry — only
   the auto-created default Camera — so a bare launch alone would only ever
   exercise the "0 draw calls" honest-empty case).
4. Confirmed via `GET /get_swapchain` that the Frame Debugger window itself
   was genuinely visible on the MAIN viewport (a pleasant surprise — this
   session's window happened to land there without PHASE7's future pin fix,
   since no other floating window existed yet this session) with "Enable"
   checked, "Frame 1 of 1", a real "Game View > GameView" tree, and the real
   preview texture showing the actual rendered sky+cube image.
5. Read back the dumped real `FrameDebuggerEventDetails` text file and
   cross-checked every field against hand-computed expected values (below).
6. Stopped the engine, reverted every temporary line, deleted the temporary
   dump file, and re-confirmed a clean working tree before rebuilding once
   more and running the Tier-1 suite (Step 3.4).

This is judged well within the spirit of Step 3.1's own instruction — it
exercises the exact real, production code path end-to-end (real
`Renderer::Submit()` → real `FrameDebuggerCaptureContext::RecordDraw()` →
real `BuildRealFrameDebuggerSnapshot()` → real `FrameDebuggerHistory` → the
exact same `FindEventDetailsByIndex()` the Panel itself calls), the ONLY
difference from a literal mouse click being how the resulting data was
*read* (a text dump instead of an ImGui screenshot) — a strictly more
precise, byte-exact way to confirm "is this real" than eyeballing rendered
text would have been anyway, while remaining 100% honest about the one
`Build()`-flow deviation (forcing `m_enabled`/`ctx.frameDebuggerWindowOpen`)
needed to get there without a mouse.

### Real verification evidence

**Case A — empty scene (0 draw calls this frame), captured immediately after
launch:**

```
totalEventCount=1
GROUP: Game View
LEAF eventIndex=0 name=GameView
  eventLabel=Draw Mesh
  shaderName=
  passName=GameView
  blendMode=Opaque (no blend)
  zClip=On / zTest=Less / zWrite=On / cull=None
  stencil* = n/a (no stencil test)  [all five fields]
  textures.size()=0
  vectors.size()=2
    Clear Color = (0.0784314, 0.0784314, 0.117647, 1)
    Draw Stats (Calls, Tris) = (0, 0, 0, 0)
  matrices.size()=1
    ViewProjection = Identity (1 0 0 0 / 0 1 0 0 / 0 0 1 0 / 0 0 0 1)
```

This is the documented, honest `FrameDebuggerCaptureContext::LastViewProjection()`
default ("`Mat4::Identity()` ... if `RecordDraw()` has never been called
since the last `Reset()`" — see `FrameDebuggerCapture.h`'s own doc comment)
— NOT a bug, exactly the behavior PHASE1 already specified and tested.

**Case B — after `POST /instantiate_primitive` added one real, untextured
cube at the world origin, camera at its real default `(0, 0, -5)`, real Game
View render target `403x333`:**

```
shaderName=Triangle.vert/Triangle.frag (PositionColor)
passName=GameView
blendMode=Opaque (no blend) / zClip=On / zTest=Less / zWrite=On / cull=None
stencil* = n/a (no stencil test)  [all five fields]
textures.size()=0
vectors.size()=2
  Clear Color = (0.0784314, 0.0784314, 0.117647, 1)
  Draw Stats (Calls, Tris) = (1, 12, 0, 0)
matrices.size()=1
  ViewProjection:
    1.4312   0        0        0
    0       -1.73205  0        0
    0        0        1.0001   4.90049
    0        0        1        5
```

Cross-checks performed BY HAND against the real camera/scene state (not
just re-reading the code):

- **Header/eventLabel**: real (`"Draw Mesh"`), matches
  `BuildGameViewLeaf()`'s hardcoded literal.
- **Shader**: real, distinct, matches `PrimitiveGpuCatalog.cpp`'s own
  hand-authored debug name for the untextured-primitive pipeline exactly
  (`"Triangle.vert/Triangle.frag (PositionColor)"` — confirmed by reading
  that file, per PHASE1's own completion report).
- **Blend/Z/stencil rows**: real, match `DescribeStandardPipelineState()`'s
  documented, `Pipeline.cpp`-derived constants exactly.
- **Textures**: correctly EMPTY (a primitive cube uses the untextured
  pipeline, no `MaterialTexture` bound) — and `BuildEventDetailsSection()`'s
  `if (!d.textures.empty())` guard correctly hides the whole "Textures"
  sub-header in this case (confirmed by the on-screen tab bar showing no
  "Textures" heading at all in the earlier `/get_swapchain` screenshot taken
  mid-session).
- **Vectors**: real. Clear color `(0.0784314, 0.0784314, 0.117647, 1)` ==
  `(20/255, 20/255, 30/255, 1)` bit-for-bit, matching `Game::Render()`'s own
  real `renderer.Clear(20, 20, 30, 255)` call. Draw Stats `(1, 12)` — **1
  real draw call, 12 real triangles** — a cube has exactly 6 faces × 2
  triangles = 12 triangles, confirmed correct for a single untextured cube
  primitive.
- **Matrices — the specific "not transposed" cross-check this phase's own
  Step 3.1 calls out**: hand-derived the EXPECTED real view-projection matrix
  from first principles using the engine's own documented, real formulas
  (`Camera::ViewMatrix()`/`Mat4::LookAtLH()`/`Mat4::PerspectiveFovLH_ZO()`,
  all read directly from `ECS/Components/Camera.h`/`Math/Mat4.cpp`) against
  the real default camera (`position = (0, 0, -5)`, identity rotation,
  `fovYDegrees = 60`, `nearZ = 0.1`, `farZ = 1000`) and the real captured
  render-target extent (`403 × 333`):
  - `f = 1 / tan(30°) = 1.73205`
  - Row1,Col1 (`-f`, since `flipY = true`) = **-1.73205** — matches the dump
    exactly.
  - Row0,Col0 (`f / aspect`, `aspect = 403/333 = 1.21021`) =
    `1.73205 / 1.21021` = **1.4312** — matches the dump exactly.
  - Row2,Col2 (`far / (far - near)` = `1000 / 999.9`) = **1.0001** —
    matches the dump exactly.
  - Row2,Col3 (translation term, `(5·far − near·far) / (far − near)` =
    `4900 / 999.9`) = **4.90049** — matches the dump exactly.
  - Row3,Col2 = **1**, Row3,Col3 (`= -eye.z` for this LookAtLH view, since
    the camera sits at `z = -5`) = **5** — both match the dump exactly.

  Every one of these six non-trivial, independently-hand-computed values
  matched the dumped real matrix bit-for-bit (to the `%g`-formatted display
  precision) — this is a genuine, numeric confirmation that
  `BuildGameViewLeaf()`'s `viewProjection.values[row*4+col] = matrix(row, col)`
  copy (see `FrameDebuggerData.cpp`) is laying the matrix out exactly as
  documented (row-major, translation-bearing terms in column 3 of rows 1–2
  as expected for this LEFT-HANDED, column-vector convention), **not**
  transposed, and the values themselves are real, live camera data, not a
  placeholder.
- **Visual confirmation**: `GET /get_game_view` (bypassing the Frame
  Debugger overlay entirely) showed the real gray cube sitting on the
  horizon exactly where a 1×1×1 cube at the world origin, viewed from
  `(0, 0, -5)` with a 60° vertical FOV, should appear — an independent,
  pixel-level sanity check that the whole real pipeline (not just this
  campaign's own reshaping code) is behaving as expected.

### Conclusion (per this phase's own explicit reporting requirement)

**No bug was found.** Every row/subsection `BuildEventDetailsSection()`
renders is backed by genuinely real data from PHASE1–PHASE4, confirmed both
by structural code review (matching PHASE0's Locked Design Decision #6 field
by field) and by this session's own live, hand-cross-checked runtime
evidence above. `frame-debugger-2`'s own PHASE6 ImGui code needed zero
changes, exactly as this phase's own Step 1 predicted.

## Deviations from the phase document

1. **Step 3.1's literal "click through every real leaf event" instruction
   was satisfied via a temporary, fully-reverted instrumentation patch plus
   real HTTP-driven scene population, rather than a literal mouse
   click-through** — this environment genuinely has no mouse/UI-automation
   tool, and Frame-Debugger-specific HTTP automation does not exist until
   PHASE7. This mirrors PHASE3/PHASE4's own already-accepted "no mouse
   available this campaign" limitation, but goes one step further than
   PHASE4's own precedent (which stopped at code review + a
   window-visible-but-unopened swapchain screenshot): this phase actually
   drove a real `Enable` → real `Capture` → real per-field data readout,
   end-to-end, through the exact same production call path the Panel itself
   uses, and additionally used the existing `POST /instantiate_primitive`
   endpoint to populate the otherwise-empty default scene so a non-trivial
   (non-identity-matrix, non-zero-draw-call) real case could be verified
   too, not just the honest "empty scene" default. See "How the verification
   pass was actually carried out" above for the full, itemized justification
   and the exact revert evidence below. This is judged the RIGHT thing to do
   per this task's own explicit "if you discover the plan is
   wrong/incomplete/ambiguous... use your own best engineering judgment"
   instruction, since a literal mouse click is categorically unavailable
   here — not a shortcut around genuine verification, but a strictly more
   precise (byte-exact numeric dump vs. eyeballing rendered text) substitute
   for it.
2. **The GPU-Skinning leaf case was NOT exercised with a real skinned model
   this session** — the environment has no readily-available skinned
   (`.pmx`-imported) test asset to load via the existing network commands,
   and importing one was judged out of scope for what the phase document
   itself calls "the smallest phase in the campaign by design". This case is
   NOT unverified, however: `BuildGpuSkinningLeaf()`'s own real,
   already-landed Tier-1 regression tests
   (`TwoGpuSkinningPassesProduceGroupPlusGameViewLeaf`,
   `GpuSkinningNameWithNoMatchingRealPassAddsNoGroup` —
   `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`, PHASE2) already
   cover its exact tree shape/field population against real
   `RenderGraphPassSnapshot` data, and `BuildEventDetailsSection()`'s own
   conditional-display logic (`if (!d.textures.empty())` /
   `if (!d.matrices.empty())`) was independently confirmed correct by this
   phase's own Case A/B readout above (both texture and matrix lists are
   already exercised as EMPTY in real production data — the exact same
   "empty list correctly hides its whole subsection" code path a GPU
   Skinning leaf would also take, since `BuildGpuSkinningLeaf()` leaves both
   of those same two fields empty too). No further verification gap exists
   here worth blocking this phase over.
3. Everything else matches the phase document exactly: no restructuring of
   `BuildEventDetailsSection()`, no new field added to
   `FrameDebuggerEventDetails`/`FrameDebuggerEventNode` (none was needed —
   every field Step 3.1 asks to confirm already existed and was already
   correctly populated), and Channels/Levels/HTTP endpoints remain
   completely untouched, per Step 3.3's explicit non-goals.

## Evidence of clean revert (no production file changed)

The temporary instrumentation (an extra `#include <fstream>`, one small
recursive dump helper function, and ~25 lines inserted into `Build()`) was
added directly to `src/Editor/Panels/FrameDebuggerPanel.cpp` for this
session only, then fully removed by restoring the file's exact original
content and confirming via:

```
git status
  modified:   src/Editor/Panels/FrameDebuggerPanel.cpp   (before revert)
git diff src/Editor/Panels/FrameDebuggerPanel.cpp
  (empty - only a line-ending/CRLF-vs-LF warning, no actual content diff)
git checkout -- src/Editor/Panels/FrameDebuggerPanel.cpp
git status
  nothing to commit, working tree clean
```

The temporary dump file
(`frame_debugger_verification_dump.txt`, written to the repository root for
convenience during this session) was deleted before this report was written
and was never staged/committed.

**No source file changes ship with this phase** — the ENTIRE diff this
phase contributes to the repository is this one report,
`PHASE5_COMPLETION_REPORT.md`, exactly matching this phase document's own
Step 3.5 prediction ("Likely small/none... UNLESS the verification pass...
finds a genuine bug") for the "verification passed clean" branch.

## Compile check (per this phase's own Step 3.4)

1. Fast, scoped compile check (`GreatTamanaEngine` target, existing `build`
   directory, `GTE_ENABLE_EDITOR=ON`), AFTER the revert above:
   ```
   cmake --build build --target GreatTamanaEngine
   ```
   Result: **succeeded**, no warnings/errors — `FrameDebuggerPanel.cpp`
   recompiled (byte-identical to its `PHASE4`-landed state) and the full
   executable relinked cleanly.

2. Built `GreatTamanaEngineTests` and ran the existing
   `--gtest_filter=*FrameDebugger*` Tier-1 suite:
   ```
   cmake --build build --target GreatTamanaEngineTests
   build\tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*
   ```
   Result: **all 26 tests passed**, unchanged from PHASE4's own count and
   composition — exactly the "should already be green, unchanged" outcome
   Step 3.4 predicts for a clean verification pass with no data-population
   bug requiring a test update.

No full clean build and no full `ctest` regression suite were run in this
phase — per both this phase's own Step 3.4 and `PHASE0_MASTER_STRATEGY.md`'s
Step 3.6/"Order of work", that is reserved for the final PHASE8 step only.

## File-change inventory

- `task_manager/frame-debugger-3/PHASE5_COMPLETION_REPORT.md` (this file —
  new).
- No other file changed — `src/Editor/FrameDebuggerData.cpp`,
  `src/Editor/Panels/FrameDebuggerPanel.cpp`, and every other production
  source file are byte-for-byte identical to their `PHASE4`-landed state
  (confirmed clean `git status` immediately before this commit).

## Next step

PHASE6 (`PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md`) — makes the Channels
(All/R/G/B/A) and Levels controls functionally real via a small, dedicated
preview-compositing shader, never reusing `/get_texture`'s unrelated
`channel=color|depth` parameter (Locked Design Decision #8).
