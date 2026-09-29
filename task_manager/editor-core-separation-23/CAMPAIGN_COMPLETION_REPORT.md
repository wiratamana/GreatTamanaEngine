# CAMPAIGN COMPLETION REPORT — `editor-core-separation-23` ("Project Assembly On-Screen Render Feature Compositing" — BIG-STEP 1 of 2)

Branch: `feature/editor-core-separation` (unchanged throughout, as required)
Campaign folder: `task_manager/editor-core-separation-23/`
Status: **Complete.** Full clean build succeeded (614/614 steps, zero errors,
zero warnings), full `ctest` regression pass succeeded (2055 tests, 100% of
executed tests passing, 25 legitimate environment-gated skips — up from
`editor-core-separation-22`'s own final baseline of 2036 tests/8 skips, a
clean **+19 tests / +17 skips**, every single delta individually accounted
for by name against this campaign's own six implementation phases), and a
final, live, HTTP-driven, end-to-end verification (PHASE6) confirmed a real,
hand-wired Project Assembly render feature is genuinely visible on screen,
survives a real hot-reload cycle, and survives 20 rename cycles with bounded
GPU-state slot consumption.

---

## The goal

Give a Project Assembly's own C++ code (a `_Game.dll`, calling a new
`Core::RegisterProjectRenderFeature()` from its own `RegisterProject()` entry
point) a way to register an on-screen render feature that is genuinely
visible in the SAME Game View/Scene View image real users see — through the
SAME already-proven, hazard-free `RenderFeatureCompositor` blend chain
`gte_plugin_abi` plugins already use today — with **zero** manual
`ImportTexture()`/handle-aliasing code of its own, safe across an unbounded
number of register/rename/unregister/re-register cycles over one long, live
Editor session, and safe across a hot-reload cycle (no dangling
`std::function` ever points into an unloaded `.dll`'s own code after
teardown). This is BIG-STEP 1 of a two-part effort — BIG-STEP 2 (an Editor
"Create → Screen Post-Process Pass" menu item) is a SEPARATE, future
campaign, explicitly out of scope here.

## The shape: seven phases, one additive third module kind

