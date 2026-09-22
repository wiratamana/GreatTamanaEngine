# PHASE1: RenderPassEvent-vs-Real-Dependency Contradiction Detector

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first. Part of the
`render-pass-4` campaign._

## Step 1: The Goal

Give `RenderGraphCompiler` a permanent, always-compiled-in, pure/testable
detector that catches the EXACT bug class documented in PHASE0 Step 2 —
`RenderPassEvent` metadata that contradicts what a pass's real,
compiler-enforced resource dependencies say must happen — the moment it is
introduced, instead of it being rediscovered by staring at a solid-white
Game View. Nothing about actual pass scheduling/ordering changes in this
phase. This is a pure ADDITION: a new function, a new call site inside
`Compile()`, strengthened doc comments, and new tests. If this phase's own
new tests, plus the entire pre-existing test suite, all still pass
unchanged, and `Compile()`'s actual `executionOrder`/`isCulled`/
`textureLifetimes` results are byte-identical to before this phase for
every existing test's input, this phase is done correctly.

## Step 2: The Situation

- `src/Renderer/RenderGraph/RenderGraphCompiler.h`/`.cpp` — `Compile()`'s
  Step 1 (`RenderGraphCompiler.cpp`, ~lines 71-162) builds `edgeExists`
  purely from `lastTextureWriter`/`lastBufferWriter`/`lastVolumeTextureWriter`
  bookkeeping arrays, walking `input.passes` once, in raw declaration
  order (index `i`, `0..passCount-1`). Nothing here reads
  `PassRecord::renderPassEvent` (`RenderGraphTypes.h`, ~line 605).
- `CompiledGraphInput` (`RenderGraphBuilder.h`) is what `Compile()` takes —
  it owns `passes` (a `std::vector<PassRecord>`), `textureDescs`,
  `bufferDescs`, `volumeTextureDescs`. Every `PassRecord` already carries
  its own `.renderPassEvent`, `.reads`, `.writes`, and `.name` (a stable
  `const char*`) — everything this phase's new detector needs is already
  sitting right there; no new field needs to be added to `PassRecord` or
  `CompiledGraphInput` for this phase.
- `ResourceUsage` (`RenderGraphTypes.h`, ~line 445) is a small tagged
  struct: `.kind` (`ResourceKind::Texture`/`Buffer`/`VolumeTexture`),
  plus one of `.texture`/`.buffer`/`.volumeTexture` being the meaningful
  handle, plus `.access`. `TextureHandle`/`BufferHandle`/
  `VolumeTextureHandle` all carry a plain `.index` field usable as a flat
  array index (this is exactly what `Compile()`'s own
  `lastTextureWriter[usage.texture.index]` bookkeeping already relies on).
- **CORRECTION vs. an earlier draft of this doc** (verified directly against
  the file this session): the existing test file,
  `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`, does **NOT**
  hand-poke `CompiledGraphInput`'s fields directly anywhere — its own
  top-of-file comment says so explicitly ("Every fixture below is built
  through a real RenderGraphBuilder... rather than hand-poking
  CompiledGraphInput's fields directly"). EVERY existing test (e.g.
  `LinearChainOrdersWriterBeforeReaderAndCullsNothing`, ~line 58) builds its
  graph through a real `RenderGraphBuilder` (`CreateTexture()`/`AddPass()`/
  `AddRenderPass()`), then calls `.Finish()` to obtain a real
  `CompiledGraphInput`. This phase's own new tests (Step 3.4 below) must
  follow that SAME convention — build via `RenderGraphBuilder` (using the
  `AddRenderPass(name, kind, viewScope, category, setup, execute, drawKind,
  renderPassEvent)` overload, `RenderGraphBuilder.h`, to stamp an explicit
  `RenderPassEvent` per pass), then call `.Finish()` and pass the resulting
  `CompiledGraphInput` directly into `DetectRenderPassEventContradictions()` —
  never `Compile()` for these particular tests, since that would trigger the
  `stderr`/`assert()` reporting this function is deliberately kept separate
  from (see "a real testability trap to avoid" below). There is nothing
  wrong with hand-building a `CompiledGraphInput` in principle (every field
  used here is public), it is simply not this test file's own established
  style — stay consistent with it.
