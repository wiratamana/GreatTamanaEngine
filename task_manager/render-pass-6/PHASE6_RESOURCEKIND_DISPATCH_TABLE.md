# PHASE6 — Generic `ResourceKind` Dispatch (item 2.2)

⚠️ **This phase gets its own dedicated `delegate_task` double-check pass
immediately after it lands, BEFORE the whole-campaign second-iteration
double-check** — see `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 5
(confirmed via `ask_questions` specifically to include THIS phase, beyond
render-pass-5's own two-dedicated-phase precedent, because a bug here
silently miscompiles every resource kind's barrier/lifetime/culling logic at
once — the single highest blast-radius change in this whole campaign). The
dedicated double-check must overwrite THIS file in place if it finds
something worth fixing — never create a new numbered file.

**Recursive `ask_questions` rule**: whoever executes this phase (and anything
it further delegates via `delegate_task`) must use the `ask_questions` tool
whenever it hits a genuine ambiguity or a design choice this document doesn't
already pin down, and must repeat this exact same instruction to anything it
delegates further down the chain.

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first). This phase directly depends on
BOTH `PHASE4_COMPLETION_REPORT.md` (the adjacency-list rewrite — see Step 2's
own "recount before you start" warning below, this is not optional context)
AND `PHASE5_COMPLETION_REPORT.md`'s `TextureSlot`/`BufferSlot`/
`VolumeTextureSlot` shape already existing — do not start this phase until
PHASE4 and PHASE5 (and each phase's own dedicated double-check) have fully
landed.

## Step 1: The Goal (Where are we going?)

Replace every independently hand-rolled `switch (usage.kind) { Texture /
Buffer / VolumeTexture }` (or `switch (a.kind)` — the same shape, spelled with
a different local variable name) block living inside
`src/Renderer/RenderGraph/RenderGraph.cpp`, `RenderGraphCompiler.cpp`, and
`RenderGraphSnapshot.cpp` with one small, generic per-kind dispatcher every
one of them routes through instead. **As confirmed directly against today's
source (2026-09-23), this is exactly SEVEN call sites today — see Step 2 for
the full list — but by the time this phase actually starts, PHASE4 has landed
and very likely added at least two MORE physical `switch (usage.kind)` blocks
to `RenderGraphCompiler.cpp` itself (see Step 2's own "recount before you
start" section). Treat "seven" as today's confirmed baseline, never as a
number to hardcode into this phase's own Definition of Done — the actual,
binding requirement is "every hand-rolled `ResourceKind` three-way branch
inside these three files, whatever the real count turns out to be once PHASE4
has landed, routes through the one dispatcher."** Adding a 4th `ResourceKind`
in the future becomes "add one table + one enumerator", not "grep N files by
hand" — **and the new dispatcher must still fail to compile if a 4th
`ResourceKind` is added without updating it**, preserving this codebase's
"no `default:` case, ever" exhaustive-switch discipline exactly.

## Step 2: The Situation (Where are we now?)

### The seven call sites confirmed against TODAY's (pre-PHASE4/PHASE5) source

Directly re-verified by grep against the real, current files as part of this
document's own most recent review (exact 0-based line numbers, not
approximations):

1. **`RenderGraph.cpp:170`, `ApplyUsageBarrierIfNeeded()`** — the
   richest of the seven: Texture branch resolves via
   `EnsureTextureResolved()`, computes `isDepthAccess`, picks `colorState`
   vs `depthState`, computes `next` via `RequiredStateFor()`, conditionally
   emits an IMAGE barrier with a computed aspect mask. Buffer branch
   resolves via `EnsureBufferResolved()`, computes `next`, conditionally
   emits a BUFFER barrier. VolumeTexture branch resolves via
   `EnsureVolumeTextureResolved()`, computes `next` (always non-depth),
   conditionally emits an IMAGE barrier with a fixed color aspect mask.
   **This site has NO existing automated test coverage** — see its own
   dedicated note under "Tier-2 coverage gap" below; this is the one call
   site whose regression safety cannot be a passing `ctest` run alone.
2. **`RenderGraphCompiler.cpp:59`, `DetectRenderPassEventContradictions()`'s
   `sameResource` lambda** (spelled `switch (a.kind)`) — trivial:
   `a.texture == b.texture` / `a.buffer == b.buffer` /
   `a.volumeTexture == b.volumeTexture`. **See this site's own dedicated
   note under "PHASE4/PHASE6 interaction" below before touching it** — its
   parent function carries an explicit "must not be modified at all" clause
   in `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md` that needed a documented
   resolution.
3. **`RenderGraphCompiler.cpp:286`, `Compile()`'s RAW-edge scan** —
   resolves `writer` from `lastTextureWriter[usage.texture.index]` /
   `lastBufferWriter[...]` / `lastVolumeTextureWriter[...]` (with a bounds
   check each), then calls `addEdge(writer, i)`.
4. **`RenderGraphCompiler.cpp:311`, `Compile()`'s WAW-edge scan** —
   near-identical shape to #3, but WRITES `lastXWriter[index] = i` after
   calling `addEdge()`.
5. **`RenderGraphCompiler.cpp:371`, `Compile()`'s root-marking scan** —
   Texture checks `ContainsTextureHandle(finalOutputs, ...)`, Buffer
   is always `false` (a buffer can never be a root), VolumeTexture checks
   `ContainsVolumeTextureHandle(input.finalVolumeTextureOutputs, ...)`.
6. **`RenderGraphCompiler.cpp:498`, `Compile()`'s lifetime `touch()` lambda**
   — resolves which of `result.textureLifetimes`/`bufferLifetimes`/
   `volumeTextureLifetimes` AND which index to touch — **this is the one
   site where "resolve an index" undersells the real shape: it genuinely
   needs to resolve TWO things (which vector, and which index into it), not
   one — see Step 3.2's own concrete recipe for this site, which resolves
   both in a single pointer return instead.**
7. **`RenderGraphSnapshot.cpp:64`, `ResourceUsageName()`** — resolves
   a display name; post-PHASE5, from `input.textures[...].name` /
   `input.buffers[...].name` / `input.volumeTextures[...].name` (with a
   bounds + null check each) — today, pre-PHASE5, still
   `input.textureNames[...]` etc. By the time this phase runs, PHASE5 has
   already landed and this is the slot-vector shape.

Every one of these branches on the SAME `ResourceKind` enum
(`RenderGraphTypes.h`, lines 321-337: `Texture`, `Buffer`, `VolumeTexture`),
and every one is written as an exhaustive `switch` with **no `default:`
case** — this is a deliberate, load-bearing, repeatedly-documented
convention (`RenderGraphTypes.h`'s own comment: "every place that branches
on ResourceKind... was audited and converted... BEFORE this enumerator was
added"). **This exact compile-time safety net is the single hardest
constraint this phase must preserve.**

### Recount before you start — PHASE4 very likely adds at least two more sites

`PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`'s own Step 3.4 introduces, inside
`Compile()`, two brand-new pieces of bookkeeping that are themselves shaped
as fresh, physical `switch (usage.kind) { ... }` blocks:

- A **`firstTextureWriter`/`firstBufferWriter`/`firstVolumeTextureWriter`
  prescan loop**, run once before Step 1's main walk, that has its own
  three-way `switch (usage.kind)` resolving which `firstXWriter` vector and
  index to update.
- An **inline "does a later pass write this same resource" fast-path
  check**, folded into Step 1's existing reads-loop right where the RAW-edge
  `writer` is already computed, with its OWN separate `switch (usage.kind)`
  resolving `laterWriter` from `firstTextureWriter`/`firstBufferWriter`/
  `firstVolumeTextureWriter`.

`PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`'s own "What NOT to do" section
characterizes these two as legitimately EXTENDING the established
`switch (usage.kind)` pattern rather than inventing a new one — true in
SPIRIT (same enum, same exhaustive-no-`default:` shape, same house style) —
but they are, mechanically, two ADDITIONAL physical `switch` statements in
`RenderGraphCompiler.cpp` that did not exist when this document's "seven"
count was taken. **Before starting Step 3.2's conversion work, re-grep the
ACTUALLY-LANDED `RenderGraphCompiler.cpp` (and `RenderGraph.cpp`/
`RenderGraphSnapshot.cpp`, in case either changed unexpectedly too) for every
`switch (usage.kind)` / `switch (a.kind)` / equivalent `ResourceKind`
three-way branch, and treat THAT fresh count — very likely nine, not seven —
as the actual, authoritative list to convert.** Do not be surprised or
alarmed if the count grew; do not silently convert only the original seven
and leave PHASE4's two new additions as hand-rolled switches — that would
directly defeat this phase's own stated goal ("one generic dispatcher every
call site routes through," not "every call site that existed when this
document was drafted").

### PHASE4/PHASE6 interaction — Site 2's resolved status

`PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`'s Step 3.4 and Definition of Done
both state that `DetectRenderPassEventContradictions()` — the standalone
function Site 2's `sameResource` lambda lives inside — "must NOT be modified
at all... byte-for-byte unchanged," specifically so it keeps serving as
PHASE4's own permanent correctness oracle (the fast-path/standalone
equivalence test compares the new inline `Compile()` logic's results against
this function's UNCHANGED output). Read literally, that clause would forbid
PHASE6 from touching Site 2 at all — directly contradicting this phase's own
plan to convert it. **This was raised as a genuine ambiguity via
`ask_questions` during this document's own review, and resolved by the
project owner: PHASE4's "byte-for-byte unchanged" requirement protects
`DetectRenderPassEventContradictions()`'s OBSERVABLE behavior — its public
signature, and the exact results it returns for any given input (which is
what its own existing direct-call tests, and PHASE4's equivalence test,
actually assert on) — not its literal internal source text.** PHASE6 MAY
therefore convert Site 2's internal `sameResource` lambda from a hand-rolled
`switch (a.kind)` to a `DispatchByKind()` call, exactly like every other
site, **as long as**:

- `DetectRenderPassEventContradictions()`'s public signature is completely
  unchanged.
- Its return value, for every existing test's exact input, is byte-for-byte
  identical before and after this conversion (this is a pure mechanical
  switch → dispatcher-call transform with the same comparison logic inside
  each lambda — it should trivially hold, but confirm it explicitly by
  re-running every one of its existing direct-call tests, unmodified,
  post-conversion).
- PHASE4's own dedicated fast-path/standalone equivalence test (see
  `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`, Step 3.4/3.5) still passes
  after this conversion — this is the actual proof that Site 2's conversion
  did not silently change what the oracle reports.

(`PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md` itself is a sibling phase
document and is intentionally NOT edited by this document to reflect this
resolution — do not edit it either; this note is the authoritative record of
the resolution for anyone executing PHASE6.)

### An incidental, harmless side effect of Step 3.3's verification you should expect, not chase

`src/Editor/FrameDebuggerData.cpp` contains two of its OWN small, exhaustive,
`default:`-less `switch (kind)` functions over `rg::ResourceKind` —
`ReadRowLabelForKind()` and `WriteRowLabelForKind()` (simple UI row-label
lookups, nothing to do with barriers/lifetime/culling). **These are NOT
among the seven-or-more sites this phase converts** — they live in the
Editor layer, serve a completely different purpose, and are explicitly out
of this campaign's scope (`PHASE0_MASTER_STRATEGY.md`'s Non-Goals never
mentions Editor code). However, Step 3.3's required "add a scratch 4th
`ResourceKind` enumerator and confirm the build fails" verification WILL
also break these two functions (they already exhaustively switch on
`ResourceKind` with no `default:`, by the same established convention) —
**this is expected, correct, and a GOOD sign the exhaustiveness discipline
is healthily enforced beyond just this campaign's own three files, not a
sign you missed a conversion site inside `src/Renderer/RenderGraph/`.** Do
not "fix" `FrameDebuggerData.cpp` as part of this phase, and do not be
confused by extra compiler errors pointing there during the scratch
verification — just confirm `DispatchByKind()`'s own switch is genuinely
one of the reported failures, alongside whatever else also (correctly)
fails.

### Tier-2 coverage gap — Site 1 has no automated regression oracle

`ApplyUsageBarrierIfNeeded()` (Site 1) is GPU-dependent Tier-2 code per
`AGENTS.md`'s "Testability & Regression Safety" section — it calls
`EmitImageBarrier()`/`EmitBufferBarrier()`, which record real
`vkCmdPipelineBarrier2` calls against a live `VkCommandBuffer`.
`tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp` — confirmed
by reading its own header comment and test bodies — deliberately tests ONLY
the pure, Vulkan-device-free decision functions this site calls
(`RequiredStateFor()`/`RequiresBarrier()`/`TargetsDepthState()`), which this
phase does not touch at all; it explicitly does NOT exercise
`ApplyUsageBarrierIfNeeded()`'s own body. **There is therefore no automated
test that would catch a logic-preservation mistake made while converting
Site 1's switch to `DispatchByKind()`** — a passing `ctest` run after this
phase proves the untouched pure decision functions still work, not that
Site 1's conversion preserved behavior. Compensate for this explicitly (see
Step 3.4's own requirement) with a live, HTTP-driven smoke test
(`GET /get_swapchain`/`GET /get_game_view` before/after comparison, and/or
opening the Frame Debugger and confirming barrier-dependent
resource-state transitions still look correct) — do not rely on "the build
compiles and `ctest` is green" alone as proof Site 1 is unchanged.

## Step 3: The Plan (How do we get there?)

### 3.1 — Design the generic dispatcher

The confirmed sites do NOT all need the same "shape" of generic help — some
need "resolve an index into the right one of 3 vectors" (#3, #4, #7), one
needs "resolve a pointer directly at the right lifetime/writer slot" (#6, and
optionally #3/#4 too — see the concrete recipes below), some need
"resolve+emit a barrier" (#1, the richest one), and one is a trivial equality
check (#2). A single, one-size-fits-all `DispatchByKind()` free function
template is the right tool for every one of these shapes — the dispatcher's
job is only to guarantee exhaustiveness and eliminate the repeated `switch`
boilerplate, never to force identical logic into each arm.

Add to `RenderGraphTypes.h` (this is genuinely core vocabulary, alongside
`ResourceKind`/`ResourceUsage` themselves) a single generic function
template. **This needs `#include <cassert>` and `#include <stdexcept>`
added to this header's existing include list** (today: `<array>`,
`<cstddef>`, `<cstdint>`, `<functional>`, `<optional>`, `<vector>`):

