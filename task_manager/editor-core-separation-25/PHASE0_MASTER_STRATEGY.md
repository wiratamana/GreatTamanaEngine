# PHASE0 — MASTER STRATEGY: "Core/Editor Separation: PassRecord Debug Metadata Sink"

Campaign folder: `task_manager/editor-core-separation-25/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

Source design document (read-only, in a SEPARATE repo/folder — never modify it):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-5\BIG_STEP_1_CORE_EDITOR_SEPARATION_PASSRECORD_METADATA_SINK_2026-09-29.txt`
— this is BIG STEP 1 OF 4 of a larger series (Buffer Roots/Blit, Persistent
Resource Cache, GPU Memory Aliasing are Steps 2-4 — NOT part of this
campaign, no dependency either direction). Read the source document in full
before starting ANY phase — this file and its children summarize/sequence
it, they do not replace it.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE5_*.md`) must be read together with this file. Every child phase must,
at its own start, re-read this file plus the completion report of the phase
immediately before it (`PHASEn-1_COMPLETION_REPORT.md`) — there might be a
clue for continuation, a discovered root cause, or a locked decision that
changes how the next phase must be carried out.

---

## Step 1: The Goal (Where are we going?)

Today, `PassRecord` (`src/Renderer/RenderGraph/RenderGraphTypes.h`) carries
three fields — `category`, `drawKind`, `tags` — that are each, individually,
**purely descriptive metadata read by NOTHING in Core**
(`RenderGraph.cpp`/`RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`
never touch them). Their only real reader is the Editor's Frame Debugger.
Despite this, all three are unconditionally, permanently stored on the hot,
always-live `PassRecord` struct, in EVERY build — including a headless
Player build that never displays a Frame Debugger and never reads these
fields at all.

When this campaign is done:

1. `PassRecord` sheds exactly these 3 fields (`category`/`drawKind`/`tags`)
   — a measurable, permanent reduction in its own per-pass storage cost, in
   every build, forever.
2. `PassRecord::kind` and `PassRecord::viewScope` are **completely
   untouched** — same type, same position, same call sites. This is a
   deliberate, permanent, scope boundary (see Step 2 below), not an
   oversight.
3. A brand-new, Core-owned, opaque, zero-cost-when-absent hook
   (`IPassDebugMetadataSink` for writing, `IPassDebugMetadataProvider` for
   reading — see Step 3, Locked Decision 1) replaces that storage. Only
   `src/Editor/` ever implements it (`FrameDebuggerPassMetadataRecorder`), a
   headless/Player build pays one null-pointer check per pass declaration
   and stores nothing.
4. The Editor's Frame Debugger / "Render Graph" panel / `GET /render_graph`
   JSON keep showing **exactly** the same information they show today — a
   pure internal storage refactor, zero feature change, zero regression.
5. `ViewScope`'s enum gains an explicit `std::uint8_t` underlying type
   (today it silently defaults to `int`, 4 bytes, despite only ever needing
   1) — a small, free, additive tightening bundled into the same phase that
   already touches `PassRecord`'s neighboring fields (Locked Decision 3,
   confirmed with the user via `ask_questions`).

This mirrors an ALREADY-SHIPPED, ALREADY-PROVEN precedent in this exact
codebase: `GpuMemoryTracker`'s debug-name observer hook
(`docs/conventions/gpu-resource-memory-tracking.md`,
`editor-core-separation-1` campaign, PHASE4). Do not invent a new pattern —
copy that one, with the two deliberate differences documented in the source
design doc's own Section 3, plus one MORE difference this campaign's own
analysis found and resolved (Locked Decision 1 below) that the source
document itself did not fully work out.

## Step 2: The Situation (Where are we now?)

Every file this campaign touches was re-read fresh, directly, immediately
before these phase files were written — confirmed, not assumed:

- `src/Renderer/RenderGraph/RenderGraphTypes.h` — `PassRecord` (struct,
  ~line 717) carries `kind` (`PassKind`), `viewScope` (`ViewScope`),
  `category` (`RenderPassCategory`), `drawKind` (`RenderPassDrawKind`),
  `tags` (`RenderPassTagMask`) — all five individually read by nothing in
  Core. `ViewScope` (enum, ~line 355) is the ONE sibling enum among
  `PassKind`/`RenderPassCategory`/`RenderPassDrawKind`/`ViewScope` that is
  NOT already `: std::uint8_t` — confirmed by direct inspection, this is
  the one TR6 targets.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — `AddRenderPass()`
  (~line 525, the two overloads) is the ONE, OFFICIAL, sole production
  entry point that stamps `category`/`drawKind`/`tags` (three
  `m_passes.back().X = X;` lines, ~534-537). `AddPass()`/`AddComputePass()`
  (~line 408/452) never stamp these three fields, only `kind`/`viewScope` —
  confirmed: a full grep of `src/` shows every real production pass
  declaration goes through `AddRenderPass()`; only
  `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` calls
  `AddPass()`/`AddComputePass()` directly, and only to assert `kind`/
  `viewScope`, never `category`/`drawKind`/`tags`.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp` — `BuildPassSnapshot()`
  (`RenderGraphSnapshot.cpp`, ~line 87) is the ONLY place in Core that
  **reads** `pass.category`/`pass.drawKind`/`pass.tags` off a `PassRecord&`
  directly (~lines 94-98) to copy them onto `RenderGraphPassSnapshot`. This
  function will not compile once `PassRecord` loses these fields — it MUST
  be fixed in the same phase that removes them.
