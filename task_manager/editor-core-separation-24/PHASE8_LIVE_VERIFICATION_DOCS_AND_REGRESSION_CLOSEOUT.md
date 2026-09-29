# PHASE8 — Live Verification, Docs, Full Regression, and Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full).
Design doc Step: Step 6 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
— **this phase restates and ticks EVERY box of that Step's own checklist,
with fresh, real evidence. Read that Step's own full text now, before doing
anything else in this phase.**

This is the FINAL phase of this campaign, and of the entire, 2-BIG-STEP
"Project Assembly On-Screen Render Feature Compositing" effort
(`editor-core-separation-23` + `editor-core-separation-24`). It is the ONLY
phase in this campaign allowed to run a full clean build and a full `ctest`
regression pass (Locked Decision 2, PHASE0).

## Step 1: The Goal

Prove, live, with real evidence (screenshots, exact log excerpts, exact file
reads, exact HTTP responses) — not merely "code review says this should
work" — that every one of the source document's own Step 6 checkboxes is
true, RIGHT NOW, against the fully-built, fully-wired feature every prior
phase in this campaign produced. Then document the capability permanently
(`docs/conventions/`, `AGENTS.md`), run the full regression suite, and write
the final `CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation

By the time this phase starts, PHASE1 through PHASE7 have each individually
compiled and passed their own targeted checks, but NO phase before this one
has:
- Run a full clean build.
- Run the full `ctest` regression suite.
- Proven the single most important claim of the whole campaign: a Screen
  Post-Process Pass scaffolded into a FRESH project, compiled, and reloaded
  — with ZERO manual C++ editing performed by the tester — is genuinely
  visible as a translucent red tint in a real Game View screenshot.
- Updated any permanent documentation.

Re-read every `PHASEn_COMPLETION_REPORT.md` in this folder (1 through 7)
before starting — collect every discovered deviation/renamed
function/adjusted design decision from each one into a single mental model of
what ACTUALLY got built, since this phase's own verification must be checked
against the REAL, final code, not this strategy document's own original
sketch. In particular, confirm the exact file names PHASE4/PHASE5 actually
used for `TryAutoWireRegisterCall()`/`ComputeNextScreenPassPriority()`
(planned as `src/Editor/ScreenPassAutoWire.h/.cpp` and
`src/Editor/ScreenPassPriorityAssignment.h/.cpp` respectively) and that
PHASE6/PHASE7 actually extended the two REAL, PRE-EXISTING test files this
campaign was told to reuse
(`tests/Editor/AssetScaffoldTemplateTests.cpp`,
`tests/Network/CreateAssetEndpointEndToEndTests.cpp`) rather than
accidentally creating parallel, duplicate ones.

## Step 3: The Plan

### 3.1 — Create the permanent, fresh test-project fixture

Per PHASE0's own Locked Decision 10: create a NEW, permanent Project Assembly
fixture (suggested name `Projects/ScreenPassAutoWireProbe/` — confirm this
name is free first; if taken, pick a clearly-related alternative and record
the final choice here) using the Editor's own "Create New Project" action (or
`POST /project_assembly/create_project`) — this guarantees it is created
AFTER PHASE2's template change, so it genuinely has both anchor comments and
`gte::Core& core` named, proving the TRUE auto-wire happy path (as opposed to
`Projects/ProjectAssemblyProbe/`, which pre-dates the anchors and can only
ever prove the fallback path).

### 3.2 — Work through the source document's own Step 6 checklist, item by
item, against REAL, live evidence:

1. Right-click the "[Active Project] `<Name>`" row's "Create" submenu shows
   "Screen Post-Process Pass..." as a fourth option; opening it shows
   "Create New Screen Post-Process Pass" as the window title.
2. Clicking it, typing a name, clicking "Create" writes exactly ONE new file,
   `Assets/<Name>ScreenPass.cpp`, matching PHASE3's template with `<Name>`
   substituted throughout and `<Priority>` substituted with `0` (the first
   Screen Post-Process Pass ever scaffolded into
   `ScreenPassAutoWireProbe`) — confirm both via the ImGui path AND the HTTP
   route (PHASE7).
3. Repeating with the SAME name (any case variant) is rejected, 400,
   "already exists" — zero files touched.
4. A name long enough that `"<Name>.ScreenTint"` would exceed 63 characters
   is rejected, 400, a clear "name is too long" message naming the actual
   limit — zero files touched, confirmed both via the ImGui path (error text
   appears in the Create-Asset window, window stays open) AND the HTTP
   route.
5. Scaffolding a SECOND Screen Post-Process Pass into `ScreenPassAutoWireProbe`
   (a different name, Create menu only, zero manual C++ editing) writes a
   second, independent `.cpp` file whose own `/*priority=*/` literal reads
   `1`, not `0` — confirmed by reading both generated files directly — AND
   confirmed LIVE that both entries blend correctly on screen with NO
   `RenderFeatureCompositor::SortAndDetectCollisionsInStage()` same-priority
   collision warning anywhere in the engine log (`GET /get_logs`).
6. Scaffolding THREE Screen Post-Process Passes into `ScreenPassAutoWireProbe`
   (priorities `0, 1, 2`), then deleting the MIDDLE one's `.cpp` file directly
   on disk (outside the Editor), then scaffolding a FOURTH pass via the
   Create menu — confirms the new file's own `/*priority=*/` literal reads
   `3` (one more than the highest surviving priority, `2`), never `2` again
   and never `1` — confirmed by reading the generated file directly, AND
   confirmed LIVE that all three surviving passes plus the new one blend
   correctly with no unexpected collision warning in the engine log.
7. The auto-wire happy path, on `ScreenPassAutoWireProbe` (a project created
   after PHASE2's template change shipped): scaffolding a Screen
   Post-Process Pass produces a `reminder_message` confirming auto-wiring
   succeeded, AND `Assets/ScreenPassAutoWireProbeGame.cpp` is confirmed,
   byte-for-byte, to now contain the correct forward declaration and call
   line, in the correct places, with NO other line in that file touched or
   reformatted (`read_file`, compare against what it looked like right after
   project creation).
8. The graceful-fallback path, on `Projects/ProjectAssemblyProbe/` (an OLD
   project, pre-dating the anchors): scaffolding a Screen Post-Process Pass
   into it produces the fallback reminder message. **Confirmed, real naming
   gotcha (also documented in PHASE6's own Step 5) — do not get this wrong
   here**: this project's own Game-half source file is
   `Assets/HelloGame.cpp`, NOT `Assets/ProjectAssemblyProbeGame.cpp` (which
   has never existed — this project predates the `<Name>Game.cpp` naming
   convention `CreateNewProjectAssembly()` established). `read_file`
   `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` and confirm it is
   COMPLETELY UNMODIFIED — zero corruption, zero partial insertion, zero
   crash, and (obviously) confirm no new `ProjectAssemblyProbeGame.cpp` file
   was ever created either. Delete the throwaway generated `.cpp` from
   `ProjectAssemblyProbe/Assets/` afterward so this OTHER permanent fixture
   (BIG-STEP 1's own) is left clean, exactly as it was before this check
   (mirroring PHASE6's own cleanup discipline).
9. Idempotency: running the SAME scaffold-then-auto-wire sequence a second
   time for a DIFFERENT new name inserts only ITS OWN forward declaration/
   call (never duplicates an earlier one) — already covered by PHASE4's own
   Tier-1 tests, but re-confirm live, once, against `ScreenPassAutoWireProbe`'s
   own real file for good measure. Separately, confirm that if a PREVIOUSLY
   auto-wired call line is manually commented out afterward and the SAME
   scaffold-create flow is invoked again for that same name (a fresh "Create"
   with the identical name is rejected as "already exists" — so this specific
   re-wire check should instead directly exercise `TryAutoWireRegisterCall()`'s
   own already-tested idempotency logic, PHASE4 — this live check is about
   confirming the REAL project file behaves exactly like PHASE4's test fixture
   predicted, not about re-deriving new logic here).
10. **The literal acceptance test — the single most important checkbox in
    this entire campaign.** Zero manual editing of `RegisterProject()`
    performed by the tester: scaffold a Screen Post-Process Pass on
    `ScreenPassAutoWireProbe`, trigger a real compile
    (`POST /project_assembly/debug/compile_only?name=ScreenPassAutoWireProbe`
    or the equivalent Editor UI action), then either relaunch the Editor or
    use the existing `POST /project_assembly/hot_reload?name=ScreenPassAutoWireProbe`
    route — and confirm, via a real screenshot
    (`gte_send_request("/get_game_view")`), the translucent red tint is
    genuinely visible in the running Editor's actual Game View panel. Save
    the screenshot evidence description (pixel region, expected vs. actual
    tint) in the completion report.
11. Iteration realism check: on `ScreenPassAutoWireProbe`, delete a
    scaffolded `.cpp` file (outside the Editor), remove its now-dangling
    forward declaration/call line from `ScreenPassAutoWireProbeGame.cpp` by
    hand, then scaffold a DIFFERENTLY-NAMED Screen Post-Process Pass, compile,
    and reload — repeat this at least FIVE times in the same running Editor
    session. Confirm this ordinary "I don't like this name, let me redo it"
    workflow never fails, never leaves a stale/duplicate render feature entry
    behind (`GET /render_graph`), and never trips BIG-STEP 1's own
    `kMaxConcurrentProjectRenderFeatures = 16` project-feature slot cap under
    this realistic amount of churn (confirm the actually-consumed slot count
    stays bounded/reused across the five cycles, mirroring
    `editor-core-separation-23`'s own PHASE6 slot-reuse proof methodology).
12. `docs/conventions/project-assembly-system.md` gains a new,
    SEPARATE sub-section (per PHASE0's own Locked Decision 9 — placed
    immediately after the existing `### On-screen Game View compositing`
    section, NOT rewriting it in place), documenting: the new
    `AssetScaffoldKind::ScreenPostProcessPass` scaffold kind, the generated
    template's own shape, the auto-wire mechanism's own honest
    fresh-project-vs-old-project boundary (with an explicit callout that
    `Projects/ProjectAssemblyProbe/` is a permanent example of the
    old-project fallback path — noting its own `HelloGame.cpp` naming
    quirk explicitly, so a future reader is not confused the same way this
    campaign's own strategy files almost were — and
    `Projects/ScreenPassAutoWireProbe/` — or whatever this phase's own
    fixture ended up named — is a permanent example of the fresh-project
    auto-wire path), and the priority auto-assignment scheme's own "highest
    surviving + 1" rule. Suggested heading: `### Screen Post-Process Pass
    scaffolding — Editor "Create" menu + auto-wire (BIG-STEP 2)`.

    **Also required, a small, targeted, IN-PLACE fix to the EXISTING
    `### On-screen Game View compositing` section itself** (this is
    additive/corrective, not a conflict with Locked Decision 9's "do not
    rewrite that section in place" rule, which is about not duplicating this
    phase's own NEW content into the old section — it does not license
    leaving a now-FALSE claim standing right next to a brand-new section that
    contradicts it). That section's own current closing paragraph (the doc's
    own real, current line ~393-399 at the time these strategy files were
    written — re-confirm the exact current line numbers before editing, this
    file may have grown since) reads:

    > **Explicitly, still BIG-STEP 1 only — there is no Editor UI menu item
    > for this yet.** A Project Assembly author must call
    > `Core::RegisterProjectRenderFeature()` directly from their own
    > `RegisterProject()`; a future, separate campaign (BIG-STEP 2) adds a
    > "Create → Screen Post-Process Pass" Editor menu item/scaffold template
    > on top of this same, already-proven mechanism.

    This is now FALSE the moment this campaign ships — leaving it standing,
    completely unchanged, directly above a brand-new sub-section that
    describes BIG-STEP 2 as fully shipped, would be a direct,
    reader-visible self-contradiction in the same document. Replace just this
    one paragraph with an honest, current statement (e.g. "BIG-STEP 1
    shipped this on-screen compositing mechanism; BIG-STEP 2
    [`editor-core-separation-24`] then added the Editor 'Create → Screen
    Post-Process Pass' menu item and its own auto-wire mechanism on top of it
    — see the dedicated section immediately below") — a one-paragraph patch,
    not a rewrite of the section's own substantial technical content above it
    (the API shape, the bounded-slot design, the hot-reload teardown
    guarantee, etc. all remain untouched, exactly per Locked Decision 9).
13. `AGENTS.md`'s own existing "Project Assembly On-Screen Render Feature
    Compositing" section (the paragraph citing `editor-core-separation-23`)
    gains a follow-up paragraph, mirroring this same file's own established
    style for a follow-up campaign (e.g. how the "Render Pass System"
    section's own multi-paragraph history reads), stating that BIG-STEP 2
    (`editor-core-separation-24`) is now also complete, briefly summarizing
    the scaffolding tool + auto-wire mechanism, and stating plainly that the
    entire 2-BIG-STEP effort is now closed.
14. Full existing regression suite (`ctest -C Debug --output-on-failure`)
    still passes, unchanged total count plus this campaign's own new tests
    (PHASE4's `TryAutoWireRegisterCall()` suite, PHASE5's
    `ComputeNextScreenPassPriority()` suite, PHASE6's extension of
    `AssetScaffoldTemplateTests.cpp`, PHASE7's extension of
    `CreateAssetEndpointEndToEndTests.cpp`, plus anything else any phase's own
    completion report says it added), 100% passing.

### 3.3 — Full clean build + full regression (the ONLY phase allowed to do this)

1. `cmake --build build --target clean`, then `cmake --build build` — record
   the exact step count and confirm zero errors/warnings, mirroring every
   prior campaign's own closeout convention. If either new PHASE4/PHASE5
   `src/Editor/*.cpp` file was somehow never actually registered in the root
   `CMakeLists.txt`'s explicit source list, THIS is where that mistake would
   surface as a link error (an undefined reference to
   `TryAutoWireRegisterCall`/`ComputeNextScreenPassPriority`) — do not treat
   that as a mysterious new bug if it happens; it means an earlier phase's own
   `CMakeLists.txt` edit was missed or reverted, go fix that phase's own diff
   directly.
2. `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`
   — record the exact test count/skip count, and compare against
   `editor-core-separation-23`'s own final baseline (2055 tests, 25
   environment-gated skips) — this campaign's own total should be that
   baseline plus every new test this campaign's own phases added, with 100%
   of executed tests passing.
3. **If anything fails** (a full build error, a regression test failure): do
   NOT attempt to silently work around it by loosening a test's expectation.
   Diagnose the real root cause first (read the exact compiler error/test
   failure output, use `GET /get_logs` if a live-behavior test is involved),
   then use `delegate_task(position: "next")` to fix whatever is broken —
   instruct that sub-task to use `ask_questions` for any ambiguity it finds,
   and to report back before this phase's own completion report is written.
   This is the one place in the whole campaign where Note 4 (no full
   build/test per phase) is deliberately, explicitly overridden — this
   phase's own job description IS to run the full build/test.

### 3.4 — Write the final `CAMPAIGN_COMPLETION_REPORT.md`

Mirroring `editor-core-separation-23`'s own `CAMPAIGN_COMPLETION_REPORT.md`
structure exactly (goal, phase-by-phase shape summary, final verification
numbers, the Entry Gate status — this time stating the ENTIRE 2-BIG-STEP
effort is closed, not merely unblocking a "BIG-STEP 2" that no longer exists
as future work — honest permanent limitations restated plainly, files
changed, delegation/ambiguity summary across the whole campaign).

## Step 4: Completion

Write `PHASE8_COMPLETION_REPORT.md` AND `CAMPAIGN_COMPLETION_REPORT.md` in
this same folder. `git_add` + `git_commit` covering every doc change and both
reports (and any last-minute code fix from a delegated sub-task in 3.3).