```cpp
// render-pass-6 campaign, PHASE6 (item 2.2) - REPLACES every independently
// hand-rolled `switch (usage.kind) { Texture / Buffer / VolumeTexture }`
// block scattered across RenderGraph.cpp/RenderGraphCompiler.cpp/
// RenderGraphSnapshot.cpp with ONE generic dispatch point every one of them
// routes through instead. `usage.kind` still drives a REAL, exhaustive
// switch internally - deliberately NOT a `default:`-having fallback of any
// kind, so a future 4th ResourceKind enumerator STILL fails to compile here
// until every call site's own three (soon four) lambdas are updated - this
// is the exact same "no default: case, ever" guarantee IsWriteAccess()/
// ToString() already provide for ResourceAccess, extended to ResourceKind's
// own dispatch shape.
//
// `onTexture`/`onBuffer`/`onVolumeTexture` are three separate callables
// (never one generic templated callable dispatched via `if constexpr`) -
// this is deliberate: three DISTINCT lambda parameters, one per kind, means
// a caller who forgets to handle one kind gets a real compile error
// (missing function argument) rather than a silently-empty generic body -
// mirroring this file's own general "plain, explicit code over template-
// heavy machinery" house style (see ResourceUsage's own doc comment on why
// it is a tagged struct, not a std::variant).
//
// All three callables must return the SAME type (C++ requires every return
// statement inside a function with a deduced `auto` return type to agree) -
// `void` is completely valid (Site 1 uses this: all the divergent per-kind
// logic lives inside the three lambdas, the dispatcher itself returns
// nothing), as is a shared pointer type (e.g. `ResourceLifetime*`, letting
// one lambda per kind resolve BOTH "which vector" and "which index" in one
// step - see Site 6's own recipe, Step 3.2) or a shared plain value type
// (`std::int32_t`, `bool`, `std::string`, ...). This is a per-call-site
// choice, re-checked independently at each of this template's own
// instantiations - it is never a global constraint on DispatchByKind()
// itself.
template <typename TextureFn, typename BufferFn, typename VolumeTextureFn>
auto DispatchByKind(const ResourceUsage& usage, TextureFn&& onTexture, BufferFn&& onBuffer,
    VolumeTextureFn&& onVolumeTexture)
{
    switch (usage.kind) {
    case ResourceKind::Texture:
        return onTexture(usage.texture);
    case ResourceKind::Buffer:
        return onBuffer(usage.buffer);
    case ResourceKind::VolumeTexture:
        return onVolumeTexture(usage.volumeTexture);
    }
    // Unreachable in practice - the switch above is exhaustive over all 3
    // current ResourceKind enumerators with no default: case, so this line
    // is only ever reached if a caller somehow constructed an out-of-range
    // ResourceKind value directly (never possible through any real
    // ResourceUsage::ForTexture()/ForBuffer()/ForVolumeTexture() factory).
    // Kept ONLY for the compiler's own "not all control paths return a
    // value" diagnostic on some compilers/warning levels (MSVC's C4715 in
    // particular, which can fire even for a switch that visibly covers
    // every named enumerator, since it cannot statically rule out a value
    // outside the enum's named range at runtime).
    //
    // Deliberately DOES NOT call onTexture/onBuffer/onVolumeTexture again
    // as a fallback (unlike naively mirroring IsWriteAccess()/ToString()'s
    // own "return a safe sentinel" convention) - those two functions return
    // a concrete, known, always-safe sentinel type (bool/const char*); this
    // template's return type is caller-chosen and may be a pointer, a pair,
    // or anything else with no safe default - and, more importantly,
    // re-invoking one of the three callables here would silently RE-RUN
    // real, possibly side-effecting per-kind logic (e.g. Site 1 emitting a
    // second GPU barrier) for a usage.kind value that, if this line is ever
    // genuinely reached, is already corrupt - doing nothing and failing
    // loudly is strictly safer than doing something wrong. A thrown
    // exception is valid for ANY deduced return type (including void),
    // since a path that always throws never needs to produce a value.
    assert(false && "DispatchByKind: ResourceUsage::kind held a value outside ResourceKind's 3 named enumerators");
    throw std::logic_error(
        "DispatchByKind: ResourceUsage::kind held a value outside ResourceKind's 3 named enumerators");
}
```