`src/Core/Plugins/RenderFeatureCompositor.cpp` already proved this exact
on-screen blend chain for `gte_plugin_abi`'s `IRenderFeatureModule_v2`/`_v3`
plugins (`editor-core-separation-6` through `-9` campaigns). This campaign
does not re-invent that mechanism — it exposes a third, additive "module
kind" into the exact same `Entry`/`ContributeRenderGraphPasses()` machinery,
sized correctly for a Project Assembly's genuinely different usage pattern
(registered/renamed/deleted/re-registered many times per session, vs. a
plugin's "scanned once, forever" lifetime):

- **PHASE1** — new `ProjectRenderFeatureCallback.h` (a brand-new,
  free-standing header, never nested inside `RenderFeatureCompositor` so
  `Core.h` can keep forward-declaring that still-incomplete class);
  `RenderFeatureCompositor::Entry` gains `projectCallback`/`projectFeatureSlot`;
  the two `moduleV3`-branch call sites widened into real three-way branches,
  the third arm a true no-op for existing entries. See
  `PHASE1_COMPLETION_REPORT.md`.
- **PHASE2** — `RenderFeatureCompositor::RegisterProjectFeature()`/
  `UnregisterProjectFeature()`; the bounded, reusable
  `kMaxConcurrentProjectRenderFeatures = 16`-sized GPU-state slot free-list
  (never keying GPU state by a human-typed, unbounded-rename-risk
  `descriptor.name`); `gpuStateKey` resolution rewrite in
  `ContributeRenderGraphPasses()`; the new `RenderFeatureDebugEntry::isProjectFeature`
  field (mirroring `isV3`) plus its `GET /render_graph`/"Render Graph" panel
  `"[Project]"` tag. See `PHASE2_COMPLETION_REPORT.md`.
- **PHASE3** — `Core::RegisterProjectRenderFeature()`/
  `UnregisterProjectRenderFeature()`, thin pass-throughs mirroring
  `RegisterProjectRenderPassProvider()`'s own precedent; the 63-byte
  `debugName` REJECT-not-truncate rule, the first call site in this engine
  building this kind of name from free-form, un-length-checked input. See
  `PHASE3_COMPLETION_REPORT.md`.
- **PHASE4** — `ProjectAssemblyRegistrationLedger` gains `renderFeatureNames`/
  `RecordRenderFeature()`; `UnregisterEverythingFor()` gains a new teardown
  loop, correctly ordered strictly BEFORE the pre-existing `renderPassNames`
  loop (consumer torn down before its potential producer);
  `IHotReloadDebugCapability::LedgerEntry`/`EditorHotReloadDebugCapability::
  GetLedgerEntry()`/`GET /project_assembly/debug/ledger` all mirror it. This
  is the single HARD REQUIREMENT gating correctness — a dangling
  `std::function` into an unloaded `.dll` would otherwise crash the engine.
  See `PHASE4_COMPLETION_REPORT.md`.
- **PHASE5** — a debug-build-only main-thread-affinity assertion on the two
  new mutators (`RegisterProjectFeature()`/`UnregisterProjectFeature()`,
  the SMALLEST possible mechanism, scoped to `RenderFeatureCompositor` alone,
  per an `ask_questions` resolution); a new, permanent Tier-1 regression test,
  `RenderGraphCompilerTest.ProjectRenderFeatureStyleAfterEverythingPassesNeverContradict`,
  proving a `projectCallback`-declared pass tagged
  `RenderPassEvent::AfterEverything` reports zero
  `DetectRenderPassEventContradictions()` findings. See
  `PHASE5_COMPLETION_REPORT.md`.
- **PHASE6** — the actual live proof. A real, permanent, hand-wired demo
  feature, `"ProjectAssemblyProbe.ScreenTint"`
  (`Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`, intentionally NOT
  committed to git — `Projects/` is `.gitignore`d by pre-existing,
  deliberate `editor-core-separation-11` policy, confirmed via
  `ask_questions`), a translucent red `AlphaOver` tint at `priority=100` (a
  deliberate, evidence-driven deviation from the design doc's own sketched
  `priority=0`, discovered live: a pre-existing demo plugin's `Replace` blend
  mode was silently wiping it out at `priority=0` — root-caused via a
  rigorous A/B investigation, NOT assumed). Full HTTP-driven live proof:
  registration succeeds; the feature is structurally distinguishable
  (`"is_project_feature":true`, `"[Project]"` tag); genuinely visible in a
  real `GET /get_game_view` screenshot, with the exact pixel math matching
  the documented `AlphaOver` formula to the integer; survives a real
  `POST /project_assembly/hot_reload` cycle with exactly one surviving entry,
  never duplicated or dropped; survives 20 real hot-reload rename cycles with
  every single one of 21 register calls claiming the EXACT SAME GPU-state
  slot (`15`) — a genuine, mechanical, log-based proof of bounded slot reuse,
  not merely a code-review claim; two simultaneous project features blend
  correctly, in priority order, matching hand-computed `AlphaOver` math
  within 1 unit of 8-bit rounding; every `gte_plugin_abi` `_v2`/`_v3` demo
  plugin feature stayed completely unaffected throughout. See
  `PHASE6_COMPLETION_REPORT.md`.
- **PHASE7** (this phase) — the full clean build + full regression pass +
  documentation + the literal Step 9 Entry Gate tick-through, all at once, at
  the very end, per every prior campaign's own established precedent. See
  `PHASE7_COMPLETION_REPORT.md`.

## Final verification numbers (PHASE7)

- **Full clean build**: `cmake --build build --target clean` (631 files
  removed) followed by `cmake --build build` — **614/614 steps succeeded,
  zero errors, zero warnings** (this repo's build enables no
  `-Wall`/`-Wextra` for its own code, so "zero new warnings introduced" and
  "zero warnings at all" are the same statement here, confirmed by a full
  read of the build's own console output).
- **Full `ctest -C Debug --output-on-failure`**: **2055 tests total, 100% of
  executed tests passing, 25 legitimate environment-gated skips** — up from
  `editor-core-separation-22`'s own documented 2036/8 baseline, a clean
  **+19 tests / +17 skips**, exactly matching this campaign's own new test
  files/cases across PHASE1 (1), PHASE2 (8, all skip), PHASE3 (7, all skip),
  PHASE4 (2 new + 2 renamed-extended, the 2 new ones skip alongside the 2
  pre-existing skips in that file), and PHASE5 (1). No test failed at any
  point during this phase's own full-suite run — the "diagnose and fix it
  yourself"/`delegate_task` contingency was never triggered.
- **Final live, HTTP-driven verification (PHASE6, re-confirmed by this
  phase's own documentation cross-check)**: a real, hand-wired
  `ProjectAssemblyProbe.ScreenTint` feature genuinely tints the Game View on
  screen, survives hot reload with zero duplication/dangling-callback risk,
  and survives 20 rename cycles reusing exactly one bounded GPU-state slot —
  every claim backed by real screenshots, JSON snippets, and log excerpts,
  never merely "looked fine".

## The Entry Gate for BIG-STEP 2

**Every checkbox in the design doc's own Step 9 checklist is independently,
freshly confirmed TRUE (see `PHASE7_COMPLETION_REPORT.md`'s own full,
evidenced tick-through) — therefore
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
may now be opened by a FUTURE, SEPARATE campaign. This campaign
(`editor-core-separation-23`) itself never opens or acts on that file in any
way.**

## Honest, permanent limitations (restated plainly, not silently smoothed over)

1. **This is BIG-STEP 1 of 2 only.** There is no Editor UI menu item, no
   scaffold template, and no "Create → Screen Post-Process Pass" wizard for
   this capability — a Project Assembly author must call
   `Core::RegisterProjectRenderFeature()` directly from their own
   `RegisterProject()` entry point. That is BIG-STEP 2's own, separate,
   future job.
2. **`Projects/` remains `.gitignore`d, by pre-existing, deliberate policy**
   — `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s own permanent
   `ScreenTint` feature (this campaign's one real, concrete proof artifact)
   is real, on-disk, and fully live-verified, but was never, and will never
   be, tracked by git — a genuine, honestly-disclosed conflict between
   PHASE0's own "commit the code change" convention and this repo's own
   `editor-core-separation-11`-era `/Projects/` policy, resolved via
   `ask_questions` in favor of respecting the pre-existing policy rather than
   force-adding around it (see `PHASE6_COMPLETION_REPORT.md`'s own dedicated
   section for the full reasoning).
3. **A `PostComposite`-stage Project Assembly feature's own priority ordering
   has a real, reachable failure mode** if a `Replace`-blend-mode entry
   exists elsewhere in the same stage's chain (discovered live, PHASE6) — a
   future Project Assembly author writing a `PostComposite` feature must pick
   a priority higher than any known `Replace`-mode entry in that stage to
   survive to the final image, or verify empirically. This is a structural
   property of `RenderFeatureBlendMode::Replace` itself, not a bug this
   campaign's own code introduced or could fix without changing existing,
   unrelated demo-plugin behavior (explicitly out of scope).
4. **`kMaxConcurrentProjectRenderFeatures = 16` is a fixed, hardcoded ceiling**
   — a session that tries to keep MORE than 16 project render features
   simultaneously registered (not renamed — genuinely, concurrently
   registered) will see the 17th registration fail, by design, exactly as
   PHASE2's own Tier-1 test (`TheSeventeenthRegistrationFailsAndAllPriorSixteenRemainRegistered`)
   documents. Raising this ceiling is a one-line, future, separate decision
   if ever needed — not attempted by this campaign, since no real usage
   pattern requiring more than 16 concurrent features was ever identified.

None of these four limitations are regressions or open bugs in what this
campaign shipped — all four are either deliberate, documented scoping
decisions (limitations 1 and 4), a pre-existing repo policy this campaign
correctly respected rather than violated (limitation 2), or a genuinely
new, honestly-disclosed interaction discovered and root-caused live during
this campaign's own final verification phase, with a clear, correct fix
already applied to this campaign's own proof artifact (limitation 3).

## Files changed across the whole campaign

- `src/Core/Plugins/ProjectRenderFeatureCallback.h` (new, PHASE1)
- `tests/Core/Plugins/ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests.cpp` (new, PHASE1)
- `src/Core/Plugins/RenderFeatureCompositor.h`/`.cpp` (PHASE1's third module
  kind; PHASE2's slot pool/`gpuStateKey`/`RegisterProjectFeature()`/
  `UnregisterProjectFeature()`; PHASE5's main-thread-affinity assertion)
- `src/Core/Plugins/RenderFeatureDebugEntry.h` (PHASE2 — `isProjectFeature`)
- `src/Core/Plugins/RenderFeatureNamePool.h` (PHASE2 — doc-comment update)
- `src/Renderer/RenderGraph/RenderGraphMetadata.cpp` (PHASE2 — `"is_project_feature"` JSON)
- `src/Editor/Panels/RenderGraphPanel.cpp` (PHASE2 — `"[Project]"` tag)
- `tests/Core/Plugins/RenderFeatureCompositorProjectFeatureTests.cpp` (new, PHASE2)
- `src/Core/Core.h`/`.cpp` (PHASE3 — `RegisterProjectRenderFeature()`/
  `UnregisterProjectRenderFeature()`)
- `tests/Core/RegisterProjectRenderFeatureApiTests.cpp` (new, PHASE3)
- `src/Core/Plugins/ProjectAssemblyRegistrationLedger.h`/`.cpp` (PHASE4 —
  `renderFeatureNames`/`RecordRenderFeature()`/teardown ordering)
- `src/Core/EditorCapabilities.h` (PHASE4 — `LedgerEntry::renderFeatureNames`)
- `src/Editor/EditorHotReloadDebugCapability.cpp` (PHASE4 — field copy)
- `src/Network/NetworkRoutes.cpp`/`.h` (PHASE4 — `"render_feature_names"` JSON key)
- `tests/Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp` (PHASE4 — extended)
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` (PHASE5 — new
  ordering regression test)
- `tests/CMakeLists.txt` (new test file registrations, multiple phases)
- `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` (PHASE6 — the real,
  permanent demo feature; `.gitignore`d, never committed, by pre-existing
  policy)
- `docs/conventions/project-assembly-system.md` (PHASE7 — rewritten
  `### On-screen Game View compositing` section + fixed Non-Goals
  cross-reference)
- `AGENTS.md` (PHASE7 — new "Project Assembly On-Screen Render Feature
  Compositing" section)
- Every `PHASEn_COMPLETION_REPORT.md`/this `CAMPAIGN_COMPLETION_REPORT.md` in
  `task_manager/editor-core-separation-23/`

## Delegation and ambiguity summary across the whole campaign

- `delegate_task(position: "next")` self-double-checks were issued twice
  (PHASE2, PHASE6 — the two heaviest phases, exactly as PHASE0's own Locked
  Decision #9 anticipated), each reporting back inline, never creating its
  own report file, per that same rule.
- `ask_questions` was invoked across three of the seven phases: PHASE3 (one
  question, on whether to build a dedicated stub for an unreachable-in-
  practice null-compositor test path — resolved in favor of code review plus
  a defensive null check, per the phase file's own offered fallback), PHASE5
  (one question, on the main-thread-affinity mechanism's shape — resolved in
  favor of the smallest possible, class-scoped mechanism), and PHASE6 (four
  questions across its own live-verification work — the bounded-slot-reuse
  driving mechanism, whether to keep a temporary second demo feature
  permanently, and the genuine `Projects/`-gitignore-vs-"commit the code"
  conflict — all resolved in favor of the most honest/minimal-footprint
  option each time). PHASE7 itself needed zero `ask_questions` calls — the
  full regression run surfaced no failures, and the documentation rewrite's
  own scope was already fully anticipated and resolved by the phase file's
  own explicit instructions.
- No `delegate_task` call was made in PHASE7 — the full-suite run surfaced no
  regression needing a fix.
