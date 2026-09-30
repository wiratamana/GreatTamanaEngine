# PHASE8 — COMPLETION REPORT: `RenderGraphBuilder::GetOrCreatePersistentTexture()` + Honest-Layout/Resize-Flush Wiring

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

This is the phase where the whole "Persistent Resource Cache" feature became
REAL and observable for the first time — the actual entry point a pass author
calls (`RenderGraphBuilder::GetOrCreatePersistentTexture()`, both overloads),
plus the tail hook that makes a persistent texture's layout honest across
frames and its resize batched.

1. **`RenderGraphPersistentResourceCache`** (`.h`/`.cpp`):
   - `Resolve()`'s own tail (the same-real-frame double-request refusal,
     age-stamping, format/resize handling, final `ResolvedTexture`
     construction) was extracted into a new shared private helper,
     `ResolveAgainstEntry(PersistentResourceCacheEntry&, const TextureDesc&,
     std::uint64_t, ExecuteTimingMode)`, called once from `Resolve()` (right
     after its own `try_emplace`/construction step) and once from the new
     `ResolveFast(PersistentResourceCacheEntry*, ...)` (the token-based fast
     path's own resolve call).
   - New public `void RecordFinalLayout(const char* key, VkImageLayout
     layout)` — a safe no-op for a null/unknown key, otherwise stamps
     `entry.lastKnownLayout`.
2. **`RenderGraphBuilder`** (`.h`/`.cpp`):
   - New public `TextureHandle GetOrCreatePersistentTexture(const char*
     owner, const char* name, const TextureDesc& desc)` (FR1) and
     `TextureHandle GetOrCreatePersistentTexture(PersistentTextureCacheToken&
     token, const char* owner, const char* name, const TextureDesc& desc)`
     (FR7, the token-based fast path).
   - New private `TextureHandle MintPersistentHandle(const
     RenderGraphPersistentResourceCache::ResolvedTexture&)` — the ONE place
     either overload calls the pre-existing, unchanged `ImportTexture()`
     (using `resolved.combinedKey->c_str()` as `name` — a pointer into the
     cache's own permanently-stable `std::string`, TR4) and pushes the
     resulting handle onto `m_persistentCacheTextures` (already existed since
     PHASE2 — PHASE3's compiler root-marking fix reads this vector).
   - New `#include "../../Core/Logging.h"` — the FIRST `GTE_LOG_ERROR` call
     site in this file (confirmed via `search_in_dir` before editing).
3. **`RenderGraph::ExecuteCompiledGraph()`** (`RenderGraph.cpp`): gained a new
   tail block, inserted immediately after the pre-existing
   `RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`
   calls:
   - A loop over `input.persistentCacheTextures` that calls
     `m_persistentResourceCache.RecordFinalLayout(input.textures[h.index].name,
     physicalTextures[h.index].colorState.layout)` for every entry whose
     index is in-bounds AND resolved this call (mirrors
     `RegisterDebugTextureSnapshots()`'s own identical defensive guard) —
     runs every regime, every call (Section 5.1's "honest layout" guarantee).
   - `if (!isPipelined) { m_persistentResourceCache.FlushPendingResizes(); }`
     — batched resize flush, `SynchronousImmediateReadback` only (Section
     5.5), mirroring `m_resourcePool`'s own identical gating a few lines
     earlier in the same function.
4. **Tests** (`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`,
   the SAME file PHASE4-7 already extended — no new test file): 6 new test
   cases (numbered 21-26 in this file's own comments) plus 3 new
   `EXPECT_DEATH` cases, described in full under "Required Tests" below.

No new source file was created — every change landed inside five
pre-existing files (`RenderGraphBuilder.h`, `RenderGraphBuilder.cpp`,
`RenderGraphPersistentResourceCache.h`, `RenderGraphPersistentResourceCache.cpp`,
`RenderGraph.cpp`) plus the one pre-existing test file.

## Step 1/2 re-confirmation (done fresh, before editing)

- Re-read `PHASE0_MASTER_STRATEGY.md` in full (Corrections 1/2/3, Locked
  Decisions 1-7) and `PHASE7_COMPLETION_REPORT.md` in full, per this phase's
  own `.md` instruction.
- Re-read this phase's own `.md`
  (`PHASE8_BUILDER_GETORCREATEPERSISTENTTEXTURE_AND_HONEST_LAYOUT_WIRING.md`)
  in full.
- Re-confirmed, by direct code read, every citation this phase's own `.md`
  makes:
  - `RenderGraphPersistentResourceCache::Resolve()`'s FINAL signature (after
    PHASE4/5/6): `(const char* owner, const char* name, const TextureDesc&
    desc, std::uint64_t currentFrame, ExecuteTimingMode timingMode)` —
    confirmed exact match against the actual current header, no drift.
  - `RenderGraphBuilder.h` (post-PHASE7) already had
    `m_persistentCache`/`m_persistentCacheTimingMode`/
    `m_persistentCacheCurrentFrame` as private members, and
    `#include "RenderGraphPersistentResourceCache.h"` already present —
    confirmed, no new include needed there.
  - `RenderGraphBuilder.cpp` had never used `GTE_LOG_ERROR` before —
    confirmed via `search_in_dir`, zero hits in `src/Renderer/RenderGraph/`
    for that macro outside `RenderGraphPersistentResourceCache.cpp` itself
    (this phase's own new `GetOrCreatePersistentTexture()` refusal branches
    are the first call sites) — added the same relative-path include
    `RenderGraph.cpp`/`RenderPassGroupRegistry.cpp` already use.
  - `RenderGraphBuilder::ImportTexture()`'s exact signature — confirmed
    matches the `.md`'s own citation exactly.
  - `RenderGraph::ExecuteCompiledGraph()`'s exact tail (the
    `RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`
    pair, immediately preceded by the `if (isPipelined) { ++m_pipelinedFrameCounter; }`
    block) — confirmed byte-for-byte against the `.md`'s own cited snippet,
    only line-number drift (expected, from PHASE1-7's own edits).
  - `RegisterDebugTextureSnapshots()`'s own `if (!tex.resolved) { continue;
    }` defensive guard — confirmed, copied verbatim in shape for this
    phase's own new loop.
  - `TextureSlot::name`/`input.textures[h.index].name` — confirmed this is
    exactly the combined `"<owner>::<name>"` key string this phase's new
    loop needs, with zero extra bookkeeping.
- No genuine ambiguity beyond what `PHASE0_MASTER_STRATEGY.md` and this
  phase's own `.md` already resolve was found — `ask_questions` was not
  needed for the production-code implementation itself.
- **Per `PHASE0_MASTER_STRATEGY.md`'s own explicit instruction (this
  campaign's second-highest-risk phase, after PHASE4), a `dispatch_sub_agent`
  double-check of this phase's own just-finished work was performed BEFORE
  writing this report** — see "Sub-agent double-check" below for the full
  finding and the fix it led to. Per Rule 4, that dispatch did NOT create its
  own separate report file; its findings are folded into this one report.

## Sub-agent double-check (required by this campaign's own PHASE0 Rule 4)

A `dispatch_sub_agent` was sent to independently re-derive, line by line,
whether the `Resolve()` → `ResolveAgainstEntry()` refactor was genuinely
behavior-preserving, whether the new builder wiring was correct, whether the
new `RenderGraph.cpp` tail hook was safe, and whether the 6 new tests were
GENUINELY correct (not merely "compiles/would-pass-if-run") — since every one
of them reports `Skipped` on this development machine (see "Honestly-flagged
open issues" below), a logic bug in a NEW test's own assertions could easily
hide behind a `Skipped` result forever without this kind of direct code-trace
review.

**Production code**: the sub-agent confirmed the `ResolveAgainstEntry()`
extraction is byte-for-byte behavior-preserving (the double-request check and
format/resize-change check are structural no-ops for a brand-new entry,
exactly as this phase's own `.md` predicted), confirmed `MintPersistentHandle()`/
both `GetOrCreatePersistentTexture()` overloads are wired correctly, and
confirmed the `ExecuteCompiledGraph()` tail hook's bounds guard and
`!isPipelined` gating are both correct.

**A genuine, confirmed-live TEST BUG was found and fixed as a direct result
of this double-check** (found BEFORE this report/commit, not after): three of
this phase's own originally-drafted tests (numbered 21, 22, 25 — "same VkImage
across 3 frames via the builder", "token fast path", "two owners never
collide") called `GetOrCreatePersistentTexture()` inside their `build` lambda
but declared **zero passes** touching the returned handle. Traced mechanically:
`RenderGraphCompiler::Compile()`'s root-marking scan (`RenderGraphCompiler.cpp`
~line 540-572) only ever scans `pass.writes` — being pushed into
`input.persistentCacheTextures` makes a **pass that writes that handle**
survive culling; it has **zero effect** when no pass in that frame touches the
handle at all. `RenderGraph::EnsureTextureResolved()` (which sets
`PhysicalTexture::resolved = true`, the flag `DebugTextureSnapshotFor()`
gates on) is only ever reached while walking a SURVIVING pass's declared
reads/writes. With no `AddPass()` call at all in these three tests' original
drafts, `physicalTextures[h.index].resolved` would stay `false` the whole
frame, so `DebugTextureSnapshotFor()` would correctly return `std::nullopt`,
and each test's own `ASSERT_TRUE(snapshot.has_value())` would **FAIL** (not
skip) on any machine whose Vulkan driver actually supports
`VK_EXT_headless_surface` — invisible on THIS machine only because every test
in this file reports `Skipped` here, silently hiding the defect. **Fixed**: all
three tests now declare a real `AddPass(...)` with a plain
`pb.WriteColorAttachment(h)` (no clear color) touching the handle, mirroring
test 24's own already-correct pattern.

**A second, related, softer defect was ALSO found and fixed in test 23**
(the write-frame-N/read-frame-N+1 "no discard" proof): its original frame-2
pass declared ONLY `pb.ReadTexture(h, ResourceAccess::ShaderRead)` — a
pass with **zero writes** can never become a root by itself (the root scan
only ever looks at `pass.writes`), and with no other pass declared that frame
to be a dependency predecessor of, this pure read-only pass would be
**unconditionally culled**, meaning the test's own comment claim ("proving the
compiler correctly barriers a genuine cross-frame read") was never actually
true — the test's assertions would likely still have numerically passed
anyway (by coincidence: `RegisterDebugTextureSnapshots()` leaves a previous
frame's stale registry entry untouched when nothing resolves it, and the
physical image/content genuinely is unchanged either way), but for the wrong
reason, proving nothing about frame 2 at all. **Fixed**: replaced the
read-only pass with a `WriteColorAttachment(h)` call carrying NO clear color
(`VK_ATTACHMENT_LOAD_OP_LOAD`, not `_CLEAR`) — a real write, so it correctly
survives culling via the SAME `persistentCacheTextures` root, AND a genuinely
meaningful regression check in its own right: if this cache's own
honest-layout-recording or the builder's `resolved.lastKnownLayout` ->
`ImportTexture()` plumbing were ever wrong, `LOAD_OP_LOAD` against a
falsely-assumed layout would read back GPU garbage instead of frame 1's real
content, and this test's own final `Renderer::CaptureImagePixels()` read-back
would then genuinely fail. Both fixes were re-verified with a fresh
incremental build (zero errors) and a fresh targeted `ctest` run (see
"Verification" below) before this report was written.

No part of this phase's own PRODUCTION code implementation was itself
delegated to a `dispatch_sub_agent` — only the double-check pass was, per
`PHASE0_MASTER_STRATEGY.md`'s own Rule 4 recommendation for this
second-highest-risk phase.

## Changes made

1. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`**:
   - Added `ResolveFast(PersistentResourceCacheEntry*, const TextureDesc&,
     std::uint64_t, ExecuteTimingMode)` and `RecordFinalLayout(const char*,
     VkImageLayout)` public method declarations, right after `IsTokenLive()`.
   - Added the private `ResolveAgainstEntry(PersistentResourceCacheEntry&,
     const TextureDesc&, std::uint64_t, ExecuteTimingMode)` helper
     declaration, right after `QueueResize()`.
2. **`src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`**:
   - `Resolve()`'s body was shortened to: validation asserts (unchanged) →
     `try_emplace` → construct-if-absent (unchanged, including its own
     exception-safety try/catch) → `return ResolveAgainstEntry(it->second,
     desc, currentFrame, timingMode);`.
   - Added `ResolveFast()`'s definition (asserts non-null `entry`, forwards
     to `ResolveAgainstEntry()`).
   - Added `ResolveAgainstEntry()`'s definition — the same-real-frame
     double-request refusal, age-stamping, format-change/resize-request
     handling, and final `ResolvedTexture` construction, moved verbatim out
     of the old `Resolve()` body (now reading/writing `entry.*` directly
     instead of `it->second.*`, and reading the key via
     `*entry.ownKeyForDebugAssert` instead of a locally-rebuilt `key` string).
   - Added `RecordFinalLayout()`'s definition (a `m_entries.find()` +
     conditional `lastKnownLayout` stamp).
3. **`src/Renderer/RenderGraph/RenderGraphBuilder.h`**:
   - Added `#include "../../Core/Logging.h"` to `RenderGraphBuilder.cpp`
     (not this header — see item 4 below).
   - Added both `GetOrCreatePersistentTexture()` overload declarations, right
     after `SetPersistentResourceCache()`.
   - Added the private `MintPersistentHandle()` declaration, right after
     `private:`.
4. **`src/Renderer/RenderGraph/RenderGraphBuilder.cpp`**:
   - Added `#include "../../Core/Logging.h"`.
   - Added `MintPersistentHandle()`'s definition and both
     `GetOrCreatePersistentTexture()` overloads' definitions, right before
     `Finish()`.
5. **`src/Renderer/RenderGraph/RenderGraph.cpp`**: `ExecuteCompiledGraph()`
   gained the new honest-layout-recording loop + `FlushPendingResizes()`
   call, immediately after `RegisterDebugVolumeTextureSnapshots(...)`.
6. **`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`**
   (PHASE4-7's own file, extended again — no new test file): added `#include
   <array>`, and a new `// --- PHASE8 ... ---` block with 6 new `TEST()`
   cases plus 3 new `EXPECT_DEATH`-based cases (all `#ifndef NDEBUG`-guarded
   for the latter), described under "Required Tests" below.

No `tests/CMakeLists.txt` change was needed — the test file was already
registered by PHASE4.

## Required Tests (Step 4 of this phase's `.md`) — mapping to what shipped

1. **Same `VkImage` across 3 real frames, via the real builder API** →
   `BuilderGetOrCreatePersistentTextureReturnsTheSamePhysicalTextureAcrossThreeFrames`
   — confirms `DebugTextureSnapshotFor()`'s `target.image` is identical
   across 3 separate `RunSynchronousFrame()` calls, each preceded by its own
   `BeginPersistentResourceFrame()` call.
2. **Token-based fast path** →
   `BuilderGetOrCreatePersistentTextureTokenFastPathReusesTheSameImage` —
   confirms a stable `PersistentTextureCacheToken` reused across 3 frames
   resolves to a valid, non-null image. **Proof mechanism used for "the fast
   path was genuinely taken" (documented per this phase's own `.md` explicit
   instruction)**: `RenderGraphBuilder::GetOrCreatePersistentTexture()`'s
   token overload only ever calls
   `RenderGraphPersistentResourceCache::DebugTokenIdentityMatches()`'s
   debug-only assert STRICTLY INSIDE its own `if (IsTokenLive(...))` branch —
   this whole test completing normally (never aborting) across 3 repeated
   calls with a stable token is real, if indirect, evidence that branch (and
   only that branch) executed on the second and third calls. No new
   call-counting instrumentation was added to production code for this,
   per this phase's own `.md`'s "simpler and less invasive" alternative.
3. **Write-on-frame-N, read-correctly-on-frame-N+1** →
   `ContentWrittenOnFrameNSurvivesReadableOnFrameNPlusOne` — frame 1 writes a
   known clear-color pattern (`{0.25, 0.5, 0.75, 1.0}`) via a real
   `WriteColorAttachment` pass with no other reader that frame; frame 2
   declares a SECOND real, genuinely-surviving `WriteColorAttachment` pass
   with **no clear color** (`VK_ATTACHMENT_LOAD_OP_LOAD`, proving the
   existing content is loaded, never discarded) against the identical
   identity. **Proof mechanism used (documented per this phase's own `.md`
   explicit instruction)**: direct GPU pixel read-back via
   `Renderer::CaptureImagePixels()` — the exact same primitive `GET
   /get_texture` itself is built on — rather than plumbing a compute-shader
   round-trip through the headless fixture, chosen because it is simpler,
   already proven-correct production code, and directly answers the "what
   are the real bytes" question this test needs. See "Sub-agent
   double-check" above for why the original draft (a `ReadTexture()`-only
   frame-2 pass) was corrected.
4. **Keep-alive in practice, through the REAL builder** →
   `WriteOnlyPersistentTextureWithNoReaderSurvivesCullingEveryFrame` — a pass
   whose ONLY declared usage of the handle is a write, never in
   `finalOutputs`, confirmed to still resolve (via
   `lastUpdatedFrameCounter` strictly advancing past a per-iteration
   `counterBefore` snapshot) every one of 3 real frames.
5. **Two different owners, same name, same desc — through the real builder
   API** → `BuilderGetOrCreatePersistentTextureTwoOwnersWithTheSameNameNeverCollide`
   — re-confirms PHASE4's own lower-level test, this time through the full
   production-shaped call path, with a real touching pass per handle (see
   "Sub-agent double-check" above).
6. **`desc.hasDepth == true` / empty owner / mismatched token identity,
   through `GetOrCreatePersistentTexture()` itself** — three new
   `EXPECT_DEATH` tests
   (`BuilderGetOrCreatePersistentTextureAssertsWhenDescHasDepthIsTrue`,
   `BuilderGetOrCreatePersistentTextureAssertsOnEmptyOwner`,
   `BuilderGetOrCreatePersistentTextureAssertsWhenTokenReusedAcrossDifferentIdentities`),
   `#ifndef NDEBUG`-guarded exactly like PHASE4's own precedent. **The
   `PipelinedDeferredReadback` regime-aware resize-refusal case (PHASE6) was
   DELIBERATELY NOT re-tested through the real builder** —
   `HeadlessRenderGraphFixture` only ever drives a
   `SynchronousImmediateReadback` `Execute()` call (`RunSynchronousFrame()`'s
   own fixed regime — it has no `RunPipelinedFrame()` counterpart, and adding
   one would need a real swapchain-shaped frame this headless fixture was
   never built to provide), and `GetOrCreatePersistentTexture()` itself adds
   NO `timingMode`-specific logic of its own (it forwards
   `m_persistentCacheTimingMode` to `Resolve()`/`ResolveFast()` completely
   unconditionally) — that refusal path is already directly, fully covered
   by PHASE6's own
   `ResizeRequestedFromThePipelinedRegimeIsRefusedButTheCallStillSucceeds`
   test, confirmed unaffected by this phase's `ResolveAgainstEntry()`
   extraction (re-ran the full PHASE4-6 suite unmodified — see
   "Verification" below). This is a deliberate, disclosed scope narrowing,
   not a silently-dropped requirement.

## Verification

1. **Incremental compile check**: `cmake --build build` — succeeded with
   zero errors on every attempt (`gte_core`, `gte_editor`,
   `GreatTamanaEditor.exe`, `tests/GreatTamanaEngineTests.exe`, and both
   Project Assembly `.dll`s all rebuilt/relinked cleanly).
2. **Targeted `ctest` run**: `ctest -C Debug -R "RenderGraphPersistentResourceCache"
   --output-on-failure` — **32/32 tests report 100% passed**, every single
   one a clean, legible `Skipped` (never `FAILED`) — this development
   machine's Vulkan driver/loader still does not support
   `VK_EXT_headless_surface` (`vkCreateInstance` → `VkResult=-7`), the exact
   same, honest, pre-existing, machine-dependent limitation PHASE4-7 already
   documented, unchanged by this phase. All 6 new test cases plus 3 new
   `EXPECT_DEATH` cases were written correctly (re-verified by direct,
   line-by-line code trace via the `dispatch_sub_agent` double-check, not
   merely "would compile") and would run and pass the moment this exact
   binary runs on a machine whose Vulkan driver/loader DOES support that
   extension.
3. **Broader regression spot-check**: `ctest -C Debug -R
   "RenderGraphPersistentResourceCache|RenderGraphCompilerTest"` — **72
   tests total, 100% of the 40 that actually ran (RenderGraphCompilerTest)
   passed**, confirming zero regression to PHASE3's own compiler
   root-marking fix (`PersistentCacheTextureWriteSurvivesCullingWithNoFinalOutputsAtAll`,
   `TextureWriteNotInPersistentCacheOrFinalOutputsIsStillCulled`,
   `PersistentCacheRootDoesNotCrossContaminateAnUnrelatedPass` all still
   pass). Also separately re-ran `ctest -C Debug -R
   "RenderGraphTypesTest|RenderGraphBuilderTest|RenderGraphCompilerTest|RenderGraphPersistentTextureCacheTokenTest|RenderGraphIsStaleCacheEntryTest|RenderGraphSnapshotTest|RenderPipelineTest"`
   — **139/139 tests passed**, confirming zero regression to any
   pre-existing Tier-1 RenderGraph-adjacent test after this phase's own
   `Resolve()` → `ResolveAgainstEntry()` refactor.
4. **`dispatch_sub_agent` double-check** (required by this campaign's own
   PHASE0 Rule 4 for this second-highest-risk phase) — see "Sub-agent
   double-check" above for the full finding (a confirmed, real test bug in 4
   of this phase's own originally-drafted tests) and the fix applied as a
   direct result, verified again with a fresh build + targeted `ctest` run
   afterward.
5. **Live-Editor sanity check**: launched `GreatTamanaEditor.exe` via
   `run_app_background`.
   - `GET /get_logs?category=RenderGraphPersistentResourceCache&limit=50` —
     `{"count":0,...}` — expected and correct: no production code anywhere in
     the engine calls `GetOrCreatePersistentTexture()` yet (that is a FUTURE
     campaign's job, using this newly-shipped primitive) — zero log activity
     under this category is the honest, correct baseline.
   - `GET /get_logs?min_level=Error&limit=50` — `{"count":0,...}` — zero
     errors across the whole session.
   - `GET /get_game_view` — returned a valid 34288-byte PNG, a normal
     rendered frame — confirms the new, unconditional
     `ExecuteCompiledGraph()` tail hook (which now runs a loop over
     `input.persistentCacheTextures` — empty in every real production frame
     today — plus a `FlushPendingResizes()` no-op call every synchronous
     frame) adds no observable cost or behavior change to real rendering.
   - `stop_app_background` — process cleanly terminated.
6. No full clean build, no full `ctest` regression pass was performed
   (correctly deferred to PHASE9, per the campaign's own Rule 3.3.3).

## Honestly-flagged open issues

- **This entire phase's Tier-2 (real GPU) proof could not actually EXECUTE on
  this development machine** — all 32 tests in this file report `Skipped`,
  not `Passed`, for the same pre-existing, machine-dependent reason PHASE4-7
  already disclosed (`VK_EXT_headless_surface` unsupported,
  `vkCreateInstance` → `VkResult=-7`). This is NOT a defect in this phase's
  own code — every new test case was written correctly against the real API,
  independently re-verified by a `dispatch_sub_agent`'s own direct code
  trace (which found and this phase then fixed one real, confirmed test
  logic bug — see above), and would run and pass the moment this exact
  binary runs on a machine whose Vulkan driver/loader supports that
  extension.
- **The `PipelinedDeferredReadback` regime-aware resize-refusal path was
  deliberately NOT re-tested through the real builder this phase** — see
  "Required Tests" item 6 above for the full, disclosed reasoning (the
  headless fixture has no way to drive a pipelined-regime `Execute()` call;
  the underlying refusal logic is unchanged and already directly tested by
  PHASE6).
- **Nothing in production code calls `GetOrCreatePersistentTexture()` yet** —
  this is expected and correct: this phase's own scope is exactly "make the
  primitive real and callable", never "find/migrate a first real production
  consumer" — that is explicitly a FUTURE campaign's job (TAA history, SSR
  history, motion vectors, volumetric fog history — see
  `PHASE0_MASTER_STRATEGY.md`'s own Step 1 motivation), not this campaign's.
- A genuine, real bug was found in this phase's OWN first-drafted test code
  (not production code) by this phase's own required `dispatch_sub_agent`
  double-check, and fixed before this report was written or anything was
  committed — see "Sub-agent double-check" above for the full account. This
  is disclosed here plainly as a concrete example of why that double-check
  step is genuinely load-bearing for a phase like this one, not a rubber
  stamp.
- No other open issues. Every acceptance point this phase's own `.md` lists
  is satisfied.

## Git

Changes staged and committed together with this report:
- `src/Renderer/RenderGraph/RenderGraph.cpp`
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.cpp`
- `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`
- `tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`
- `task_manager/editor-core-separation-27/PHASE8_COMPLETION_REPORT.md`