### 3.2 — Convert each call site

For each, replace the existing `switch (usage.kind) { case
ResourceKind::Texture: ...; case ResourceKind::Buffer: ...; case
ResourceKind::VolumeTexture: ...; }` with a single
`DispatchByKind(usage, [&](TextureHandle h) { ... }, [&](BufferHandle h) {
... }, [&](VolumeTextureHandle h) { ... });` call, moving each case's
existing body VERBATIM into the corresponding lambda (same logic, same
variable captures via `[&]`, same computed values) — this is a pure
mechanical transform, not a rewrite of any actual logic, for every site
below.

**Site 2 (`sameResource` lambda) is the simplest — do this one FIRST as a
low-risk warm-up.** See Step 2's own "PHASE4/PHASE6 interaction" note above
before touching it — converting it is explicitly permitted, as long as
`DetectRenderPassEventContradictions()`'s own existing direct-call tests AND
PHASE4's fast-path/standalone equivalence test both still pass unmodified
afterward:
```cpp
auto sameResource = [](const ResourceUsage& a, const ResourceUsage& b) {
    if (a.kind != b.kind) {
        return false;
    }
    return DispatchByKind(a,
        [&](TextureHandle) { return a.texture == b.texture; },
        [&](BufferHandle) { return a.buffer == b.buffer; },
        [&](VolumeTextureHandle) { return a.volumeTexture == b.volumeTexture; });
};
```

