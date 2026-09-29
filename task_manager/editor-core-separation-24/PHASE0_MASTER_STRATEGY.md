# PHASE0 — MASTER STRATEGY: "Project Assembly On-Screen Render Feature Compositing" — BIG-STEP 2 (Editor Integration)

Campaign folder: `task_manager/editor-core-separation-24/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

Source design documents (read-only, in a SEPARATE repo/folder — never modify these):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-4\`
  - `DESIGN_REQUIREMENTS_ONSCREEN_RENDER_PASS_COMPOSITING_2026-09-29.txt` — the
    "why"/goal document. Read it once for background; not re-cited step by
    step below.
  - `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`
    — BIG-STEP 1. **Already fully shipped and closed** by campaign
    `editor-core-separation-23` (`task_manager/editor-core-separation-23/CAMPAIGN_COMPLETION_REPORT.md`).
    Its own "Entry Gate" (Step 9 checklist) is independently confirmed TRUE —
    this is exactly why this campaign is allowed to exist at all. Do not
    re-verify BIG-STEP 1 itself; treat `Core::RegisterProjectRenderFeature()`/
    `UnregisterProjectRenderFeature()` as a stable, already-proven foundation.
  - `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
    — **THIS campaign's own source document.** Every phase below cites this
    file's own Step numbers (Step 1 through Step 6); re-read the cited Step in
    full before starting that phase — this master file and its children
    summarize and sequence that document, they do not replace it.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE8_*.md`) must be read together with this file. Every child phase must,
at its own start, re-read this file plus the completion report of the phase
immediately before it (`PHASEn-1_COMPLETION_REPORT.md`, written by that
phase) — there might be a clue for continuation, a discovered root cause, or
a locked decision that changes how the next phase must be carried out.

---

## Step 1: The Goal (Where are we going?)

Today, a Project Assembly author who wants an on-screen render feature must
hand-write a call to `Core::RegisterProjectRenderFeature()` themselves, inside
their own `RegisterProject()` function, with zero Editor support — that is
exactly what BIG-STEP 1's own permanent proof fixture
(`Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s `"ProjectAssemblyProbe.ScreenTint"`
feature) still requires.

This campaign closes that gap for good. When it is done, a Project Assembly
author can:

1. Right-click the "[Active Project] `<Name>`" row in the Project panel (or
   call `POST /project_assembly/create_asset?kind=screen_post_process_pass`),
   choose "Screen Post-Process Pass...", type a name, and click "Create".
2. Get exactly one new, real, immediately-compileable file,
   `Assets/<Name>ScreenPass.cpp`, that already calls
   `Core::RegisterProjectRenderFeature()` correctly (a translucent red tint,
   the same kind of minimal-but-real starting point every other scaffold kind
   already provides).
3. On a project created after this campaign ships, have that new file's own
   `Register<Name>ScreenPass(core);` call **automatically inserted** into
   their `RegisterProject()` function — zero manual C++ editing required to
   see it live, end to end: compile, hot-reload (or relaunch), and the tint is
   genuinely visible in the running Editor's Game View.
4. Create a SECOND (or third, fourth, ...) Screen Post-Process Pass into the
   SAME project, with each one landing on a distinct, auto-assigned
   `RenderFeatureStage::PostComposite` priority, so they never silently
   collide — even after deleting one from disk and creating a new one, any
   number of times, in the same live Editor session.
5. On a project created BEFORE this campaign ships (e.g.
   `Projects/ProjectAssemblyProbe/`), still get the new file safely, with a
   clear, honest fallback message explaining that the one remaining step
   (adding the call by hand) must be done manually for that one project only,
   forever — never a corrupted or partially-written `<ProjectName>Game.cpp`.

