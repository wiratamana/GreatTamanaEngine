# CAMPAIGN COMPLETION REPORT — `editor-core-separation-25` ("Core/Editor Separation: PassRecord Debug Metadata Sink" — BIG STEP 1 of 4)

Branch: `feature/editor-core-separation` (unchanged throughout, as required)
Campaign folder: `task_manager/editor-core-separation-25/`
Status: **Complete.** Full clean build succeeded (624/624 steps, zero errors,
zero warnings), full `ctest` regression pass succeeded (2090 tests, 100% of
executed tests passing, 25 legitimate environment-gated skips — up from
`editor-core-separation-24`'s own final baseline of 2081 tests/25 skips, a
clean **+9 tests / +0 skips**, matching this campaign's own new/changed test
cases: PHASE2's 6 new recorder tests, PHASE3's 3 new sink-wiring builder tests
(`RenderGraphBuilderTests.cpp`), minus PHASE3's 1 removed obsolete test
(`RenderGraphTypesTests.cpp`'s `DefaultConstructedPassRecordHasGeneralCategory`,
replaced with a doc comment since the fields it asserted on no longer exist on
`PassRecord`) — net `6 + 3 - 1 = 8`... plus PHASE3's new
`SharedSinkAcrossTwoDeclarationCyclesNeverLeaksStaleEntries` test in
`RenderGraphSnapshotTests.cpp` = **9**, confirmed by direct arithmetic against
each phase's own completion report), and a final, live, HTTP-driven,
two-independent-capture verification (this phase) confirming the Frame
Debugger / "Render Graph" panel / `GET /render_graph` still show byte-for-byte
correct `category`/`draw_kind`/`tag_group_label` data end-to-end, both BEFORE
and AFTER the full clean rebuild.

**This closes BIG STEP 1 of 4 of the source design document's own larger
series** (Buffer Roots/Blit, Persistent Resource Cache, and GPU Memory
Aliasing are Steps 2-4 — NOT part of this campaign, no dependency either
direction, and may now begin as their own, later, separate campaigns).

---

## The goal

`PassRecord` (`src/Renderer/RenderGraph/RenderGraphTypes.h`) used to carry
three fields — `category`, `drawKind`, `tags` — that are each, individually,
purely descriptive metadata read by NOTHING in
`RenderGraph.cpp`/`RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`,
with their only real reader being the Editor's Frame Debugger. Despite this,
all three were unconditionally, permanently stored on the hot, always-live
`PassRecord` struct, in EVERY build — including a headless Player build that
never displays a Frame Debugger and never reads these fields at all. This
campaign moved all three off `PassRecord` into a brand-new, Core-owned,
opaque, zero-cost-when-absent observer-hook mechanism (mirroring
`GpuMemoryTracker::DebugNameObserver`'s own already-shipped, already-proven
shape), shedding a measurable, permanent 16 bytes per `PassRecord` instance in
every build, forever — a pure internal storage refactor, zero feature change,
zero regression, `PassRecord::kind`/`::viewScope` deliberately, permanently
untouched (Locked Decision 4/Section 2 of the source document).

## The shape: five phases, one new hook mechanism wired end-to-end

- **PHASE1** — new, purely-additive, Core-owned header,
  `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h`: `PassDebugMetadata`
  (plain 3-field value struct), `IPassDebugMetadataSink` (write side, exactly
  2 pure-virtual methods — `OnPassDeclared()`/`BeginFrame()`), and
  `IPassDebugMetadataProvider` (read side, exactly 1 pure-virtual method —
  `QueryPassDebugMetadata()`, the ONE piece of new mechanism this campaign's
  own analysis added beyond the source document, confirmed via `ask_questions`
  before any phase file was written — Locked Decision 1). Zero existing file
  touched. See `PHASE1_COMPLETION_REPORT.md`.
- **PHASE2** — `src/Editor/FrameDebuggerPassMetadataRecorder.h`, the
  Editor-owned concrete implementation of both interfaces: a plain, densely
  index-addressed `std::vector<PassDebugMetadata>`, appended to via exactly
  one `push_back()` per `OnPassDeclared()` call — never a container
  searched/found by key. A new, dedicated 6-case Tier-1 test file. See
  `PHASE2_COMPLETION_REPORT.md`.
- **PHASE3** (the campaign's heaviest phase, self-double-checked via
  `delegate_task position: "next"` per Locked Decision 8) — THE hot-swap:
  `category`/`drawKind`/`tags` deleted entirely from `PassRecord`; `ViewScope`
  widened to an explicit `: std::uint8_t` (TR6); `RenderGraphBuilder::
  AddRenderPass()` wired to call the sink instead of stamping the three
  fields directly; `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()`
  (`RenderGraphSnapshot.h`/`.cpp`) rewired to read through a new, trailing,
  defaulted `metadataLookup` parameter instead of `PassRecord` fields
  directly; the 4 existing `RenderGraphSnapshotTests.cpp` tests that used to
  assert on `PassRecord`-backed snapshot data rewritten against a local
  `FakeMetadataSink`, plus 3 new sink-wiring tests in
  `RenderGraphBuilderTests.cpp`. A genuine gap in the phase's own written
  plan was discovered and fixed mid-implementation: 3 MORE test files
  (`RenderGraphTypesTests.cpp`, `RenderPassTests.cpp`,
  `RenderPipelineTests.cpp`) also asserted on the now-removed fields and
  needed the identical fake-sink fix. See `PHASE3_COMPLETION_REPORT.md`.
- **PHASE4** — `RenderGraph` (`src/Renderer/RenderGraph/RenderGraph.h`/`.cpp`)
  gains the two sink/provider pointers, their setters, and real
  `Execute()`/`ExecuteCompiledGraph()` wiring (constructs the
  `metadataLookup` lambda and threads it into
  `BuildRenderGraphSnapshot()`'s new parameter); `EditorHost`'s constructor
  installs one persistent `FrameDebuggerPassMetadataRecorder` onto both
  pointers, mirroring `EditorGpuMemoryNameOverlay::Install()`'s own placement
  precedent. First live, HTTP-driven verification of the whole mechanism
  end-to-end against a real running Editor. See `PHASE4_COMPLETION_REPORT.md`.
- **PHASE5** (this phase) — full clean build + full `ctest` regression pass
  (the only phase in this campaign allowed to run either, per Locked Decision
  6) + the literal Section 8 acceptance-criteria tick-through, each item with
  its own freshly-gathered evidence + this report.

## Final verification numbers (PHASE5)

- **Full clean build**: `cmake --build build --clean-first` (642 files
  cleaned) followed by a full rebuild — **624/624 steps succeeded, zero
  errors, zero warnings** (`GreatTamanaEditor.exe`, `GreatTamanaEngineTests.exe`,
  every plugin `.dll`, every Project Assembly `.dll`, all rebuilt and relinked
  cleanly).
- **Full `ctest -C Debug --output-on-failure`**: **2090 tests total, 100% of
  executed tests passing, 25 legitimate environment-gated skips** — up from
  `editor-core-separation-24`'s own documented 2081/25 baseline, a clean
  **+9 tests / +0 skips**, exactly matching this campaign's own net new/changed
  test count (see the itemized arithmetic in the Status line above). No test
  failed at any point during this phase's own full-suite run — the "diagnose
  and fix it yourself"/`delegate_task` regression-fix contingency (PHASE0's
  Step 3.3.2) was never triggered.
- **Two independent live, HTTP-driven captures against a real running
  `GreatTamanaEditor.exe`** (this phase, `GET /render_graph`, `GET /get_logs`,
  `GET /get_swapchain`): one BEFORE the full clean rebuild (PID 14028) and one
  AFTER it, against the freshly relinked binary (PID 22364) — both showed the
  identical, correct, non-default `category`/`draw_kind`/`tag_group_label`
  values (see Section 8, item 3, below for the full evidence), zero
  `GET /get_logs?min_level=Error` entries in either session, and a
  byte-identical 85737-byte `GET /get_swapchain` PNG in both sessions. Neither
  session was left running (`stop_app_background` confirmed for both PIDs).

## Section 8 acceptance criteria — re-confirmed with fresh evidence (this phase)

Restated per `PHASE0_MASTER_STRATEGY.md`'s Locked Decision 1 adaptation
(the two-interface split) where the source document's own literal wording
assumed a single interface.

1. **`sizeof(PassRecord)` measurably shrinks; `kind`/`viewScope` unchanged in
   type/position/every call site that stamps them.** Confirmed by direct
   re-read of `RenderGraphTypes.h` (this phase): `category`/`drawKind`/`tags`
   are genuinely absent from the `PassRecord` struct today — replaced by two
   doc-comment blocks (lines ~793-811, ~853-856) pointing at
   `RenderGraphDebugMetadataSink.h`. `kind` (line 758, `PassKind`) and
   `viewScope` (line 766, `ViewScope`) remain real fields, same type, same
   position. PHASE3's own `sizeof` probe (168 bytes before, 152 bytes after —
   a genuine 16-byte reduction) is the concrete measurement; this phase
   re-confirmed the STRUCTURAL absence of the fields directly rather than
   re-running that probe. `AddPass()`/`AddComputePass()` in
   `RenderGraphBuilder.h` — re-grepped this phase for `m_debugMetadataSink`/
   `OnPassDeclared`: exactly 4 total occurrences, all inside
   `SetDebugMetadataSink()`'s declaration, the private member declaration,
   and the two lines inside the 9-argument `AddRenderPass()` overload —
   **zero** inside either `AddPass()` or `AddComputePass()`'s own bodies,
   confirming both remain byte-for-byte unchanged.