**Sites 3/4 (RAW/WAW writer-index resolution) — recommended shared helper,
concrete recipe** (not merely "consider a shared helper" — this exact shape
resolves both sites cleanly and is the recommended default unless it turns
out to complicate the diff for no clear win once the real, post-PHASE4 code
is in front of you):
```cpp
// Returns a pointer directly at the resolved lastXWriter slot for `usage`,
// or nullptr if its index is out of bounds - a single shared type
// (std::int32_t*) across all three ResourceKind branches, satisfying
// DispatchByKind()'s "same return type" requirement while letting both the
// RAW-scan (read-only) and WAW-scan (read-then-write) sites share one
// resolution step instead of duplicating the bounds-checked indexing logic
// twice.
auto lastWriterSlotFor = [&](const ResourceUsage& usage) -> std::int32_t* {
    return DispatchByKind(usage,
        [&](TextureHandle h) -> std::int32_t* {
            return h.index < lastTextureWriter.size() ? &lastTextureWriter[h.index] : nullptr;
        },
        [&](BufferHandle h) -> std::int32_t* {
            return h.index < lastBufferWriter.size() ? &lastBufferWriter[h.index] : nullptr;
        },
        [&](VolumeTextureHandle h) -> std::int32_t* {
            return h.index < lastVolumeTextureWriter.size() ? &lastVolumeTextureWriter[h.index] : nullptr;
        });
};

// RAW-edge scan (Site 3) - a missing/out-of-bounds slot behaves exactly
// like today's code (writer stays -1, addEdge() is then a documented no-op).
for (const ResourceUsage& usage : pass.reads) {
    const std::int32_t* slot = lastWriterSlotFor(usage);
    addEdge(slot != nullptr ? *slot : -1, i);
}

// WAW-edge scan (Site 4) - only touches lastXWriter when the slot genuinely
// exists, exactly matching today's "if (index < size) { addEdge(...);
// lastXWriter[index] = i; }" guard - an out-of-bounds usage gets no edge and
// no write, identical to today.
for (const ResourceUsage& usage : pass.writes) {
    std::int32_t* slot = lastWriterSlotFor(usage);
    if (slot != nullptr) {
        addEdge(*slot, i);
        *slot = i;
    }
}
```
If PHASE4 also introduced the `firstXWriter` prescan/inline fast-path check
(see Step 2's "recount before you start"), the exact same `lastWriterSlotFor`
SHAPE (a second, sibling helper capturing `firstTextureWriter`/
`firstBufferWriter`/`firstVolumeTextureWriter` instead) applies there too —
convert it the same way, do not leave it as a hand-rolled switch just
because it was added after this document's original draft.

**Site 1 (`ApplyUsageBarrierIfNeeded`) — the highest-value, highest-risk
conversion, and the one with NO automated regression oracle (see Step 2's
"Tier-2 coverage gap")**: convert the outer `switch (usage.kind)` to
`DispatchByKind(usage, [&](TextureHandle textureHandle) { ... /* entire
existing Texture case body, using textureHandle instead of
usage.texture */ ... }, [&](BufferHandle bufferHandle) { ... }, [&
](VolumeTextureHandle volumeHandle) { ... });`, with a `void` return (this
site's three branches genuinely cannot be unified into one same-return-type
dispatch — Texture legitimately does meaningfully more work than Buffer/
VolumeTexture, per this file's own established rationale for why Texture
needs its own richer branch). Every existing computed value/comment inside
each case body is preserved verbatim — only the outer dispatch mechanism
changes. **After converting this site, perform the live/manual smoke test
described in Step 2's "Tier-2 coverage gap" note before considering this
site done** — this is not optional given the complete absence of an
automated oracle here.

**Site 6 (lifetime `touch()` lambda) — concrete recipe, resolving BOTH
"which vector" and "which index" in one pointer return**:
```cpp
auto touch = [&](const ResourceUsage& usage) {
    ResourceLifetime* lifetime = DispatchByKind(usage,
        [&](TextureHandle h) -> ResourceLifetime* {
            return h.index < result.textureLifetimes.size() ? &result.textureLifetimes[h.index] : nullptr;
        },
        [&](BufferHandle h) -> ResourceLifetime* {
            return h.index < result.bufferLifetimes.size() ? &result.bufferLifetimes[h.index] : nullptr;
        },
        [&](VolumeTextureHandle h) -> ResourceLifetime* {
            return h.index < result.volumeTextureLifetimes.size() ? &result.volumeTextureLifetimes[h.index] : nullptr;
        });
    if (lifetime == nullptr) {
        return;
    }
    if (lifetime->firstUsePassIndex == -1) {
        lifetime->firstUsePassIndex = static_cast<std::int32_t>(pos);
    }
    lifetime->lastUsePassIndex = static_cast<std::int32_t>(pos);
};
```
This is strictly simpler than the original two-local-variable
(`lifetimes`/`index`) shape it replaces, not just a mechanical transform —
confirm it produces IDENTICAL `ResourceLifetime` results against every
existing `RenderGraphCompilerTests.cpp` lifetime assertion.

**Sites 5/7, and PHASE4's two likely-new sites (the `firstXWriter` prescan
and its inline fast-path consumer — see Step 2)**: same mechanical
transform (Site 5), or the `lastWriterSlotFor`-style shared-pointer pattern
(the two PHASE4 additions, mirroring Sites 3/4's own recipe above) — in
every case, existing case bodies moved verbatim into the three lambdas.

### 3.3 — Verify the exhaustiveness guarantee, concretely

After converting every confirmed site (whatever the final count turned out
to be — see Step 2), perform this REQUIRED verification (not optional, given
this phase's own stated risk): temporarily (in a scratch, NEVER-committed
local edit) add a 4th dummy enumerator to `ResourceKind` (e.g.
`ResourceKind::_ScratchFourthKindForVerificationOnly`) and attempt an
incremental compile. **Expect MORE than one compile error** — `DispatchByKind()`'s
own switch will fail (the actual proof this phase's compile-time safety net
holds), and `src/Editor/FrameDebuggerData.cpp`'s `ReadRowLabelForKind()`/
`WriteRowLabelForKind()` will ALSO fail, harmlessly and correctly, for the
reason explained in Step 2's own dedicated note — do not be thrown off by
this second, expected, out-of-scope failure. Confirm specifically that
`DispatchByKind()`'s own switch is one of the reported failures (a "not all
control paths return a value" or "enumerator not handled in switch"
diagnostic, depending on compiler) — this proves the exhaustiveness
guarantee genuinely holds through the new dispatcher, not merely "by
inspection". **Immediately revert this scratch edit** before committing
anything — it must never land in the actual commit.