This is BIG-STEP 2 of 2 — the FINAL phase of the whole "Project Assembly
On-Screen Render Feature Compositing" effort. When this campaign's own
Definition of Done (Step 6 below, mirroring the source document's own Step 6)
is green, the entire 2-BIG-STEP effort is closed for good, exactly like
`editor-core-separation-15` closed the 4-BIG-STEP "Project Assembly Hot
Reload" effort before it.

## Step 2: The Situation (Where are we now?)

Every file this campaign touches was re-read fresh, directly, immediately
before these phase files were written, and matches the source design
document's own citations — no drift found:

- `src/Core/EditorCapabilities.h` line 360 — `enum class AssetScaffoldKind {
  RenderPass, ComputeShader, ShaderPair };` — exactly 3 values today, no
  `ScreenPostProcessPass` yet. `IAssetScaffoldingCapability::CreateAssetScaffold(kind, name)`
  (line 381) is the one dispatch entry point, already wired to BOTH callers.
- `src/Editor/CreateAssetWindow.cpp` — `TitleForKind()` (lines 11-19) is an
  exhaustive `switch` with NO `default:` case, and a trailing, unreachable
  `return "Create New Asset";` after the switch (silences the "not all
  control paths return a value" warning this codebase's own toolchain would
  otherwise emit — `-Wswitch` itself is NOT enabled, confirmed).
- `src/Editor/Panels/ProjectPanel.cpp`, `RenderActiveProjectAssetsRow()`
  (lines ~248-269) — the right-click "Create" submenu has exactly 3
  `ImGui::MenuItem` entries today (Render Pass, Compute Shader,
  Vertex/Fragment Shader Pair), each setting
  `ctx.createAssetWindowPendingKind` + `ctx.createAssetWindowOpen = true`.
- `src/Editor/EditorProjectLifecycleCapability.cpp` — `BuildGameStubCppContent()`
  (lines 59-82) is the CURRENT "New Project" template — `RegisterProject(gte::Core&
  /*core*/)` with the parameter commented out, NO anchor comments anywhere.
  `BuildScaffoldFileSpecs()` (lines 250-267) and `ReminderMessageForKind()`
  (lines 269-280) are both exhaustive `switch`es with NO `default:` case and a
  trailing fallback `return`/`return {}` — confirmed safe to leave with no new
  case for the new enumerator (it is never reached for
  `ScreenPostProcessPass`, since `CreateAssetScaffold()` special-cases that
  kind BEFORE these two helpers are ever called — see PHASE6 below).
  `CreateAssetScaffold()` itself (lines 587-645) is the one dispatch method
  this campaign's PHASE6 adds a new, early-returning branch to, ahead of its
  existing generic flow.
- `src/Network/NetworkServer.cpp` lines 1367-1401 —
  `POST /project_assembly/create_asset` already has a plain
  `if (kindParam == "render_pass") {...} else if (...) {...} else { 400 }`
  chain, directly inline inside the route lambda (no separate parsing
  function), and already forwards `outcome.reminderMessage`/`outcome.createdFiles`
  generically — a NEW `kindParam` branch plus an updated 400 message is the
  entire HTTP-side change needed (PHASE7).
- `src/Core/Core.h` lines 391-401 — `Core::RegisterProjectRenderFeature(const
  char* debugName, RenderFeatureStage stage, RenderFeatureBlendMode blendMode,
  std::int32_t priority, ProjectRenderFeatureCallback callback)` and
  `UnregisterProjectRenderFeature(const char* debugName)` both exist, both
  compile, both are already live-verified (BIG-STEP 1) — this campaign calls
  them from GENERATED code, never modifies either signature.
- `plugins/gte_plugin_abi/RenderFeatureDescriptor.h` — `GtePluginRenderFeatureDescriptor::name`
  is a fixed `char[64]` (63 usable bytes); `RenderFeatureStage::PostComposite`/
  `RenderFeatureBlendMode::AlphaOver` are both real, wired, live values.
  `RenderFeatureCompositor::SortAndDetectCollisionsInStage()`
  (`src/Core/Plugins/RenderFeatureCompositor.h` line 296,
  `RenderFeatureCompositor.cpp` line 183) is the real, existing collision
  detector this campaign's auto-assigned priority scheme exists to avoid
  tripping unnecessarily.
- `docs/conventions/project-assembly-system.md` lines 289-363 — the
  `### On-screen Game View compositing` section was ALREADY fully rewritten by
  `editor-core-separation-23`'s own PHASE7 to describe BIG-STEP 1 (it is not
  the stale "confirmed NOT safe today" text the source design document's own
  Step 6 checklist describes striking through — that strikethrough already
  effectively happened, via a full rewrite rather than a literal `~~strike~~`
  annotation, during the PRIOR campaign). This campaign's own PHASE8 adds a
  NEW, separate sub-section immediately after it (Locked Decision 9 below) —
  it does not need to re-strike anything.