- `src/Editor/FrameDebuggerData.cpp`, `src/Editor/FrameDebuggerCoverageChecker.cpp`,
  `src/Renderer/RenderGraph/RenderGraphMetadata.cpp` — all three read
  `.category`/`.drawKind`/`.tags`, but confirmed, by direct inspection of
  each call site's own parameter type, that every one of them operates on a
  `RenderGraphPassSnapshot`/`RenderGraphPassMetadata` value — **never**
  `PassRecord` directly. `RenderGraphPassSnapshot`'s own 5-field shape is
  UNCHANGED by this campaign (Non-Goal 3 of the source doc) — so **none of
  these three files need to change at all**, as long as
  `BuildRenderGraphSnapshot()` keeps correctly populating those 3 fields on
  the snapshot it hands back. This is the crux of Locked Decision 1 below.
- `src/Renderer/Memory/GpuMemoryTracker.h` / `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp`
  — the precedent. `GpuMemoryTracker::SetDebugNameObserver(DebugNameObserver, void*)`
  is a single, plain C function-pointer hook (WRITE side only).
  `EditorGpuMemoryNameOverlay::GetDebugName(handle)` (the READ side) is a
  totally separate, static, Editor-owned accessor that Editor UI code
  (`MemoryPanel.cpp`, `FrameDebuggerDrawRecording.cpp`) calls **directly**
  — it never goes through `GpuMemoryTracker` at all for reads, because
  those UI call sites are themselves already Editor-tier code.
  `EditorGpuMemoryNameOverlay::Install(*renderer.GetMemoryTracker())` is
  called from `CreateEditorLayer()` (`src/Editor/ImGuiEditorLayer.cpp`,
  ~line 1311) — this is the exact spot this campaign's own sink/provider
  installation call is added alongside.
- **The one real gap the source design document leaves unresolved:**
  `RenderGraph::ExecuteCompiledGraph()` (Core, `RenderGraph.cpp`) calls
  `BuildRenderGraphSnapshot()` (Core, `RenderGraphSnapshot.cpp`)
  unconditionally, every `Execute()` call, in every build. Once
  `PassRecord` loses `category`/`drawKind`/`tags`, this Core code path has
  **no remaining way** to learn their real value for a surviving/culled
  pass — `GpuMemoryTracker`'s own precedent doesn't help here, because its
  READ side is only ever called from code that is ALREADY Editor-tier
  (`MemoryPanel.cpp` etc.), while `BuildRenderGraphSnapshot()` is Core code
  with no Editor-tier caller anywhere in between it and `RenderGraph.cpp`.
  The source document's own Section 5 asserts "`BuildRenderGraphSnapshot()`
  reads `category`/`drawKind`/`tags` from THIS sink's own per-frame table"
  without ever explaining the mechanism, while its own TR1/acceptance
  criteria simultaneously lock `IPassDebugMetadataSink` at "exactly two
  pure-virtual methods" (both write-only). Resolved with the user, via
  `ask_questions`, BEFORE any phase file below was written — see Locked
  Decision 1.