- `assert()` (`<cassert>`) is already used directly, unwrapped, elsewhere
  in this exact folder (`RenderGraphBuilder.cpp`, 5 call sites; `RenderGraphBarrierPlanner.cpp`,
  2 call sites) — there is no project-specific `GTE_ASSERT` macro anywhere
  in `src/`. Use plain `assert()` — per PHASE0's Locked Design Decision 2,
  this is deliberate, not an oversight.
- **A real testability trap to avoid**: if the detector's `assert()` lived
  directly inside a function under test, a test that deliberately
  constructs a contradicting graph (exactly the regression test this phase
  must add) would ABORT THE ENTIRE TEST BINARY the moment it ran, since
  test binaries are built in a configuration where `assert()` is live.
  This is why the detection logic itself MUST be a separate, pure,
  side-effect-free function that only RETURNS data — `Compile()` is the
  ONE place that turns a non-empty result into `stderr` + `assert()`. This
  is not optional polish; it is the only way this phase's own required
  regression test (Locked Design Decision 5) can exist at all without
  crashing the test runner.

## Step 3: The Plan

### 3.1 — New pure function: `DetectRenderPassEventContradictions()`

Add to `RenderGraphCompiler.h` (public, `namespace gte::rg`, right above
the existing `Compile()` declaration):

```cpp
// render-pass-4 campaign, PHASE1
// (PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md) - RenderPassEvent
// is purely descriptive metadata (see its own doc comment,
// RenderGraphTypes.h) - Compile()'s own RAW/WAW dependency scan never
// reads it. This is a SEPARATE, PURE, side-effect-free diagnostic scan
// that cross-checks the two against each other and reports every place
// they disagree - it changes NOTHING about what Compile() actually
// returns. See Compile()'s own doc comment for how a non-empty result is
// turned into a loud, unmissable diagnostic.
enum class RenderPassEventContradictionKind : std::uint8_t {
    // `readerPassIndex` reads a resource with no writer declared anywhere
    // BEFORE it in `processingOrder` - but `writerPassIndex` (declared
    // somewhere AFTER it in `processingOrder`) writes that exact same
    // resource. This is the precise, confirmed root-cause pattern behind
    // the documented, already-occurred "AtmosphereComposite silently
    // culls RenderOpaque" bug (see PHASE0_MASTER_STRATEGY.md, Step 2) -
    // `Compile()`'s own RAW-edge scan can never link this read to that
    // write, because "the most recent pass, among those declared so far,
    // that wrote it" (RenderGraphCompiler.cpp's own header comment) never
    // includes a write that hasn't happened "so far" yet.
    OrphanReadWithLaterWriter,
    // A real RAW dependency was formed - `writerPassIndex` genuinely,
    // provably runs before `readerPassIndex` - but `writerPassIndex`'s own
    // declared RenderPassEvent is LATER (numerically greater) than
    // `readerPassIndex`'s. The real dependency is still honored correctly
    // either way (Compile() doesn't need this to be consistent to be
    // CORRECT) - but the disagreement is a strong signal that one of the
    // two passes has the wrong RenderPassEvent tag for what it actually
    // does, and is worth a human looking at before it becomes a real bug
    // the next time either pass's real dependency set changes.
    DeclaredEventOrderDisagreesWithRealDependency,
};

struct RenderPassEventContradiction {
    RenderPassEventContradictionKind kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
    std::int32_t readerPassIndex = -1;
    std::int32_t writerPassIndex = -1;
    ResourceKind resourceKind = ResourceKind::Texture;
    std::uint32_t resourceIndex = 0;
};

// A pure, Tier-1-testable scan - never prints, never asserts, never
// throws, and never mutates `input`. `processingOrder` must be a
// permutation of `[0, input.passes.size())`, expressing "which order will
// the compiler actually resolve a read's nearest prior writer in" -
// PHASE1 always calls this with the identity permutation (raw declaration
// order, `[0, 1, 2, ...]`), matching Compile()'s own current algorithm
// exactly. PHASE2 (PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md)
// is what will later pass a DIFFERENT permutation here (its new
// RenderPassEvent-sorted "effective order") - this function's signature
// is deliberately already shaped for that, so PHASE2 never has to touch
// this function's own logic, only what it's called with. "Before"/"after"
// in both RenderPassEventContradictionKind enumerators above always means
// "earlier/later in `processingOrder`", never "smaller/larger original
// pass index".
std::vector<RenderPassEventContradiction> DetectRenderPassEventContradictions(
    const CompiledGraphInput& input, std::span<const std::int32_t> processingOrder);
```