## Step 3: The Plan (detailed strategy)

This campaign is split into 8 implementation phases, each its own `.md` file
in this same folder, plus this PHASE0 orchestrator.

| Phase | File | Design doc Step(s) | One-line summary |
|---|---|---|---|
| 1 | `PHASE1_SCAFFOLD_KIND_ENUM_AND_EDITOR_UI_ENTRY_POINTS.md` | Step 2 | New `AssetScaffoldKind::ScreenPostProcessPass` enumerator; `CreateAssetWindow.cpp`'s `TitleForKind()` gains a real case; `ProjectPanel.cpp`'s "Create" submenu gains a 4th `ImGui::MenuItem`. |
| 2 | `PHASE2_PROJECT_TEMPLATE_AUTO_WIRE_ANCHORS.md` | Step 2B | `BuildGameStubCppContent()`'s template gains two anchor comments and un-comments the `core` parameter — for NEWLY-CREATED projects only, going forward. |
| 3 | `PHASE3_GENERATED_SCREEN_POST_PROCESS_PASS_TEMPLATE.md` | Step 3 | `BuildScreenPostProcessPassCppContent(name, priority)` — the real, working, translucent-red-tint `.cpp` template generator. |
| 4 | `PHASE4_AUTO_WIRE_HELPER_TRY_AUTO_WIRE_REGISTER_CALL.md` | Step 3B | `TryAutoWireRegisterCall()` — reads the target `<ProjectName>Game.cpp`, inserts the forward declaration + call line at the two anchors, idempotently, or safely refuses if either anchor is missing. |
| 5 | `PHASE5_PRIORITY_AUTO_ASSIGNMENT_HELPER.md` | Step 4 (priority sub-section) | `ComputeNextScreenPassPriority(assetsDirectory)` — scans sibling `*ScreenPass.cpp` files for the highest surviving `/*priority=*/` literal and returns one more than that. |
| 6 | `PHASE6_CREATE_ASSET_SCAFFOLD_DISPATCH_BRANCH.md` | Step 4 | `CreateAssetScaffold()`'s new, dedicated, early-returning branch for `ScreenPostProcessPass` — the 63-byte length check, the priority computation, the file write, the auto-wire attempt, and the dynamic reminder message — wiring PHASE1-5's own pieces together into one real, working feature for the first time. |
| 7 | `PHASE7_HTTP_ROUTE_UPDATE.md` | Step 5 | `POST /project_assembly/create_asset`'s `kindParam` chain gains one more `else if`; the 400 error message lists all four valid kinds. |
| 8 | `PHASE8_LIVE_VERIFICATION_DOCS_AND_REGRESSION_CLOSEOUT.md` | Step 6 (the full Definition of Done) | Every live verification checkbox from the source document's own Step 6, a NEW permanent test-project fixture proving the fresh-project auto-wire happy path, a new `docs/conventions/project-assembly-system.md` sub-section, an `AGENTS.md` update, full clean build, full `ctest` regression, `CAMPAIGN_COMPLETION_REPORT.md`. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

1. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any NEW
   diagnostic or permanent code in this campaign.** Always
   `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR` (`src/Core/Logging.h`),
   retrieved via `GET /get_logs` (`gte_send_request`). This mirrors every
   prior campaign's own convention and is a hard, repo-wide rule
   (`AGENTS.md`'s own "Logging" section), not new to this campaign.
2. **Never run a full clean build or full `ctest` regression pass except in
   PHASE8.** Every other phase uses an INCREMENTAL build (`cmake --build
   build`) as its compile-check gate, plus a TARGETED `ctest -R <filter>` for
   whatever new Tier-1 test(s) that phase itself adds. Use
   `run_app_background`/`gte_send_request`/`stop_app_background` for any live,
   HTTP-driven debugging a phase genuinely needs (e.g. `GET /get_logs`,
   `GET /render_graph`, `GET /get_game_view`) — never a blind guess.
3. **`AssetScaffoldKind`'s existing three call sites with no `default:` case
   are a KNOWN, DELIBERATE, load-bearing property of this codebase** (this
   codebase enables no `-Wswitch`) — do not "fix" this by adding a
   `default:` anywhere, and do not assume a missing case is a bug. PHASE1
   adds a REAL case only where the design document says one is genuinely
   required (`TitleForKind()`); PHASE6 explains exactly why
   `BuildScaffoldFileSpecs()`/`ReminderMessageForKind()` need no new case at
   all (the new kind is special-cased earlier, in its own branch, and never
   reaches either helper).
4. **A `debugName`/human-typed name that would exceed
   `GtePluginRenderFeatureDescriptor::name`'s 63-usable-byte limit is REJECTED
   at scaffold time, before any file is written — never silently truncated,
   and never merely deferred to compile-and-run time.** This mirrors BIG-STEP
   1's own `Core::RegisterProjectRenderFeature()` reject-not-truncate
   precedent; PHASE6 implements the scaffold-time half of this rule.
5. **The auto-assigned priority is computed by scanning the HIGHEST SURVIVING
   `/*priority=*/` literal actually present among sibling `*ScreenPass.cpp`
   files and adding one — NEVER by counting how many such files currently
   exist.** A plain file count silently breaks the moment any sibling file is
   deleted outside the Editor and a new one is then created (see PHASE5 for
   the full reasoning and its own required regression test covering exactly
   this scenario).
6. **The auto-wire mechanism (`TryAutoWireRegisterCall()`, PHASE4) is
   ALL-OR-NOTHING and NEVER partially writes a target file.** Either both
   required anchor comments are found and both insertions are made (or were
   already present — a safe, idempotent no-op), or the target file is left
   completely, byte-for-byte untouched and the caller falls back to a manual
   reminder message. A commented-out, previously-auto-wired call line (a
   human manually disabling one effect) must be correctly treated as NOT
   currently wired, never mistaken for still-active — PHASE4's own dedicated
   regression test proves this exact case.
7. **This campaign does NOT retroactively rewrite any project created before
   PHASE2's template change ships** (explicit Non-Goal, source document) —
   `Projects/ProjectAssemblyProbe/` and every other pre-existing project keep
   their own original, anchor-less `<ProjectName>Game.cpp` forever unless a
   human manually adds the two anchors and un-comments `core` themselves.
   PHASE8's own live verification deliberately exercises BOTH the fresh-project
   auto-wire path AND the old-project graceful-fallback path using two
   DIFFERENT projects for exactly this reason.
8. **This campaign does NOT extend auto-wiring to `RenderPass`/`ComputeShader`**
   (explicit Non-Goal, source document) — both keep today's fully-manual,
   reminder-only behavior, unchanged, forever, until some future, separate
   campaign decides otherwise.
9. **PHASE8's documentation update ADDS a new, separate sub-section to
   `docs/conventions/project-assembly-system.md`, immediately after the
   existing `### On-screen Game View compositing` section — it does not
   rewrite that existing section in place.** (Decided by the strategist for
   this campaign, in the user's absence, favoring a clean, additive,
   BIG-STEP-1-vs-BIG-STEP-2 separation that mirrors this same document's own
   existing convention of one section per capability increment — e.g. its
   separate `## Hot Reload` and `## Creating a New Project` sections.)
10. **PHASE8's new fresh-project test fixture is a PERMANENT, new regression
    fixture, kept forever on disk (never deleted after verification) —
    mirroring `Projects/ProjectAssemblyProbe/`'s own precedent as BIG-STEP 1's
    permanent proof artifact.** (Decided by the strategist for this campaign,
    in the user's absence, favoring long-term regression value over a tidier
    but throwaway `Projects/` folder — `Projects/` is `.gitignore`d either
    way, so this decision has zero git-visible footprint.) Suggested name:
    `Projects/ScreenPassAutoWireProbe/` — confirm this name is actually free
    (not already used by any other campaign) at the start of PHASE8; if
    taken, pick a clearly-related alternative and record the final choice in
    that phase's own completion report.
11. **Every phase that changes `gte_editor`/`gte_core`-tier logic must add or
    extend a Tier-1 test wherever the underlying problem allows it**
    (`AGENTS.md`'s own "Testability & Regression Safety" section) —
    `TryAutoWireRegisterCall()` (PHASE4) and `ComputeNextScreenPassPriority()`
    (PHASE5) are BOTH pure, plain-data functions (a filesystem path in, a
    bool/int out) and are EXCELLENT Tier-1 candidates; each phase file below
    spells out the exact test cases the source document's own Step 6 checklist
    requires.
12. **Every phase must end with**: an incremental compile check succeeding, a
    targeted `ctest` pass for that phase's own new/changed tests (where
    applicable), a `.md` completion report (`PHASEn_COMPLETION_REPORT.md`)
    written into this same folder, and a git commit (`git_add` + `git_commit`)
    covering both the code change and the report.
13. **Whenever a phase discovers a genuine design ambiguity or a decision only
    a human can make, it MUST use `ask_questions` before proceeding** — every
    phase file below calls out its own likely decision points explicitly, but
    an implementer must use `ask_questions` for ANY other genuine ambiguity it
    personally discovers too. Every task an implementation phase itself
    delegates must ALSO be instructed to use `ask_questions` for its own
    ambiguities.
14. **Implementation-phase agents may use `delegate_task` ONLY with
    `position: "next"`, and ONLY to double-check their OWN just-finished,
    large piece of work before writing that phase's completion report** (e.g.
    PHASE6 and PHASE8 are the two heaviest phases in this campaign and are the
    most likely candidates). Such a sub-task double-check must NEVER create
    its own separate report file — it reports back inline, and the ORIGINAL
    phase still writes the one `PHASEn_COMPLETION_REPORT.md`. No phase may
    delegate an ENTIRELY DIFFERENT phase's work ahead of schedule, and no
    phase may use `position: "end"`.