### 3.4 — Tests

`DispatchByKind()` itself is a small, pure, Tier-1-testable function
template — add new tests to `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`
(the file that already tests `ResourceUsage`/`ResourceKind` themselves — see
its own existing `RenderGraphResourceUsageTest` suite for the naming/style
convention to match) in a new `RenderGraphDispatchByKindTest` suite, mirroring
that file's existing table-driven precedent:

```cpp
TEST(RenderGraphDispatchByKindTest, TextureUsageDispatchesOnlyToOnTextureWithTheCorrectHandle)
{
    const ResourceUsage usage = ResourceUsage::ForTexture(TextureHandle{ 5, 2 }, ResourceAccess::ShaderRead);
    bool onTextureCalled = false;
    bool onBufferCalled = false;
    bool onVolumeTextureCalled = false;
    TextureHandle received;

    DispatchByKind(usage,
        [&](TextureHandle h) { onTextureCalled = true; received = h; },
        [&](BufferHandle) { onBufferCalled = true; },
        [&](VolumeTextureHandle) { onVolumeTextureCalled = true; });

    EXPECT_TRUE(onTextureCalled);
    EXPECT_FALSE(onBufferCalled);
    EXPECT_FALSE(onVolumeTextureCalled);
    EXPECT_EQ(received, (TextureHandle{ 5, 2 }));
}
```
Add the analogous `BufferUsageDispatchesOnlyToOnBufferWithTheCorrectHandle`
and `VolumeTextureUsageDispatchesOnlyToOnVolumeTextureWithTheCorrectHandle`
cases, and one more confirming a non-`void` return type round-trips
correctly (e.g. `DispatchByKindWithNonVoidReturnTypeProducesTheExpectedValue`,
dispatching to three lambdas that each return a distinct `std::int32_t`
sentinel and asserting the right one comes back for each of the three
`ResourceUsage::For*()` factories). This is a genuinely new, cheap-to-add
Tier-1 test this phase's own refactor opens up, per `AGENTS.md`'s "every
change to Tier 1 code must come with a matching test change" rule
(`RenderGraphTypes.h`/`.cpp` is unambiguously Tier-1).