Implementation (`RenderGraphCompiler.cpp`, in `namespace gte::rg`, right
above `Compile()`):

```cpp
std::vector<RenderPassEventContradiction> DetectRenderPassEventContradictions(
    const CompiledGraphInput& input, std::span<const std::int32_t> processingOrder)
{
    std::vector<RenderPassEventContradiction> contradictions;

    // Tiny helper: does `usage` refer to the same resource as `other`?
    auto sameResource = [](const ResourceUsage& a, const ResourceUsage& b) {
        if (a.kind != b.kind) {
            return false;
        }
        switch (a.kind) {
        case ResourceKind::Texture:
            return a.texture == b.texture;
        case ResourceKind::Buffer:
            return a.buffer == b.buffer;
        case ResourceKind::VolumeTexture:
            return a.volumeTexture == b.volumeTexture;
        }
        return false;
    };

    for (std::size_t readerPos = 0; readerPos < processingOrder.size(); ++readerPos) {
        const std::int32_t readerIndex = processingOrder[readerPos];
        const PassRecord& reader = input.passes[static_cast<std::size_t>(readerIndex)];

        for (const ResourceUsage& read : reader.reads) {
            // Find the NEAREST writer strictly before readerPos in processingOrder.
            std::int32_t nearestWriterIndex = -1;
            for (std::int32_t candidatePos = static_cast<std::int32_t>(readerPos) - 1; candidatePos >= 0;
                 --candidatePos) {
                const PassRecord& candidate = input.passes[static_cast<std::size_t>(processingOrder[static_cast<std::size_t>(candidatePos)])];
                bool writesIt = false;
                for (const ResourceUsage& write : candidate.writes) {
                    if (sameResource(write, read)) {
                        writesIt = true;
                        break;
                    }
                }
                if (writesIt) {
                    nearestWriterIndex = processingOrder[static_cast<std::size_t>(candidatePos)];
                    break;
                }
            }

            if (nearestWriterIndex == -1) {
                // No writer found before this read in processingOrder - is
                // there one ANYWHERE (i.e. strictly after, in
                // processingOrder terms)?
                for (std::int32_t otherPos = static_cast<std::int32_t>(readerPos) + 1;
                     otherPos < static_cast<std::int32_t>(processingOrder.size()); ++otherPos) {
                    const std::int32_t otherIndex = processingOrder[static_cast<std::size_t>(otherPos)];
                    const PassRecord& other = input.passes[static_cast<std::size_t>(otherIndex)];
                    for (const ResourceUsage& write : other.writes) {
                        if (sameResource(write, read)) {
                            RenderPassEventContradiction contradiction;
                            contradiction.kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
                            contradiction.readerPassIndex = readerIndex;
                            contradiction.writerPassIndex = otherIndex;
                            contradiction.resourceKind = read.kind;
                            contradiction.resourceIndex = read.kind == ResourceKind::Texture ? read.texture.index
                                : read.kind == ResourceKind::Buffer                          ? read.buffer.index
                                                                                              : read.volumeTexture.index;
                            contradictions.push_back(contradiction);
                            break; // One report per (reader, resource) pair is enough.
                        }
                    }
                }
            } else {
                const PassRecord& writer = input.passes[static_cast<std::size_t>(nearestWriterIndex)];
                if (writer.renderPassEvent > reader.renderPassEvent) {
                    RenderPassEventContradiction contradiction;
                    contradiction.kind = RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency;
                    contradiction.readerPassIndex = readerIndex;
                    contradiction.writerPassIndex = nearestWriterIndex;
                    contradiction.resourceKind = read.kind;
                    contradiction.resourceIndex = read.kind == ResourceKind::Texture ? read.texture.index
                        : read.kind == ResourceKind::Buffer                          ? read.buffer.index
                                                                                       : read.volumeTexture.index;
                    contradictions.push_back(contradiction);
                }
            }
        }
    }

    return contradictions;
}
```