15. **`RenderFeatureStage::PostComposite` + `RenderPassEvent::AfterEverything`
    is the ONLY stage/event pair the generated template (PHASE3) ever uses** —
    mirroring BIG-STEP 1's own hand-wired demo exactly; no phase in this
    campaign exposes stage/blend-mode/event choice through the ImGui window or
    HTTP route (explicit Non-Goal, source document).
16. **`TryAutoWireRegisterCall()` (PHASE4) and `ComputeNextScreenPassPriority()`
    (PHASE5) MUST each be declared in their own new header with EXTERNAL
    linkage** (`src/Editor/ScreenPassAutoWire.h`/`.cpp` and
    `src/Editor/ScreenPassPriorityAssignment.h`/`.cpp` respectively) — NEVER
    inside `EditorProjectLifecycleCapability.cpp`'s own anonymous namespace.
    This is not a style preference: `tests/Editor/AssetScaffoldTemplateTests.cpp`
    (a REAL, PRE-EXISTING file, confirmed present) already documents, in its
    own header comment, that every existing template-builder helper in that
    anonymous namespace has INTERNAL linkage and is uncallable from a separate
    test `.cpp` file — PHASE4/PHASE5 each require a brand-new, DEDICATED test
    file that calls these two functions directly, which is only possible with
    external linkage. Both new header/source pairs must be added to the root
    `CMakeLists.txt`'s explicit (non-glob) `gte_editor` source list, and both
    new test files must be added to `tests/CMakeLists.txt`'s equally explicit
    (non-glob) list — forgetting either produces no error of any kind, the
    new code/tests simply never compile/run.
17. **PHASE6/PHASE7/PHASE8 each reuse a REAL, PRE-EXISTING test file — never
    create a competing, parallel one for the same route/capability.** PHASE6
    extends `tests/Editor/AssetScaffoldTemplateTests.cpp` (already covers
    `CreateAssetScaffold()` for the three pre-existing kinds); PHASE7 extends
    `tests/Network/CreateAssetEndpointEndToEndTests.cpp` (already covers
    `POST /project_assembly/create_asset` end to end, including an existing
    `InvalidKindReturns400NamingTheThreeValidValues` test this campaign's own
    4th kind requires updating, not ignoring). Confirm both files still exist
    under those exact names before assuming otherwise — an earlier draft of
    this campaign's own strategy files incorrectly asserted "the complete
    absence of any NetworkServer-route-level unit test file anywhere in
    tests/", which was never true.