Re-run every existing test file touched by the mechanical conversions in 3.2:
`RenderGraphCompilerTests.cpp` (Sites 2/3/4/5/6, and PHASE4's own two new
sites if present) and `RenderGraphSnapshotTests.cpp` (Site 7) must show ZERO
regressions and ZERO required assertion changes — these tests exercise the
LOGIC each dispatch arm contains, which is unchanged, so they should pass
without modification. **`RenderGraphBarrierPlannerTests.cpp` does NOT need
re-running for Site 1-specific reasons — it never exercised Site 1's own
body in the first place (see Step 2's "Tier-2 coverage gap" note); re-run it
anyway only as a generic regression sanity check, expecting zero diffs,
never as proof Site 1's conversion is correct.** Site 1's actual
compensating check is the live/manual smoke test described in Step 2/3.2.

### 3.5 — What NOT to do in this phase

- Do NOT change what any of the confirmed call sites' logic actually DOES —
  this phase changes HOW `ResourceKind` is dispatched on, never WHAT happens
  for each kind.
- Do NOT attempt item 2.10 (de-duplicating the near-identical Buffer/
  VolumeTexture branches inside `ApplyUsageBarrierIfNeeded`) as part of this
  phase, even though the source document notes it "mostly disappears for
  free once [this] refactor exists" — that is explicitly a P3/opportunistic
  item, out of scope for `render-pass-6` per `PHASE0_MASTER_STRATEGY.md`'s
  own Non-Goals. If, while converting Site 1, the Buffer/VolumeTexture
  lambda bodies end up looking almost identical anyway (a natural
  side-effect of using the same dispatcher), that is fine to leave as-is —
  do not go out of your way to further merge them.
- Do NOT touch `src/Editor/FrameDebuggerData.cpp`'s `ReadRowLabelForKind()`/
  `WriteRowLabelForKind()` — out of scope for this campaign (see Step 2's
  own dedicated note). `DispatchByKind()` is available for a FUTURE,
  separate cleanup of that file if anyone ever wants it, but that is
  explicitly not this phase's job.
- Do NOT touch `RenderGraphBuilder.h`'s public API, `PassRecord`, or
  anything PHASE1-3 already touched.
- Do NOT change `DetectRenderPassEventContradictions()`'s public signature
  or its return value for any existing test's input, even though its
  INTERNAL implementation (Site 2) is explicitly permitted to change — see
  Step 2's "PHASE4/PHASE6 interaction" note for the exact boundary.

## Definition of Done for this phase

- `DispatchByKind()` exists in `RenderGraphTypes.h`, is a real exhaustive
  switch with no `default:` case, hard-fails (assert + throw, never a
  fabricated sentinel or a re-invoked callable) on its unreachable tail, and
  is Tier-1-tested (four-plus new cases in `RenderGraphTypesTests.cpp`,
  covering all three kinds plus a non-`void` return-type round-trip).
- Every hand-rolled `switch (usage.kind)`/`switch (a.kind)` call site
  actually found inside `RenderGraph.cpp`/`RenderGraphCompiler.cpp`/
  `RenderGraphSnapshot.cpp` by a FRESH grep performed at the START of this
  phase (not the "seven" count recorded when this document was drafted,
  which pre-dates PHASE4's own additions — see Step 2) is converted to
  route through `DispatchByKind()`, with every existing per-kind logic body
  preserved verbatim inside its own lambda.
- Site 2's conversion (`DetectRenderPassEventContradictions()`'s internal
  `sameResource` lambda) preserves that function's public signature and
  produces byte-for-byte identical results for every existing direct-call
  test AND for PHASE4's own fast-path/standalone equivalence test.
- The scratch "add a 4th dummy `ResourceKind` enumerator" verification (3.3)
  was actually performed, confirmed to fail to compile at
  `DispatchByKind()` specifically (alongside the expected, harmless,
  out-of-scope `FrameDebuggerData.cpp` failures), and then reverted before
  committing.
- Every existing test touching any converted call site's behavior
  (`RenderGraphCompilerTests.cpp`, `RenderGraphSnapshotTests.cpp`) passes
  unmodified; the new `DispatchByKind()`-specific Tier-1 tests are added and
  passing.
- Site 1's conversion is additionally confirmed via a live, HTTP-driven
  smoke test (`GET /get_swapchain`/`GET /get_game_view` before/after, and/or
  the Frame Debugger) — explicitly documented in the completion report,
  since no automated test exercises this site's own body.
- A fast, targeted incremental compile check passes for the whole engine
  target AND the whole test target.
- `PHASE6_COMPLETION_REPORT.md` is written, explicitly confirming: the
  exhaustiveness-verification step (3.3) was performed and reverted; the
  ACTUAL final count of converted call sites (with a short note if it
  differed from seven, and why); Site 2's equivalence-preservation check
  result; and Site 1's live smoke-test result.
- Changes are committed via `git_add`/`git_commit`.

## Dedicated Double-Check Instructions (for the `delegate_task` this phase
spawns immediately after landing)