This is intentionally a self-contained `O(passCount^2)` (per resource
usage) scan, mirroring `RenderGraphCompiler.cpp`'s own existing
performance philosophy verbatim (its own header comment: "Pass counts in
this engine are single digits today... not expected to grow into the
thousands"). Do not attempt to reuse/share state with `Compile()`'s own
`lastTextureWriter`/etc. bookkeeping arrays — keeping this function fully
independent is what makes it safely callable from a test with an
arbitrary hand-built `CompiledGraphInput`, with no dependency on
`Compile()`'s own internals.

**Correction vs. an earlier draft of this snippet**: a `passCount` local and
a `position[originalPassIndex]` inverse-permutation vector were removed from
the sample above — both were computed but never actually read anywhere in
this function's own body (the loops below index `processingOrder` directly
via `readerPos`/`candidatePos`/`otherPos`, never through a `position[]`
lookup), which would have left two genuinely dead, "written but never read"
locals in the shipped code. If your own real implementation naturally wants
a `passCount`/`position` for some genuine reason (e.g. an added bounds
assertion), that is fine — just do not copy these two symbols into the real
file if nothing ends up reading them, since a Debug build with warnings-as-
errors would then fail to compile on a pure unused-variable warning.

### 3.2 — Wire it into `Compile()`

At the very top of `Compile()` (`RenderGraphCompiler.cpp`), immediately
after the early-return-on-`passCount == 0` check, add:

```cpp
    // render-pass-4 campaign, PHASE1 - a pure diagnostic pre-pass, changing
    // NOTHING about the algorithm below. PHASE1 always checks against raw
    // declaration order (the identity permutation) - matching this
    // function's own current, unchanged algorithm. See
    // DetectRenderPassEventContradictions()'s own doc comment
    // (RenderGraphCompiler.h) for why `processingOrder` is a parameter at
    // all (PHASE2 will pass something else here).
    {
        std::vector<std::int32_t> declarationOrder(static_cast<std::size_t>(passCount));
        for (std::int32_t i = 0; i < passCount; ++i) {
            declarationOrder[static_cast<std::size_t>(i)] = i;
        }
        const std::vector<RenderPassEventContradiction> contradictions =
            DetectRenderPassEventContradictions(input, declarationOrder);
        for (const RenderPassEventContradiction& contradiction : contradictions) {
            const PassRecord& reader = input.passes[static_cast<std::size_t>(contradiction.readerPassIndex)];
            const PassRecord& writer = input.passes[static_cast<std::size_t>(contradiction.writerPassIndex)];
            const char* kindText = contradiction.kind == RenderPassEventContradictionKind::OrphanReadWithLaterWriter
                ? "a read resolved to NO prior writer, but a LATER-declared pass writes the same resource - this "
                  "read may be silently dropped from the dependency graph and its writer may be silently culled"
                : "a real dependency edge was formed, but the writer's own declared RenderPassEvent is LATER than "
                  "the reader's - the two passes' RenderPassEvent tags disagree with their real dependency";
            std::fprintf(stderr,
                "RenderGraphCompiler: RenderPassEvent contradiction detected - reader pass \"%s\" (RenderPassEvent=%s), "
                "writer pass \"%s\" (RenderPassEvent=%s): %s.\n",
                reader.name != nullptr ? reader.name : "<unnamed>", ToString(reader.renderPassEvent),
                writer.name != nullptr ? writer.name : "<unnamed>", ToString(writer.renderPassEvent), kindText);
        }
        assert(contradictions.empty()
            && "RenderGraphCompiler: one or more passes' declared RenderPassEvent contradicts a real (or "
               "possibly-missing) resource dependency - see the stderr output immediately above this assert for "
               "exactly which passes and why. See task_manager/render-pass-4/PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md.");
    }
```