- No test file in `tests/` currently asserts on `PassRecord::category`/
  `::drawKind`/`::tags` through anything other than `AddRenderPass()`'s own
  call path (confirmed grep) — but `RenderGraphSnapshotTests.cpp` DOES
  contain 4 real, currently-passing tests
  (`DrawKindIsCopiedThroughForSurvivingPass`,
  `DrawKindIsCopiedThroughForCulledPass`,
  `TagsIsCopiedThroughForSurvivingPass`, `TagsIsCopiedThroughForCulledPass`)
  that build a real graph via `AddRenderPass()` and assert on the resulting
  `RenderGraphPassSnapshot`'s `.drawKind`/`.tags` fields directly — these
  currently pass because `BuildPassSnapshot()` reads straight off
  `PassRecord`. Once no sink is installed AND `PassRecord` no longer stores
  the value, these 4 tests would start reading back nothing but each
  field's own struct-default — they MUST be rewritten (TR3) to install a
  small, test-local, dual-interface fake sink and thread it through
  `BuildRenderGraphSnapshot()`'s new parameter (PHASE3 below spells out the
  exact fake and the exact rewritten test bodies).
- `tests/Renderer/RenderGraph/` has no `RenderGraphTests.cpp` (no test
  constructs a real `gte::rg::RenderGraph` object — it needs a live
  `Renderer&`). This is why PHASE4's own production-wiring inside
  `RenderGraph.h`/`.cpp` is verified via **incremental build + live,
  HTTP-driven verification** (`run_app_background` /
  `gte_send_request` / `stop_app_background`), never a new Tier-1 unit
  test of the `RenderGraph` class itself — there is no existing precedent
  for one, and inventing one is out of scope.

## Step 3: The Plan (detailed strategy)