1. Independently re-run the "add a 4th dummy `ResourceKind` enumerator, does
   it fail to compile at `DispatchByKind()`" verification (3.3) — do not
   trust the completion report's claim that this was done; actually redo it
   once, confirm `DispatchByKind()` is among the failures (alongside the
   expected `FrameDebuggerData.cpp` ones), then revert.
2. Independently re-grep `RenderGraph.cpp`/`RenderGraphCompiler.cpp`/
   `RenderGraphSnapshot.cpp` for any surviving hand-rolled
   `switch (usage.kind)`/`switch (a.kind)` the completion report's own count
   might have missed — especially PHASE4's `firstXWriter` prescan and its
   inline fast-path consumer, the two sites most likely to be overlooked
   since they postdate this document's original draft.
3. Diff each converted call site against its PRE-PHASE6 version and confirm
   the only change is "switch → `DispatchByKind()` call with the same
   bodies moved into lambdas" — zero logic drift.
4. Confirm Site 2's conversion left `DetectRenderPassEventContradictions()`'s
   public signature untouched, and that its existing direct-call tests AND
   PHASE4's fast-path/standalone equivalence test all still pass.
5. Confirm `RenderGraphTypes.h`'s existing "no `default:` case, ever"
   comment convention is preserved/extended, not silently weakened,
   anywhere this phase touched, and that `DispatchByKind()`'s unreachable
   tail hard-fails rather than re-invoking a real per-kind callable.
6. Re-run `RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp`/
   `RenderGraphTypesTests.cpp` and confirm all pass, including the new
   `DispatchByKind()` tests.
7. Confirm Site 1's live/manual smoke test was actually performed (not just
   claimed) and looked correct.
8. Overwrite this file (`PHASE6_RESOURCEKIND_DISPATCH_TABLE.md`) in place
   with any correction found — never create a new numbered file.