18. **`Projects/ProjectAssemblyProbe/`'s own Game-half source file is named
    `Assets/HelloGame.cpp`, NOT `Assets/ProjectAssemblyProbeGame.cpp`** — it
    predates the `<Name>Game.cpp` naming convention `CreateNewProjectAssembly()`
    established. Any phase that verifies the old-project fallback path against
    this specific fixture (PHASE6, PHASE8) must read/cite `HelloGame.cpp`, not
    a `ProjectAssemblyProbeGame.cpp` that has never existed —
    `TryAutoWireRegisterCall()` will correctly, harmlessly fail against this
    fixture via its own "file cannot be opened" branch (the target path it
    computes does not exist at all), not the "anchors missing" branch — same
    safe observable outcome, different, confirmed real reason.

### 3.2 — Why this shape (eight phases, not fewer/more)

- PHASE1 is deliberately the smallest, lowest-risk, purely-additive UI/enum
  change — it must compile and be manually clickable (even though the actual
  scaffold write is still a no-op behind it until PHASE6 lands) before any
  heavier logic is attempted. This mirrors `editor-core-separation-23`'s own
  PHASE1/PHASE2 split rationale: get the shape right and compiling first,
  then add the real logic behind it.
- PHASE2 is separate because it touches a DIFFERENT template
  (`BuildGameStubCppContent()`, the "New Project" scaffold) with its own,
  independent hazard (an honest, permanent, "every project created before this
  ships never gets these anchors" consequence) that has nothing to do with the
  new scaffold KIND itself.
- PHASE3, PHASE4, and PHASE5 are three separate, independently-testable, PURE
  functions (a template-string builder, a file-patching helper, a
  priority-scanning helper) — each deserves its own focused phase and its own
  focused Tier-1 test suite, exactly like `editor-core-separation-23` split
  its own PHASE1/PHASE2/PHASE3 along equally clean, independent-class
  boundaries. Debugging an idempotency bug in the auto-wire helper is much
  easier when it is not tangled together with a fresh priority-arithmetic bug
  in a sibling function written in the same sitting.
- PHASE6 is its own phase because it is qualitatively different work — not a
  new pure function, but the actual WIRING of every earlier phase's pieces
  into `CreateAssetScaffold()`'s real dispatch body, the single place all the
  earlier phases' correctness actually gets exercised together for the first
  time. This is the single heaviest phase in this campaign and the most
  likely candidate for a `position: "next"` self-double-check
  (Locked Decision 14).
- PHASE7 is separate because it is a different FILE entirely
  (`NetworkServer.cpp`, `gte_core`-tier network routing) with a trivial,
  mechanical, low-risk diff — it does not need to be tangled into PHASE6's
  own, much larger diff and review surface.
- PHASE8 is always last, matching every prior campaign in this codebase's own
  history — full regression + docs + the literal Step 6 Definition-of-Done
  tick-through, once, at the end, never spread across phases. It is also the
  ONLY phase that creates new, real, on-disk Project Assembly fixtures and
  performs true end-to-end HTTP-driven live proof, so it is kept separate from
  every phase that is still just building the underlying mechanism.

### 3.3 — Definition of Done for the whole campaign

Identical to Step 6 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
— PHASE8 restates the full checklist and ticks every box with its own fresh
evidence. A full clean incremental build and a full `ctest` regression pass
(100% of executed tests, test count only ever growing) both succeed, and every
one of that Step 6 checklist's items is explicitly, individually confirmed
true — including the single most important one: a Screen Post-Process Pass
scaffolded into a FRESH project, compiled, and reloaded with **zero manual
C++ editing performed by the tester**, is genuinely visible as a translucent
red tint in a real Game View screenshot. When this is green, the entire
2-BIG-STEP "Project Assembly On-Screen Render Feature Compositing" effort
(`editor-core-separation-23` + `editor-core-separation-24`) is closed for
good.