This campaign is split into 5 implementation phases, each its own `.md`
file in this same folder, plus this PHASE0 orchestrator.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_CORE_DEBUG_METADATA_INTERFACES.md` | New, Core-owned, pure-additive header `RenderGraphDebugMetadataSink.h` — `PassDebugMetadata` struct, `IPassDebugMetadataSink` (write, 2 methods), `IPassDebugMetadataProvider` (read, 1 method). Zero existing file touched. |
| 2 | `PHASE2_EDITOR_FRAME_DEBUGGER_PASS_METADATA_RECORDER.md` | New, Editor-owned `src/Editor/FrameDebuggerPassMetadataRecorder.h` — implements BOTH interfaces, dense index-addressed `std::vector`, clear-but-keep-capacity `BeginFrame()`. New dedicated Tier-1 test file. |
| 3 | `PHASE3_PASSRECORD_FIELD_MIGRATION_AND_SNAPSHOT_REWIRING.md` | THE hot-swap: removes `category`/`drawKind`/`tags` from `PassRecord`; widens `ViewScope` to `std::uint8_t` (TR6); wires `RenderGraphBuilder::AddRenderPass()` to the sink instead of `PassRecord` storage; rewires `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()` to read from a caller-supplied metadata-lookup instead of `PassRecord`; rewrites the 4 affected Tier-1 tests + adds new sink-wiring coverage. |
| 4 | `PHASE4_RENDERGRAPH_PRODUCTION_WIRING_AND_EDITOR_INSTALL.md` | `RenderGraph` gains the sink/provider pointers + setters + `Execute()`/`ExecuteCompiledGraph()` wiring (TR4); Editor startup installs one persistent `FrameDebuggerPassMetadataRecorder`, alongside `EditorGpuMemoryNameOverlay::Install()`. Live, HTTP-driven verification that the Frame Debugger/`GET /render_graph` still shows correct data. |
| 5 | `PHASE5_FULL_REGRESSION_ACCEPTANCE_AUDIT_AND_CLOSEOUT.md` | Every acceptance-criteria checkbox from the source document (Section 8) individually re-confirmed with fresh evidence; full clean build; full `ctest` regression; `CAMPAIGN_COMPLETION_REPORT.md`. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

1. **The Core-can't-read-Editor-data gap is solved with a SECOND, separate,
   read-only interface — `IPassDebugMetadataProvider` — never a 3rd method
   added to `IPassDebugMetadataSink`.** Confirmed with the user via
   `ask_questions` before any phase file was written. `IPassDebugMetadataSink`
   stays EXACTLY the 2 write-only methods (`OnPassDeclared()`/`BeginFrame()`)
   the source document's own TR1/acceptance criteria literally require;
   `IPassDebugMetadataProvider` is a brand-new, additional, equally opaque,
   equally Core-owned interface with exactly ONE method,
   `QueryPassDebugMetadata(declarationIndex, outMetadata) -> bool`. One
   concrete class (`FrameDebuggerPassMetadataRecorder`, PHASE2) implements
   BOTH interfaces — Core holds TWO separate pointers to the SAME object
   (`RenderGraph::m_debugMetadataSink` / `m_debugMetadataProvider`), never a
   `dynamic_cast` between them (this codebase's own house style prefers
   plain, explicit code over that kind of machinery — see
   `RenderGraphTypes.h`'s own `DispatchByKind()` doc comment). This keeps
   `RenderGraphPassSnapshot`'s own 5-field public shape **completely
   unchanged** (true to Non-Goal 3) and means `FrameDebuggerData.cpp`/
   `FrameDebuggerCoverageChecker.cpp`/`RenderGraphMetadata.cpp` need **zero**
   changes (true to FR3/Section 5's own promise) — the correct value is
   already sitting on the snapshot by the time any of them ever see it.
2. **New interfaces live in a brand-new sibling header,
   `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h`** — never
   folded directly into `RenderGraphTypes.h`. Confirmed with the user via
   `ask_questions`. Mirrors this exact folder's own established one-small-
   header-per-concern precedent (`RenderGraphDebugTextureRegistry.h`,
   `RenderGraphDebugVolumeTextureRegistry.h`, `RenderGraphNameSlotTable.h`).
3. **TR6 (`ViewScope`'s underlying-type widening to `std::uint8_t`) is
   folded into PHASE3**, the same phase that already touches
   `RenderGraphTypes.h`'s other `PassRecord`-neighboring fields — never its
   own separate phase. Confirmed with the user via `ask_questions`.
4. **`PassRecord::kind` and `PassRecord::viewScope` are NEVER migrated,
   NEVER touched in type/position/call-site, for the whole campaign.** This
   is the source document's own Section 2 — the single most important
   scope boundary in the whole design. `AddPass()`/`AddComputePass()`
   require **zero** changes anywhere in this campaign. Do not "helpfully"
   extend the sink to cover these two fields — see Section 2 of the source
   document for the full reasoning (in short: it would either double-fire
   the sink for every `AddRenderPass()`-declared pass, or silently delete
   the only test coverage `AddComputePass()`'s own contract has).
5. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any
   NEW diagnostic or permanent code in this campaign.** Always
   `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR` (`src/Core/Logging.h`),
   retrieved via `GET /get_logs` (`gte_send_request`) — this is a hard,
   repo-wide rule (`AGENTS.md`), not new to this campaign.
6. **Never run a full clean build or full `ctest` regression pass except
   in PHASE5.** Every other phase uses an INCREMENTAL build
   (`cmake --build build`) as its compile-check gate, plus a TARGETED
   `ctest -R <filter>` for whatever new/changed Tier-1 test(s) that phase
   itself owns. Use `run_app_background`/`gte_send_request`/
   `stop_app_background` for any live, HTTP-driven debugging a phase
   genuinely needs (`GET /get_logs`, `GET /render_graph`,
   `GET /get_game_view`) — never a blind guess.
7. **Every phase must, at its start, use `ask_questions` for any genuine
   ambiguity it personally discovers** beyond what this file and its own
   phase file already resolve. **Every task an implementation phase itself
   delegates must ALSO be instructed to use `ask_questions`** for its own
   ambiguities — this applies recursively, with no exception, to every
   `delegate_task` call made anywhere in this campaign.
8. **Implementation-phase agents may use `delegate_task` ONLY with
   `position: "next"`, and ONLY to double-check their OWN just-finished,
   large piece of work before writing that phase's completion report**
   (PHASE3 is the single heaviest phase in this campaign — the field
   removal + snapshot rewiring + 4-test rewrite in one sitting — and the
   most likely candidate). Such a sub-task double-check must NEVER create
   its own separate report file — it reports back inline, and the
   ORIGINAL phase still writes the one `PHASEn_COMPLETION_REPORT.md`. No
   phase may delegate an entirely different phase's work ahead of
   schedule, and no phase may use `position: "end"`.
9. **Every phase that changes `gte_core`/`gte_editor`-tier logic must add
   or extend a Tier-1 test wherever the underlying problem allows it**
   (`AGENTS.md`'s "Testability & Regression Safety" section) — every phase
   file below spells out its own exact required test cases.
10. **Every phase must end with**: an incremental compile check
    succeeding, a targeted `ctest` pass for that phase's own new/changed
    tests, a `.md` completion report (`PHASEn_COMPLETION_REPORT.md`)
    written into this same folder, and a git commit (`git_add` +
    `git_commit`) covering both the code change and the report.
11. **New source files are added to BOTH the root `CMakeLists.txt`'s
    explicit (non-glob) source list AND, for new test files,
    `tests/CMakeLists.txt`'s equally explicit (non-glob) list** — forgetting
    either produces no error of any kind, the new code/tests simply never
    compile/run. Every phase file below states EXACTLY which list(s) it
    must edit and roughly where (mirroring an existing, named, nearby
    entry).
12. **`FR2` ("zero production/test call-site churn") is the one hard
    constraint that makes this campaign safe.** `AddRenderPass()`'s public
    signature never changes. Every one of the hundreds of existing
    `AddRenderPass()`/`AddPass()`/`AddComputePass()` call sites across the
    engine keeps compiling, completely unmodified, for the whole campaign
    — if any phase discovers a call site that would need to change, STOP
    and use `ask_questions` before proceeding; that would mean this
    campaign's own understanding of the codebase has a gap.

### 3.2 — Why this shape (five phases, not fewer/more)

- PHASE1 is deliberately the smallest, safest, purely-additive first step —
  two new interfaces, zero existing file touched, so the very first
  incremental build after this phase is guaranteed trivial to diagnose if
  it somehow fails.
- PHASE2 is separate because it is a genuinely different TIER
  (`src/Editor/`, not `src/Renderer/RenderGraph/`) with its own dedicated
  test file and its own `gte_editor`-only-library compile/link concern — this
  codebase has no `GTE_ENABLE_EDITOR` preprocessor macro at all (it was
  removed long before this campaign; see AGENTS.md's "`gte_core` /
  `gte_editor` Library Separation" section), so the Core/Editor boundary
  every phase file below must respect is enforced by which of the two
  separately-linked static libraries a file compiles into (`gte_core` vs.
  `gte_editor`), never by a compile-time `#if` — keeping PHASE2 apart from
  PHASE1's Core-only interfaces makes a future "did the Editor-only
  implementation leak into Core?" audit trivial (just check which phase's
  diff touched which folder).