Confirm `#include <cassert>` and `#include <cstdio>` are present in
`RenderGraphCompiler.cpp` (add if missing).

### 3.3 — Strengthen doc comments (rename nothing — Locked Design Decision 3)

Update, in place, WITHOUT changing the enum/field/parameter NAMES
themselves:

- `RenderGraphTypes.h`'s `RenderPassEvent` enum doc comment (~lines
  403-416): keep the existing "purely descriptive sort hint... NOT a
  dependency mechanism" wording (it remains 100% true after this phase),
  but ADD a new paragraph pointing at this campaign: *"As of the
  `render-pass-4` campaign, `RenderGraphCompiler::Compile()` cross-checks
  this field against every pass's real, declared resource dependencies —
  see `DetectRenderPassEventContradictions()` (`RenderGraphCompiler.h`).
  A wrong tag here is no longer silent: it is reported to `stderr` and,
  in debug builds, fails an `assert()`. This still does not make
  `RenderPassEvent` itself a scheduling mechanism — see
  `task_manager/render-pass-4/PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md`
  for the phase that actually changes that."*
- `PassRecord::renderPassEvent`'s own field comment (~lines 595-605):
  same addition, shorter — point at the enum's own comment instead of
  repeating the whole paragraph.
- `RenderPipeline.h`'s header comment (~lines 1-30) and its
  `RenderPassDesc::order` field comment (~lines 163-169): add a one-line
  pointer to the same campaign/detector, matching the tone above.
- `RenderGraphBuilder.h`'s `AddRenderPass()` overloads' `renderPassEvent`
  parameter doc comments (~lines 444-460, ~477-489): same one-line pointer.

Do not weaken or delete any EXISTING sentence in any of these comments —
every one of them is still an accurate description of what the code does
today; this phase only ADDS the new, accurate fact that a detector now
exists.

### 3.4 — Tests

In `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`, add a new
test group exercising `DetectRenderPassEventContradictions()` directly
(pure, no `Compile()` call needed, no `assert()`/`stderr` involved at all
— this is exactly why 3.1 made it a separate, pure function):

- `NoReadsOrWritesProducesNoContradictions` — sanity/empty-input case.
- `NormalWriterBeforeReaderProducesNoContradictions` — the ordinary,
  correct case (mirrors `LinearChainOrdersWriterBeforeReaderAndCullsNothing`'s
  own graph shape) with matching, correctly-ordered `RenderPassEvent`
  values — must return an empty vector.
- `OrphanReadWithLaterWriterIsDetected` — **the permanent regression test
  for the exact historical bug (Locked Design Decision 5)**: via a real
  `RenderGraphBuilder`, create one texture handle, then declare pass 0
  (`"Composite"`, `RenderPassEvent::AfterTransparents`, reads that texture)
  BEFORE pass 1 (`"Opaque"`, `RenderPassEvent::Opaques`, writes that SAME
  texture) — i.e. the writer is declared textually AFTER the reader,
  exactly reproducing `AtmosphereComposite`/`RenderOpaque`'s real,
  historical shape. Call `.Finish()` to get a `CompiledGraphInput`, then
  call `DetectRenderPassEventContradictions()` directly with the identity
  permutation (`{0, 1}`) — never `Compile()` here. Assert the returned
  vector has exactly one entry, `kind == OrphanReadWithLaterWriter`,
  `readerPassIndex == 0`, `writerPassIndex == 1`.
