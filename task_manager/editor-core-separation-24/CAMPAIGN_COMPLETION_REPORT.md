# CAMPAIGN COMPLETION REPORT — `editor-core-separation-24` ("Project Assembly On-Screen Render Feature Compositing" — BIG-STEP 2 of 2)

Branch: `feature/editor-core-separation` (unchanged throughout, as required)
Campaign folder: `task_manager/editor-core-separation-24/`
Status: **Complete.** Full clean build succeeded (623/623 steps, zero errors,
zero warnings), full `ctest` regression pass succeeded (2081 tests, 100% of
executed tests passing, 25 legitimate environment-gated skips — up from
`editor-core-separation-23`'s own final baseline of 2055 tests/25 skips, a
clean **+26 tests / +0 skips**, every single delta individually accounted for
by name against this campaign's own eight implementation phases), and a
final, live, mouse-and-HTTP-driven, end-to-end verification (PHASE8)
confirmed a Screen Post-Process Pass scaffolded into a FRESH project,
compiled, and hot-reloaded — with ZERO manual C++ editing performed by the
tester — is genuinely visible as a translucent red tint in a real Game View
screenshot, survives 5 back-to-back delete/recreate/compile/reload cycles
with a bounded, reused GPU-state slot, and gracefully, safely falls back to a
manual reminder on a pre-existing project with zero corruption.

**This closes the entire, 2-BIG-STEP "Project Assembly On-Screen Render
Feature Compositing" effort (`editor-core-separation-23` + `editor-core-
separation-24`) for good** — exactly like `editor-core-separation-15` closed
the 4-BIG-STEP "Project Assembly Hot Reload" effort before it.

---

## The goal

Close the one remaining gap `editor-core-separation-23` (BIG-STEP 1)
deliberately left open: a Project Assembly author previously had to
hand-write a call to `Core::RegisterProjectRenderFeature()` themselves, with
zero Editor support. This campaign gives them a fourth Editor "Create" menu
scaffold kind — "Screen Post-Process Pass..." — that writes a real, working,
translucent-red-tint `.cpp` file AND (for any project created after this
campaign shipped) automatically wires the one required call into that
project's own `RegisterProject()` function, with an auto-assigned,
collision-free priority so scaffolding more than one such pass into the same
project, any number of times, across any amount of realistic "delete this,
try a different name" churn, never manually requires touching a single line
of C++.

## The shape: eight phases, one new scaffold kind wired end-to-end

- **PHASE1** — new, fourth `AssetScaffoldKind::ScreenPostProcessPass`
  enumerator; `CreateAssetWindow.cpp`'s `TitleForKind()` gains a real case;
  `ProjectPanel.cpp`'s "Create" submenu gains a 4th `ImGui::MenuItem`.
  Verification was PARTIAL that session (a locked interactive desktop
  session structurally could not receive simulated mouse input) — fully,
  genuinely confirmed via real mouse input in PHASE6 and again in PHASE8. See
  `PHASE1_COMPLETION_REPORT.md`.
- **PHASE2** — `BuildGameStubCppContent()`'s "New Project" template gains two
  exact, stable anchor comments (`GTE_AUTO_REGISTER_FORWARD_DECLARATIONS`/
  `GTE_AUTO_REGISTER_ANCHOR`) and un-comments the `core` parameter — for
  NEWLY-CREATED projects only, going forward; every pre-existing project
  (including `Projects/ProjectAssemblyProbe/`) keeps its original,
  anchor-less template forever unless a human manually updates it. See
  `PHASE2_COMPLETION_REPORT.md`.
- **PHASE3** — `BuildScreenPostProcessPassCppContent(name, priority)`, the
  real, working, translucent-red-tint `.cpp` template generator, using a
  collision-safe two-token substitution scheme (`__NAME__` then
  `@@PRIORITY@@` — NOT the source design document's own naive
  `__NAME__`/`__PRIORITY__` sketch, which this phase's own adversarial-name
  test proved would corrupt a name legitimately containing the substring
  `"__PRIORITY__"`). See `PHASE3_COMPLETION_REPORT.md`.
- **PHASE4** — `TryAutoWireRegisterCall()`
  (`src/Editor/ScreenPassAutoWire.h/.cpp`), the auto-wire helper. Discovered
  and fixed, via `ask_questions`, two real deviations from this phase's own
  written assumptions: the anchor comments are 7-line wrapped blocks, not
  single lines, and insertion happens after the WHOLE comment block, not the
  literal next physical line. 9 new Tier-1 tests, including a
  commented-out-call-is-not-active-wiring regression proof. See
  `PHASE4_COMPLETION_REPORT.md`.
- **PHASE5** — `ComputeNextScreenPassPriority()`
  (`src/Editor/ScreenPassPriorityAssignment.h/.cpp`), the priority
  auto-assignment helper — "highest surviving `/*priority=*/` literal plus
  one", never a plain file count, so a manual delete-then-recreate cycle
  stays correct. 10 new Tier-1 tests, including the exact
  delete-middle-priority-then-recreate regression proof this design exists
  to protect against. See `PHASE5_COMPLETION_REPORT.md`.
- **PHASE6** — `CreateAssetScaffold()`'s new, dedicated,
  early-returning dispatch branch wiring PHASE1-5's own pieces together for
  the first time: the 63-byte length check, the priority computation, the
  file write, the auto-wire attempt, and the dynamic reminder message. First
  genuinely mouse-driven, end-to-end smoke test of the whole feature (the
  interactive desktop session was unlocked this time), confirming both the
  fresh-project auto-wire path and the old-project (`ProjectAssemblyProbe`/
  `HelloGame.cpp`) graceful-fallback path live. 5 new tests extending the
  real, pre-existing `tests/Editor/AssetScaffoldTemplateTests.cpp`. See
  `PHASE6_COMPLETION_REPORT.md`.
- **PHASE7** — `POST /project_assembly/create_asset`'s `kindParam` chain
  gains one more `else if`; the 400 error message lists all four valid
  kinds. 2 new tests extending the real, pre-existing
  `tests/Network/CreateAssetEndpointEndToEndTests.cpp` (plus one pre-existing
  test renamed, not duplicated, to reflect four valid kinds instead of
  three). See `PHASE7_COMPLETION_REPORT.md`.
- **PHASE8** (this phase) — the full clean build + full regression pass +
  a brand-new permanent fixture (`Projects/ScreenPassAutoWireProbe/`) +
  the literal Step 6 Definition-of-Done tick-through (14 items, every one
  confirmed with real, live evidence — screenshots, exact log excerpts, exact
  file reads, exact HTTP responses, never merely "code review says this
  should work") + documentation + this report, all at the very end, per
  every prior campaign's own established precedent. See
  `PHASE8_COMPLETION_REPORT.md`.

## Final verification numbers (PHASE8)

- **Full clean build**: `cmake --build build --target clean` (641 files
  removed) followed by `cmake --build build` — **623/623 steps succeeded,
  zero errors, zero warnings**.
- **Full `ctest -C Debug --output-on-failure`**: **2081 tests total, 100% of
  executed tests passing, 25 legitimate environment-gated skips** — up from
  `editor-core-separation-23`'s own documented 2055/25 baseline, a clean
  **+26 tests / +0 skips**, exactly matching this campaign's own new test
  cases across PHASE4 (9), PHASE5 (10), PHASE6 (5 new), and PHASE7 (2 new).
  No test failed at any point during this phase's own full-suite run — the
  "diagnose and fix it yourself"/`delegate_task` contingency was never
  triggered.
- **Final live, mouse-and-HTTP-driven verification (PHASE8)**: a Screen
  Post-Process Pass scaffolded into `Projects/ScreenPassAutoWireProbe/` (a
  fresh project created after PHASE2's template change shipped), compiled,
  and hot-reloaded — with ZERO manual C++ editing performed by the tester —
  is genuinely visible as a translucent red tint in a real
  `GET /get_game_view` screenshot; structurally confirmed via
  `GET /render_graph`'s `"is_project_feature":true` entries; survives 5
  back-to-back delete/recreate/compile/reload cycles with every single
  registration claiming the exact same bounded GPU-state slot (mirroring
  `editor-core-separation-23`'s own 20-cycle proof methodology); and
  `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` (the old-project
  fallback fixture) is confirmed byte-for-byte, 252-lines identical before
  and after scaffolding into it.

## The Entry Gate — now fully closed

**Every checkbox in the source design document's own Step 6 Definition-of-
Done checklist is independently, freshly confirmed TRUE** (see
`PHASE8_COMPLETION_REPORT.md`'s own full, evidenced tick-through) —
therefore the entire, 2-BIG-STEP "Project Assembly On-Screen Render Feature
Compositing" effort described by
`DESIGN_REQUIREMENTS_ONSCREEN_RENDER_PASS_COMPOSITING_2026-09-29.txt`/
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`/
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
is now closed for good. Unlike `editor-core-separation-23`'s own closing
report (which unblocked a FUTURE, separate BIG-STEP 2 campaign), there is no
further BIG-STEP left to unblock — this is the final phase of the final
campaign of this effort.

## Honest, permanent limitations (restated plainly, not silently smoothed over)

1. **This campaign does NOT retroactively rewrite any project created before
   PHASE2's template change shipped.** `Projects/ProjectAssemblyProbe/` and
   any other pre-existing project keep their own original, anchor-less
   `<ProjectName>Game.cpp` (or, for `ProjectAssemblyProbe` specifically,
   `HelloGame.cpp`) forever, unless a human manually adds the two anchors and
   un-comments `core` themselves. This is an explicit, permanent Non-Goal,
   not an oversight.
2. **Auto-wiring was never extended to the two older scaffold kinds**
   (`RenderPass`/`ComputeShader`) — both keep today's fully-manual,
   reminder-only behavior, unchanged, forever, until some future, separate
   campaign decides otherwise. `TryAutoWireRegisterCall()` itself is
   perfectly reusable for that future work (external linkage, generic
   `registerFunctionName` parameter) — this campaign simply never wires it
   into either of those two dispatch branches.
3. **The auto-assigned priority is scoped to a project's OWN Screen
   Post-Process Passes only** — it cannot see or coordinate against whatever
   priority a currently-loaded `gte_plugin_abi` plugin already occupies in
   the same `RenderFeatureStage::PostComposite` stage. A collision against a
   plugin is safe (logged, lexically tie-broken, never crashing — confirmed
   live during PHASE8's own item 10 verification, where `RedTint.ScreenTint`
   and the pre-existing `DemoRenderFeatureV3` plugin both legitimately
   declared priority 0), but this scaffolding tool has no visibility into it
   and cannot avoid it in advance. This is a structural property of the
   underlying `RenderFeatureCompositor` collision-detection design (BIG-STEP
   1), not a bug this campaign introduced or could fix without changing that
   system's own scope.
4. **No in-engine delete/rename action for a scaffolded asset file** —
   removing or renaming a generated `<Name>ScreenPass.cpp` (and its
   now-dangling forward declaration/call line, if auto-wired) remains a
   manual, off-engine file operation, exactly like every other asset this
   scaffolding system produces. `ComputeNextScreenPassPriority()` is
   deliberately designed to stay correct across exactly this manual
   delete-then-recreate pattern (proven live, repeatedly, in this
   campaign's own PHASE8 verification) without the engine ever needing to
   grow a delete/rename feature of its own to support it.
5. **`RenderFeatureStage`/`RenderFeatureBlendMode`/priority are never exposed
   as manual choices through the ImGui Create-Asset window** — every
   generated pass always uses `PostComposite` + `AlphaOver` +
   `RenderPassEvent::AfterEverything`, matching BIG-STEP 1's own hand-wired
   demo exactly. A project author who wants a different relative blend order
   across several such passes edits the generated `/*priority=*/` literal by
   hand — the same permanent, honest, off-engine-editing boundary every
   other "content/behavior" decision in this system already lives with.

None of these five limitations are regressions or open bugs in what this
campaign shipped — all five are deliberate, documented scoping decisions
carried over unchanged from the source design document's own explicit
Non-Goals section, restated here honestly rather than silently smoothed
over.

## Files changed across the whole campaign

- `src/Core/EditorCapabilities.h` (PHASE1 — `AssetScaffoldKind::
  ScreenPostProcessPass`)
- `src/Editor/CreateAssetWindow.cpp` (PHASE1 — `TitleForKind()` new case)
- `src/Editor/Panels/ProjectPanel.cpp` (PHASE1 — 4th `ImGui::MenuItem`)
- `src/Editor/EditorProjectLifecycleCapability.cpp` (PHASE2 — anchor
  comments + named `core` parameter in `BuildGameStubCppContent()`; PHASE3 —
  `BuildScreenPostProcessPassCppContent()`; PHASE6 — `CreateAssetScaffold()`'s
  new dispatch branch + two new `#include`s)
- `src/Editor/ScreenPassAutoWire.h`/`.cpp` (new, PHASE4)
- `src/Editor/ScreenPassPriorityAssignment.h`/`.cpp` (new, PHASE5)
- `src/Network/NetworkServer.cpp` (PHASE7 — `kindParam` chain + 400 message)
- `CMakeLists.txt` (PHASE4/PHASE5 — new file-pair entries in the `gte_editor`
  source list)
- `tests/CMakeLists.txt` (PHASE4/PHASE5 — new test-file entries)
- `tests/Editor/ScreenPassAutoWireTests.cpp` (new, PHASE4)
- `tests/Editor/ScreenPassPriorityAssignmentTests.cpp` (new, PHASE5)
- `tests/Editor/AssetScaffoldTemplateTests.cpp` (PHASE6 — extended, 5 new
  tests + one new fixture helper)
- `tests/Network/CreateAssetEndpointEndToEndTests.cpp` (PHASE7 — extended, 2
  new tests + one renamed)
- `Projects/ScreenPassAutoWireProbe/` (new, permanent fixture, PHASE8 —
  `.gitignore`d, never committed, by pre-existing `editor-core-separation-11`
  policy)
- `docs/conventions/project-assembly-system.md` (PHASE8 — corrected the
  existing `### On-screen Game View compositing` section's closing paragraph
  + new `### Screen Post-Process Pass scaffolding` section)
- `AGENTS.md` (PHASE8 — historical-accuracy note + new follow-up paragraph
  on the "Project Assembly On-Screen Render Feature Compositing" section)
- Every `PHASEn_COMPLETION_REPORT.md`/this `CAMPAIGN_COMPLETION_REPORT.md` in
  `task_manager/editor-core-separation-24/`

## Delegation and ambiguity summary across the whole campaign

- No `delegate_task` call was made in ANY phase of this campaign. PHASE6
  (the heaviest implementation phase) explicitly considered and declined a
  `position: "next"` self-double-check, reasoning that its own direct,
  in-line, mouse-driven end-to-end verification already exceeded the
  evidentiary bar a sub-agent review would independently re-establish.
  PHASE8's own full-suite run surfaced zero failures, so its own contingency
  `delegate_task` call was never triggered either.
- `ask_questions` was invoked in exactly one phase, PHASE4 — two related
  questions about the auto-wire helper's own anchor-matching and
  insertion-point logic, both resolved in favor of the smaller, more robust
  option (bare-token search instead of whole-sentence matching;
  insert-after-the-whole-comment-block instead of literally-the-next-line).
  No other phase, including PHASE8 itself, needed to ask a genuine question —
  every ambiguity PHASE8 encountered live (the `compile_only`-vs-`hot_reload`
  distinction for an already-loaded project, the exact reason
  `TryAutoWireRegisterCall()` fails against `ProjectAssemblyProbe/`) was
  resolved by direct investigation of the real, observed system behavior,
  not by a decision only a human could make.
- PHASE1 honestly disclosed a partial-verification gap (a locked interactive
  desktop session at the time) rather than silently working around it or
  inventing a temporary debug hook — that gap was fully closed by PHASE6's
  and PHASE8's own later, genuinely mouse-driven verification sessions.