- PHASE3 is, by a wide margin, the single most invasive phase (a hot,
  always-live struct loses fields; the ONE production write chokepoint
  changes; the ONE Core snapshot-reader changes; 4 existing tests must be
  rewritten in the same sitting to keep compiling and passing) — kept as
  ONE phase (not split further) specifically because `PassRecord`'s field
  removal and `BuildPassSnapshot()`'s rewrite are NOT independently
  buildable: removing the fields without simultaneously fixing
  `BuildPassSnapshot()` leaves the whole engine in a non-compiling state,
  which this campaign's own Locked Decision 10 (every phase ends with a
  succeeding incremental build) forbids ever leaving as an intermediate
  state. This is exactly why PHASE3 is flagged (Locked Decision 8) as the
  most likely candidate for a `position: "next"` self-double-check before
  its own completion report.
- PHASE4 is separate because it is the FIRST time any of this campaign's
  new mechanism is wired into a real, running `RenderGraph` instance and
  actually exercised end-to-end by a live Editor session — genuinely
  different verification method (live HTTP/screenshot, not a Tier-1 unit
  test) than every phase before it, and the natural place to also do the
  Editor-tier startup installation call.
- PHASE5 is always last, matching every prior campaign in this codebase's
  own history — full regression + the literal Section 8 acceptance
  checklist tick-through, once, at the end, never spread across phases.

### 3.3 — Definition of Done for the whole campaign

Identical to Section 8 ("ACCEPTANCE CRITERIA") of the source design
document — PHASE5 restates the full checklist and ticks every box with its
own fresh evidence (code inspection quotes, build output, `ctest` output,
and at least one live `gte_send_request` screenshot/JSON capture of the
Render Graph panel proving the Frame Debugger still shows correct
`category`/`draw_kind`/`tag_group_label` data end to end). When this is
green, `editor-core-separation-25` is closed for good, and BIG STEP 2 of 4
(Buffer Roots/Blit) may begin as its own, later, separate campaign.