- `EdgeContradictingDeclaredEventOrderIsDetected` — via a real
  `RenderGraphBuilder`, declare pass 0 (`"Opaque"`,
  `RenderPassEvent::AfterTransparents` — DELIBERATELY mistagged) writing a
  texture, THEN pass 1 (`"Composite"`, `RenderPassEvent::Opaques` — also
  mistagged, relative to `"Opaque"`) reading it. Call `.Finish()` then
  `DetectRenderPassEventContradictions()` with the identity permutation
  (`{0, 1}`) — a real edge exists (writer really is declared/processed
  first), but `writer.renderPassEvent (AfterTransparents) >
  reader.renderPassEvent (Opaques)`. Assert exactly one
  `DeclaredEventOrderDisagreesWithRealDependency` entry.
- `SameEventTierNeverProducesAContradiction` — two passes both tagged
  `RenderPassEvent::Opaques`, writer before reader — must return empty
  (equal tiers are never a contradiction, by definition: `writer.event >
  reader.event` is false when they're equal).
- `CallingCompileWithAConsistentGraphNeverAborts` — build a full,
  realistic, CORRECTLY-ordered-and-tagged multi-pass graph (reuse/adapt
  an existing test's input, e.g. `DiamondDependencyOrdersCorrectlyWithDeterministicSiblingOrder`'s
  own graph shape) and call the REAL `Compile()` end-to-end — confirms
  3.2's new wiring doesn't fire a false positive against a graph that
  was always fine, and that `executionOrder`/`isCulled` are unchanged
  from what that same test already asserts elsewhere.

Register nothing new in `tests/CMakeLists.txt` — this all lives in the
already-registered `RenderGraphCompilerTests.cpp`.

## Definition of Done

- `RenderGraphCompiler.h` declares `RenderPassEventContradictionKind`,
  `RenderPassEventContradiction`, and `DetectRenderPassEventContradictions()`.
- `RenderGraphCompiler.cpp` defines `DetectRenderPassEventContradictions()`
  exactly as a pure function (no `stderr`, no `assert`, no mutation of
  its `input` parameter), and `Compile()` calls it once at the top, then
  turns a non-empty result into an unconditional `stderr` report followed
  by a plain `assert()`.
- Every doc comment listed in Step 3.3 is updated, with no enum/field/
  parameter renamed anywhere.
- Every new test in Step 3.4 passes; the entire pre-existing
  `RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp`/
  `RenderPipelineTests.cpp` suites still pass unchanged (confirming zero
  scheduling-behavior regression).
- An incremental compile of `gte_core` and `GreatTamanaEngineTests`
  succeeds, and running the test binary (or `ctest -R RenderGraphCompilerTest`)
  locally shows every test above passing.
- `search_in_dir` for `RenderPassEvent` still returns every one of its
  pre-existing hits, byte-identical, EXCEPT for the doc-comment additions
  from 3.3 — confirming nothing was accidentally renamed.

## What We Will NOT Do

- Do NOT change `Compile()`'s actual `executionOrder`/`isCulled`/
  lifetime-computation algorithm at all in this phase — that is PHASE2's
  entire job. This phase only ADDS a diagnostic pre-pass.
- Do NOT rename `RenderPassEvent` or any of its enumerators.
- Do NOT touch `RenderPipeline.h`'s `ProviderTiming`/`DeclareOnePhase()`
  logic — per PHASE0's Locked Design Decision 4, the known immediate-vs-
  deferred gap there is left exactly as-is; this phase's detector already
  covers it for free once `Compile()` runs against the final, flattened
  `CompiledGraphInput::passes` list (which includes every pass regardless
  of which layer declared it).
- Do NOT gate the detection-and-report logic behind `#ifndef NDEBUG` —
  per Locked Design Decision 2, the `stderr` report must be unconditional;
  only `assert()`'s own, already-standard behavior provides the
  Debug-vs-Release split.
- Do NOT invent a new assert macro — use plain `assert()`.
