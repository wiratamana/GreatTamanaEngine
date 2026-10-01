# PHASE10 — Final Verification and Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE1`-`PHASE9`, ALL of
which must already be complete and committed.

This is the ONLY phase in this whole campaign allowed to run a full clean build and a full
`ctest` regression pass (Note 4 of this campaign's own process rules).

---

## Step 1 — The Goal

Prove, end-to-end, with a full build and a full regression pass, that this whole 9-phase
campaign left the engine in a strictly BETTER state with **zero regression**: every real compute
pass migrated in PHASE4-7 still renders identically; the new `CommandBuffer`/reflection/
`TextureUsage`/`AddScreenPostProcessPass()` infrastructure all genuinely works; and the confirmed
Finding 10(b) scaffolding bug is fixed for real, live, against the actual
`Projects/ScreenPassAutoWireProbe/` fixture the original investigation's own Finding 2 was
discovered in.

---

## Step 2 — The Situation

`Projects/ScreenPassAutoWireProbe/Assets/` already contains three real, permanent fixture files
(`RedTintScreenPass.cpp`, `YellowTintScreenPass.cpp`, `IterEScreenPass.cpp`) — the exact files
Finding 2 identified as all three drawing red due to unedited copy-pasted scaffold boilerplate.
This campaign does not attempt to fix Finding 2's own root cause (an explicit non-goal, per
`PHASE0`) — but this fixture project IS the correct, already-existing place to live-reproduce and
confirm the PHASE9 Part A fix (Finding 10(b)) against a REAL project, not just the Tier-1 test's
synthetic fixture content.

`AGENTS.md`'s "Render Pass System" section is the living, append-only history of every prior
`render-pass-N` campaign — this campaign's own entry must be appended there, following the exact
same style/detail level every prior entry already uses (read the existing entries for `render-pass-7`
onward as the direct style template).

---

## Step 3 — The Plan