2. **A headless build compiles with ZERO references to
   `FrameDebuggerPassMetadataRecorder` anywhere outside `src/Editor/`.**
   Confirmed THREE independent ways, all run fresh in this phase:
   - `search_in_dir` across all of `src/` for `FrameDebuggerPassMetadataRecorder`
     — exactly 6 hits in 3 files: `src/Editor/EditorHost.h` (2, the `#include`
     and the member declaration), `src/Editor/FrameDebuggerPassMetadataRecorder.h`
     (1, the class declaration itself), and
     `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h` (2, both
     inside doc-comment prose, never a real `#include`/reference). Every hit
     is under `src/Editor/`, none under `src/Renderer/` or any other
     `gte_core`-tier folder.
   - `tools/ci/gte_core_standalone_probe/` — configured fresh
     (`cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe -G Ninja`)
     and built (`cmake --build build-core-probe`): **249/249 steps, zero
     errors** — `gte_core.a` compiles as a genuinely standalone archive, zero
     `gte_editor`, zero ImGui, zero SDL3 involvement of any kind.
   - `tools/ci/gte_core_player_link_probe/` — configured fresh and built:
     **175/175 steps, zero errors** — `gte_core_player_link_probe.exe` LINKS
     (a strictly stronger proof than an archive build, since a real link
     resolves every cross-translation-unit symbol reference) against
     `gte_core.a` ALONE. Also ran the resulting `.exe`'s own PHASE5 bonus
     check (`editor-core-separation-3`'s own precedent): a real headless
     `gte::Core` construction attempt correctly, honestly self-skipped
     (`"this machine's Vulkan driver lacks VK_EXT_headless_surface"`,
     `VkResult=-7`) — the documented, expected outcome on this dev machine,
     not a crash or hang.
   - Both probe build trees (`build-core-probe/`, `build-player-link-probe/`)
     were deleted after use — confirmed via a clean final `git_status`
     (nothing to commit, working tree clean; both trees are also
     permanently `.gitignore`d).
3. **The Editor's "Render Graph" panel / Frame Debugger shows byte-for-byte
   identical output before and after this change, for the same captured
   frame.** Two independent live captures gathered THIS phase (PHASE4 already
   gathered its own, separate one during its own work):
   - **Capture A** (PID 14028, before the full clean rebuild): `GET
     /render_graph` — `"DrawSkyBackground"` (both `GameView`/`SceneView`
     instances) reports `"draw_kind":"DrawQuad"` against every other pass's
     struct-default `"DrawMesh"`; all 5 Atmosphere LUT passes report
     `"tag_group_label":"Compute LUT"` against every other pass's `null`;
     `"DemoRenderFeaturePlugin_Clear"`/`"DemoRenderFeatureSecondPlugin_Clear"`
     report `"category":"Debug"` against every other pass's `"General"`.
     `GET /get_logs?min_level=Error` — `{"count":0,...}`. `GET /get_swapchain`
     — a normal, correctly-rendered 85737-byte PNG (Scene/Game panels, Render
     Graph tab, Probe Panel all present and normal).
   - **Capture B** (PID 22364, AFTER the full clean rebuild — a genuinely
     different, freshly-relinked binary): identical structural result —
     `"DrawSkyBackground"` → `"DrawQuad"`, every Atmosphere LUT pass →
     `"Compute LUT"`, both Demo plugin passes → `"Debug"` — same shape, same
     non-default values, same zero-error log state, and a **byte-identical
     85737-byte** `GET /get_swapchain` PNG.
   - Both sessions launched via `run_app_background`, verified, and cleanly
     stopped via `stop_app_background` — neither left running.
4. **Every Tier-1 test identified under TR3 is updated and passing; every
   test asserting on `kind`/`viewScope` directly is confirmed completely
   unmodified.** Re-confirmed this phase via `git diff` against
   `dd2f8d0` (the commit immediately before PHASE1's own first commit):
   - `git diff dd2f8d0 HEAD --stat` for
     `RenderGraphBuilderTests.cpp`/`RenderGraphSnapshotTests.cpp` shows 233
     net insertions across the two files (the new/rewritten sink-related
     tests) — a targeted `findstr` for `viewScope`/`.kind` inside that same
     diff found **zero matches in either file**, confirming no line
     mentioning either field was ever touched by this campaign in those two
     files.
   - `git diff dd2f8d0 HEAD` for `RenderPassTests.cpp`/`RenderPipelineTests.cpp`
     (the two files PHASE3's own discovered-gap fix touched) shows every
     `EXPECT_EQ(input.passes[0].kind, ...)`/`EXPECT_EQ(input.passes[0].viewScope, ...)`
     line appearing ONLY as unchanged context (no `+`/`-` prefix on any such
     line) — confirmed via a second, `^[-+]`-anchored regex pass that
     returned zero matches. Every one of these assertions is therefore
     byte-for-byte unmodified, exactly as required.
   - The full `ctest` regression pass (above) confirms all of these tests —
     rewritten, newly-added, and untouched alike — currently pass, 100%.
5. **`FrameDebuggerPassMetadataRecorder`'s own table is a plain,
   contiguously index-addressed `std::vector`, never a container
   searched/found by key.** Re-confirmed by direct re-read of
   `src/Editor/FrameDebuggerPassMetadataRecorder.h` this phase:
   `std::vector<rg::PassDebugMetadata> m_table;` is the class's only private
   member; `OnPassDeclared()` does exactly one `m_table.push_back(...)`,
   guarded by an `assert(declarationIndexThisFrame == m_table.size())` —
   never a `find`/key comparison of any kind anywhere in the file.
6. **`IPassDebugMetadataSink` exposes exactly two pure-virtual methods;
   `IPassDebugMetadataProvider` exposes exactly one; `RenderGraph::Execute()`
   calls `BeginFrame()` once, unconditionally, before `build(builder)` runs,
   and `OnPassDeclared()` indirectly, once per declared pass.** Re-confirmed
   by direct re-read of `RenderGraphDebugMetadataSink.h` (exactly
   `OnPassDeclared()`/`BeginFrame()` on the sink interface, exactly
   `QueryPassDebugMetadata()` on the provider interface — no more, no less)
   and `RenderGraph.h`'s `Execute()` template body (lines ~176-193):
   `builder.SetDebugMetadataSink(m_debugMetadataSink);` immediately followed
   by a null-guarded `m_debugMetadataSink->BeginFrame();`, BOTH before
   `build(builder)` is ever called — `OnPassDeclared()` itself is only ever
   invoked indirectly, from inside `AddRenderPass()`, which `build(builder)`
   calls once per declared pass.
7. **A single `RenderGraph::SetDebugMetadataSink()`/
   `SetDebugMetadataProvider()` call, made once at Editor startup, is
   sufficient for every later offscreen/present, synchronous/pipelined
   `Execute()` call that session.** Confirmed by BOTH pieces of required
   evidence:
   (a) PHASE3's own `SharedSinkAcrossTwoDeclarationCyclesNeverLeaksStaleEntries`
   test — re-run this phase as part of the full `ctest` pass — **Passed**,
   proving one shared sink correctly resets its declaration-index space
   across two independent declare/`BeginFrame()`/declare cycles at the
   builder/snapshot layer.
   (b) This phase's own two live captures (item 3, above) — the SAME
   captured-frame response's `offscreen_regime` shows real, non-default
   values (proving the recorder is genuinely populated and read back
   correctly for that `Execute()` call) AND the SAME response's
   `present_regime` shows a structurally well-formed, non-error `"Present"`
   entry (`category:"General"`, `draw_kind:"DrawMesh"`,
   `tag_group_label:null` — the documented, expected honestly-default shape
   for a plain `AddRenderPass()` call with every trailing argument at its own
   default, per PHASE5's own Step 3.1 item 7 wrinkle note) — proving the
   SAME shared, single-installed recorder served a SECOND, independent
   `Execute()` call in the same session (the pipelined Present regime)
   without crashing or returning stale/corrupted data.
8. **`ViewScope`'s underlying type is an explicit `std::uint8_t`.**
   Re-confirmed by direct re-read of `RenderGraphTypes.h` this phase (line
   362): `enum class ViewScope : std::uint8_t { Shared, GameView, SceneView };`,
   with an updated doc comment recording the widening and its rationale.
9. **Full `ctest` regression pass, zero unexplained delta.** See "Final
   verification numbers" above — 2090/2090 executed tests passing, 25
   legitimate skips, +9 tests over the prior baseline, every one individually
   accounted for.

## What would the NEXT campaign in this series (BIG STEP 2 of 4 — Buffer Roots/Blit) need to know

Nothing from this campaign's own implementation surfaced a concern worth
flagging forward — this is a valid, honest answer, not an invented one. BIG
STEP 1 has zero dependency on Steps 2-4 in either direction (per the source
document's own Section 9), and this campaign's own scope boundary
(`PassRecord::kind`/`::viewScope` permanently untouched, `RenderPassEvent`
untouched, `RenderGraphPassSnapshot`'s own 5-field public shape untouched)
means nothing about this campaign's own mechanism constrains or informs how a
future Buffer Roots/Blit campaign would approach its own, unrelated problem
space. The one thing worth restating for a future reader (not a concern, just
context): this campaign's own `IPassDebugMetadataProvider` read-side
interface was a genuine, NEW piece of mechanism beyond the source document's
own original single-interface sketch (Locked Decision 1) — any future
campaign in this series that also needs a Core-to-Editor read-back path
(rather than just a write-only observer hook, `GpuMemoryTracker`'s own
original shape) now has a second, real, in-repo precedent to copy from
alongside `GpuMemoryTracker::DebugNameObserver` itself.

## Honest, permanent limitations (restated plainly, not silently smoothed over)

1. **This is deliberately NOT a headless-build performance or memory fix**
   (Non-Goal 3, Section 6 of the source document). The dominant
   Frame-Debugger-only cost today remains `RenderGraphSnapshot`'s own
   unconditional per-frame construction (including a heap-allocated
   `std::string` copy of every declared read/write resource's name, for every
   pass, every single frame, in every build) — this campaign never touched
   that. A `GTE_ENABLE_EDITOR=OFF`-equivalent (this codebase's headless/Player
   build shape) build still builds a full snapshot every frame and still
   carries every one of these fields' values inside that snapshot; this
   campaign only stopped three of them from ALSO being permanently stored, a
   second time, on `PassRecord` itself.
2. **`PassRecord::kind`/`::viewScope` remain permanently on `PassRecord`,
   forever** — not because they are more load-bearing than
   `category`/`drawKind`/`tags` (they are equally read-by-nothing-in-Core),
   but because they are the only two of the five fields with a second,
   independently-tested stamping path (`AddPass()`/`AddComputePass()`) this
   campaign has no safe way to fold into the same sink without inventing new
   machinery (Section 2 of the source document). This is a deliberate,
   permanent scope boundary, not a placeholder for a future phase.
3. **No new runtime reentrancy/thread-safety guard was added** to
   `FrameDebuggerPassMetadataRecorder`/`IPassDebugMetadataSink`/
   `IPassDebugMetadataProvider` — the mechanism's safety rests entirely on
   the already-true, pre-existing fact that one `RenderGraph::Execute()` call
   against one `RenderGraph` instance always finishes before the next one
   starts (Section 5.2 of the source document). If a future change ever
   parallelizes `RenderGraph::Execute()` calls, this mechanism (and this
   document) must be revisited as a prerequisite, not patched around after
   the fact.

None of these three are regressions or open bugs in what this campaign
shipped — all three are deliberate, documented scoping decisions carried over
unchanged from the source design document's own explicit Non-Goals/Section 2/
Section 5.2, restated here honestly rather than silently smoothed over.

## Files changed across the whole campaign

- `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h` (new, PHASE1 —
  `PassDebugMetadata`/`IPassDebugMetadataSink`/`IPassDebugMetadataProvider`)
- `src/Editor/FrameDebuggerPassMetadataRecorder.h` (new, PHASE2 — the
  concrete, Editor-owned implementation of both interfaces)
- `tests/Editor/FrameDebuggerPassMetadataRecorderTests.cpp` (new, PHASE2 — 6
  Tier-1 tests)
- `src/Renderer/RenderGraph/RenderGraphTypes.h` (PHASE3 — `category`/
  `drawKind`/`tags` removed from `PassRecord`; `ViewScope` widened to
  `std::uint8_t`)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` (PHASE3 —
  `SetDebugMetadataSink()`, `AddRenderPass()` wired to the sink)
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp` (PHASE3 —
  `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()` gain the new
  `metadataLookup` parameter)
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` (PHASE3 — 4 tests
  rewritten against a local `FakeMetadataSink`, 1 new test added)
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` (PHASE3 — 3 new
  sink-wiring tests)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (PHASE3's discovered
  gap — 1 obsolete test removed, replaced with an explanatory doc comment)
- `tests/Renderer/RenderGraph/RenderPassTests.cpp` (PHASE3's discovered gap —
  8 assertions rewritten against a local `FakeMetadataSink`)
- `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` (PHASE3's discovered
  gap — 2 assertions rewritten against a local `FakeMetadataSink`)
- `src/Renderer/RenderGraph/RenderGraph.h`/`.cpp` (PHASE4 — sink/provider
  pointers + setters + `Execute()`/`ExecuteCompiledGraph()` wiring)
- `src/Editor/EditorHost.h`/`.cpp` (PHASE4 — installs one persistent
  `FrameDebuggerPassMetadataRecorder` at startup)
- `CMakeLists.txt` (PHASE1/PHASE2 — new file entries in the `gte_core`/
  `gte_editor` source lists)
- `tests/CMakeLists.txt` (PHASE2 — new test-file entry)
- Every `PHASEn_COMPLETION_REPORT.md`/this `CAMPAIGN_COMPLETION_REPORT.md` in
  `task_manager/editor-core-separation-25/`

## Delegation and ambiguity summary across the whole campaign

- `ask_questions` was invoked exactly once, BEFORE any phase file was
  written (during this campaign's own strategy-authoring step) — resolving
  the source document's own unaddressed "Core can't read Editor data" gap in
  favor of a second, separate `IPassDebugMetadataProvider` interface (Locked
  Decision 1) rather than a third method on the sink or a `dynamic_cast`. No
  phase (PHASE1-PHASE5) needed to ask a further genuine question — each
  phase's own plan file specified exact worked-example code for every change,
  and the one real ambiguity PHASE3 discovered mid-implementation (3 more
  test files silently asserting on the doomed `PassRecord` fields) had an
  unambiguous, mechanical fix (reuse the identical fake-sink pattern already
  mandated for the 4 named tests) rather than a genuine design choice.
- `delegate_task` (`position: "next"`) was used exactly once, by PHASE3 (the
  campaign's own flagged heaviest/highest-risk phase per Locked Decision 8) —
  an independent self-double-check of its own just-finished diff, build, and
  test run, reporting back inline per this campaign's own rules. No other
  phase used it; PHASE5's own full regression pass surfaced zero failures,
  so its own contingency `delegate_task` (Step 3.3.2, for diagnosing a
  genuine regression) was never triggered.
- No phase left the engine in a non-compiling or non-passing intermediate
  state at any point — every phase's own completion report documents a
  succeeding incremental build and a passing targeted `ctest` run before
  moving on, exactly as PHASE0's Locked Decision 10 requires.