1. **Full clean build.** `cmake --build build` (or a genuinely clean re-configure +
   build if the campaign's own incremental changes across 9 phases raise any doubt about stale
   CMake cache state, particularly given PHASE1's brand-new `spirv_reflect` third-party target).
   Confirm zero errors, and specifically confirm zero NEW warnings introduced by any of this
   campaign's own new files.
2. **Full `ctest` regression pass**:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`.
   Confirm 100% of executed tests pass (matching this repository's own established "100% of
   executed tests passing, N legitimate environment-gated skips" reporting convention — see any
   recent `AGENTS.md` entry for the exact phrasing to reuse), with the total test count having
   grown by exactly the number of new tests this campaign's own phases added (PHASE1's
   `ShaderReflectionTests`, PHASE2's `ComputePipeline` grouping test, PHASE3's
   `CommandBufferTests`, PHASE8's `RenderGraphTypesTests` additions, PHASE9's
   `ScreenPassAutoWireTests` regression test, plus any others individual phases added) — if the
   count does not match what those phases' own completion reports claimed, investigate and
   resolve the discrepancy before proceeding (a silently-not-registered new test file is a real,
   previously-seen failure mode in this codebase's own history — verify `tests/CMakeLists.txt`
   actually globs/lists every new test `.cpp` file this campaign added).
3. **Live, HTTP-driven Game/Scene View smoke test** (`run_app_background` the built Editor
   executable, use `gte_send_request` throughout, `stop_app_background` when fully done):
   - Capture `/get_swapchain` and `/get_game_view` at startup — confirm the scene renders with no
     solid-magenta/black-screen failure mode (the exact symptom `editor-core-separation-20`'s own
     campaign once fixed — a useful sanity baseline to re-check here).
   - Sweep the Atmosphere panel's sun-angle control (or issue the equivalent HTTP/ECS command if
     one exists) and confirm the sky/Transmittance/Multi-Scattering/Sky-View/Aerial-Perspective
     LUTs (PHASE5's migration) still respond correctly.
   - Toggle GPU-driven culling's own test batch (`POST /spawn_gpu_driven_test_batch`, if still
     live per `render-pass-5`'s own documented convention) and confirm culling (PHASE4's migration)
     still responds to camera movement.
   - Toggle GPU Vertex Skinning mode (the Editor "Jobs" panel's "Skinning Mode" toggle, or its
     HTTP equivalent) and confirm animated models still skin correctly (PHASE4's migration).
   - Toggle "Show Blurred Scene Output"/"Show GBuffer Validation" (PHASE6's migration) and confirm
     both still render correctly.
   - Open the Frame Debugger, enable it, capture a frame, confirm the tree still shows every
     migrated compute pass correctly grouped (the "Compute LUT" heading for Atmosphere, the
     generic "Compute Dispatches" bucket for everything else — per `render-pass-7`'s own
     established grouping, which this campaign must not have disturbed at all), and sweep its
     Channels/Levels preview controls (PHASE6's `FrameDebuggerPreviewProcessing` migration).
   - Confirm both demo plugins (`demo_render_feature_v3`/its siblings) still render correctly
     (PHASE7's migration).
   - `GET /get_logs` after every one of the above steps — confirm zero new
     `GTE_LOG_ERROR`/`GTE_LOG_WARNING` entries appear that weren't already present before this
     campaign. **Correction — do NOT use `delegate_task` here, regardless of what any older draft of
     this document may have said**: whoever executes this phase is itself a LEAF, delegated task
     that must NOT call `delegate_task` under any circumstance (per this campaign's own outer
     delegation rules). If a genuine regression traceable to one of THIS campaign's own PHASE1-9
     changes is found, fix it directly, in place, as part of finishing this phase (it is, by
     definition, a small, well-understood, freshly-introduced bug in code this same campaign just
     wrote — not a large, open-ended investigation) — re-run the affected verification step(s)
     afterward to confirm the fix. Only if the regression turns out to be something this document
     could not have anticipated (e.g. it reveals a genuine, pre-existing bug this campaign merely
     exposed rather than caused, or a real design ambiguity with no clear correct fix) should you
     stop and use `ask_questions` to ask the user how to proceed, rather than guessing.
4. **Live-reproduce the Finding 10(b) fix** against the real `Projects/ScreenPassAutoWireProbe/`
   fixture: pick one of its three existing `*ScreenPass.cpp` files (or scaffold a brand-new
   throwaway fourth one via `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=<X>`
   specifically for this test, reverting it afterward), manually comment out ONLY its
   `Register<Name>ScreenPass(core);` call line inside `ScreenPassAutoWireProbeGame.cpp` (leaving
   its forward declaration active), re-trigger the scaffolding tool for that SAME name (forcing
   `TryAutoWireRegisterCall()` to run again against an already-partially-wired file), and confirm
   — by reading the resulting file directly — that NO duplicate forward declaration was inserted
   (PHASE9 Part A's own fix, now proven against a real project, not just a synthetic test
   fixture). Revert this manual edit afterward if it was made against a permanent fixture file
   (restore the active call), or delete the throwaway fourth file if one was created.
5. **Update `AGENTS.md`'s "Render Pass System" section** — append a new paragraph, in the exact
   same style/detail level as the existing `render-pass-7` entry immediately above it, summarizing
   this whole campaign: the new `gte::rg::CommandBuffer`, SPIRV-Reflect-based
   `CreateComputePipeline(path)`, the full migration of every real compute pipeline in the engine,
   `TextureDesc::usage`/the new transient-`RWTexture` capability, the Finding 10(b) scaffolding fix,
   and `Core::AddScreenPostProcessPass()` — plus an explicit, honest restatement that Milestone 2
   (bindless, R3) and the Atmosphere dirty-flag optimization (R8) remain deliberately, explicitly
   deferred to a future `better-render-pass-2` campaign, mirroring every prior campaign's own
   "what remains genuinely open, honestly restated" discipline (see the `RENDERGRAPH_FUTURE_TODO_
   DELIBERATELY_NOT_IMPLEMENTED.md`/`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`
   precedent for the exact tone/level of honesty expected).
6. Write `CAMPAIGN_COMPLETION_REPORT.md` in this same folder — a full, phase-by-phase summary
   (mirroring every prior campaign's own `CAMPAIGN_COMPLETION_REPORT.md` shape, e.g.
   `task_manager/render-pass-7/CAMPAIGN_COMPLETION_REPORT.md`), including: the full clean build
   result, the full `ctest` result (exact before/after test counts), every live verification step's
   outcome, and an explicit "what remains genuinely open" section restating Milestone 2/R8's
   deferral plus anything any individual phase's own completion report flagged as a remaining gap
   (e.g. PHASE7's optional `CommandBuffer`-for-the-plugin-adapter sub-goal, if it was not
   completed).
7. `git_add`/`git_commit` everything, including the `AGENTS.md` update.

### Acceptance bar for this phase (and the whole campaign)

- Full clean build: zero errors, zero new warnings.
- Full `ctest`: 100% of executed tests passing, test count increase matches the sum of every
  phase's own reported new-test count.
- Every live verification step in Step 3/4 passes with zero new log warnings/errors.
- `AGENTS.md` updated; `CAMPAIGN_COMPLETION_REPORT.md` written; everything committed.
- If ANY step above surfaces a genuine regression traceable to this campaign's own PHASE1-9 changes,
  fix it directly as part of finishing this phase (never via `delegate_task` — this phase is itself
  a leaf, delegated task that must not spawn another one, per this campaign's own outer delegation
  rules), then re-run the affected verification step(s) from scratch once that fix lands. Only use
  `ask_questions` (never `delegate_task`) if the regression is genuinely outside what this document
  anticipated and you cannot confidently resolve it yourself.
