# PHASE4 — `RenderGraphCompiler::Compile()` Adjacency-List Rewrite (item 2.3)

⚠️ **This phase gets its own dedicated `delegate_task` double-check pass
immediately after it lands, BEFORE the whole-campaign second-iteration
double-check** — see `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 5.
The dedicated double-check must overwrite THIS file in place if it finds
something worth fixing — never create a new numbered file.

**Recursive `ask_questions` rule**: whoever executes this phase (and anything
it further delegates via `delegate_task`) must use the `ask_questions` tool
whenever it hits a genuine ambiguity or a design choice this document doesn't
already pin down, and must repeat this exact same instruction to anything it
delegates further down the chain.

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first). This phase is independent of
PHASE1-3 (it touches `RenderGraphCompiler.cpp`/`.h` only, not
`RenderGraph.h`/`.cpp`) and can, in principle, be implemented in isolation —
but still execute it fourth, in order, per the master strategy's own
sequencing, so `PHASEn_COMPLETION_REPORT.md` history stays linear and
easy to follow.

## Step 1: The Goal (Where are we going?)

Replace `RenderGraphCompiler::Compile()`'s `O(P^2)` adjacency-matrix
(`std::vector<std::vector<bool>> edgeExists`) with adjacency LISTS, driving
both the backward-reachability cull and Kahn's topological sort in
`O(P + E)` instead of `O(P^2)`. Make `Compile()`'s own internal handling of
`RenderPassEvent` contradiction detection reuse the SAME last-writer
bookkeeping `Compile()` already builds (`lastTextureWriter`/
`lastBufferWriter`/`lastVolumeTextureWriter`) instead of calling the
standalone `DetectRenderPassEventContradictions()` function's own independent
`O(P^2*R)` backward-then-forward linear scan on every `Compile()` call.

**The single hard acceptance gate for this entire phase**: given the exact
same `CompiledGraphInput`/`finalOutputs` as today, the rewritten `Compile()`
must produce **byte-identical `CompiledGraph::executionOrder`** (same
`PassHandle` values, same order) for every existing fixture in
`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` — this is a
documented, tested, non-negotiable contract (see `RenderGraphCompiler.h`'s
own doc comment: "Given the SAME `input`... Compile() always produces the
exact same `executionOrder`"), not a nice-to-have. Zero change to
`Compile()`'s public signature (`CompiledGraph Compile(CompiledGraphInput&
input, std::span<const TextureHandle> finalOutputs)`), and zero change to
`DetectRenderPassEventContradictions()`'s own public signature/behavior (it
keeps being independently callable and independently tested exactly as
today — only what `Compile()` itself calls internally changes).

## Step 2: The Situation (Where are we now?)

Confirmed directly against the current `RenderGraphCompiler.cpp` (every line
number below was re-verified against the real file, not assumed from an
earlier read):

`RenderGraphCompiler.cpp`'s `Compile()` (lines 158-531) does, today:

1. **Effective order** (lines 185-201): stable-sorts `[0, passCount)` by
   `(RenderPassEvent, original declaration index)` — this part is ALREADY
   `O(P log P)` and is completely UNCHANGED by this phase; it is the input
   the rest of the algorithm walks.
2. **`DetectRenderPassEventContradictions()`** (called at line 213, defined
   at lines 49-134) — a SEPARATE, fully self-contained function (deliberately
   independent of `Compile()`'s own bookkeeping, per its own doc comment,
   so it stays safely callable from a test with an arbitrary hand-built
   `CompiledGraphInput` and its own hand-built `processingOrder` — confirmed
   against `RenderGraphCompilerTests.cpp`: every existing test builds `input`
   through a real `RenderGraphBuilder`, never by hand-poking
   `CompiledGraphInput` fields directly, but several tests DO call
   `DetectRenderPassEventContradictions()` directly with a small hand-built
   `processingOrder` array, e.g. `const std::int32_t identity[] = { 0, 1 };`
   — that direct-call path must keep working exactly as today). For every
   read of every pass, it does an independent backward linear scan (worst
   case `O(P)`) to find the nearest prior writer in `processingOrder`, and —
   if none is found — an independent forward linear scan (worst case another
   `O(P)`) to check for a LATER writer. Overall: `O(P^2 * R)` where `R` is
   reads-per-pass. **This call site sits BEFORE `edgeExists`/`lastXWriter`
   even exist** (the assert/stderr block spans lines 211-232, entirely
   before `edgeExists` is declared at line 243) — this positioning matters
   for 3.4 below.
3. **Step 1, edge construction** (lines 234-332): builds `edgeExists`, a
   full `passCount x passCount` `std::vector<std::vector<bool>>` (line 243),
   walking passes in effective order and, for each read/write, doing an
   `O(1)` array lookup into `lastTextureWriter`/`lastBufferWriter`/
   `lastVolumeTextureWriter` (already `O(1)` per usage — this part is
   already efficient) — but `addEdge()` (lines 246-256) just flips a boolean
   in the `O(P^2)`-sized matrix. This part is already effectively `O(P + E)`
   for CONSTRUCTING the edges (each usage is visited once) — the `O(P^2)`
   cost is purely in the MATRIX'S OWN STORAGE/ITERATION shape, not in how
   edges are discovered. The reads-loop (lines 284-304) calls `addEdge(writer,
   i)` exactly once per READ USAGE (not once per pass) — this is the exact
   detail Step 3.1 below depends on.
4. **Step 2, backward reachability** (lines 364-402): marks every pass
   reachable backward from a root. The `while (!stack.empty())` loop (lines
   392-402) does, for each popped `node`, a full `for (predecessor = 0;
   predecessor < passCount; ++predecessor)` scan checking
   `edgeExists[predecessor][node]` — this is the real `O(P^2)` cost (a full
   column scan of the matrix for every node ever pushed). The root-marking
   scan immediately above it (lines 367-390) is a separate, already-linear
   `O(P * writes-per-pass)` loop, unaffected by this phase — it is not part
   of the `O(P^2)` problem this phase targets and must not be touched.
5. **Step 3, Kahn's algorithm** (lines 408-459): computes `inDegree[to]` via
   a full nested `for (to) for (from)` double loop over the WHOLE matrix
   (lines 418-429, `O(P^2)`), then, in the main Kahn loop, for each popped
   `node`, a full `for (successor = 0; successor < passCount; ++successor)`
   scan checking `edgeExists[node][successor]` (lines 451-458, another
   `O(P^2)` total across the whole loop).
6. **Step 4, resource lifetimes** (lines 483-528): already `O(order.size() *
   average usages per pass)` — this part is already efficient and is
   UNCHANGED by this phase.

Total complexity today: matrix storage `O(P^2)`, backward reachability
`O(P^2)`, Kahn's in-degree + main loop `O(P^2)` each, contradiction detector
`O(P^2 * R)`.

Also confirmed: the "Pass counts in this engine are single digits today...
so an `O(passCount^2)` adjacency matrix is the right, simplest tool here"
comment sitting directly above `edgeExists`'s declaration (part of the "Step
1" comment block just before line 243) is now STALE even before this phase
touches anything — `PHASE0_MASTER_STRATEGY.md`'s own Step 2 evidence
explicitly corrects this premise ("real pass counts per frame are very
plausibly already in the 20-40 range, not 'single digits'"). This comment
must be rewritten as part of this phase (see Step 3.6 below) — leaving it in
place next to code that no longer uses a matrix, still claiming the old
premise that this very phase exists to correct, would be actively
misleading to the next reader.

## Step 3: The Plan (How do we get there?)

### 3.1 — Replace `edgeExists` with adjacency lists (successors AND
predecessors, both needed)

The existing algorithm needs to answer TWO different adjacency questions:
"who does this pass depend on" (predecessors — used nowhere directly today,
since in-degree is derived by counting, but see 3.3) and "who depends on
this pass" (successors — used by BOTH the backward-reachability walk, which
actually needs PREDECESSORS of a kept node, and Kahn's main loop, which
needs SUCCESSORS of a popped node). Build BOTH lists once, during the exact
same edge-construction loop that already exists (lines 269-332) — this loop
already visits each usage exactly once, so this change costs nothing extra
algorithmically, it just changes WHERE `addEdge()` writes to:

```cpp
// render-pass-6 campaign, PHASE4 (item 2.3) - REPLACES the O(P^2) adjacency
// MATRIX (std::vector<std::vector<bool>>) with adjacency LISTS - successors[from]
// is every pass that must run strictly after `from`; predecessors[to] is every
// pass that must run strictly before `to`. Both are built during the SAME
// single O(P+E) edge-construction walk below (Step 1) that already existed -
// this changes WHERE addEdge() writes an edge, not HOW MANY TIMES it is
// called. A plain std::vector<std::vector<std::int32_t>> (never a hash-keyed
// container) - matches this engine's own "no hashing on the hot path"
// convention (see AGENTS.md) and keeps push_back()'s own ordering
// (insertion order) deterministic, which matters below (see Step 3.3's own
// note on Kahn's tie-break correctness).
std::vector<std::vector<std::int32_t>> successors(static_cast<std::size_t>(passCount));
std::vector<std::vector<std::int32_t>> predecessors(static_cast<std::size_t>(passCount));

auto addEdge = [&successors, &predecessors](std::int32_t from, std::int32_t to) {
    if (from < 0 || from == to) {
        return; // Same self-loop/no-prior-writer exclusion as today - see original addEdge() comment.
    }
    successors[static_cast<std::size_t>(from)].push_back(to);
    predecessors[static_cast<std::size_t>(to)].push_back(from);
};
```

**Duplicate-edge nuance — confirmed real, but NOT a correctness landmine
given the exact in-degree computation this phase uses (verify this
reasoning yourself before trusting it, per this phase's own high-scrutiny
flag)**: the original `edgeExists[from][to] = true` is idempotent — calling
`addEdge()` twice for the same `(from, to)` pair leaves the matrix unchanged.
A plain `push_back()`-based adjacency list is NOT idempotent by default — and
the same `(from, to)` pair genuinely CAN be pushed more than once: the
reads-loop (lines 284-304, confirmed above) calls `addEdge(writer, i)` once
per READ USAGE, not once per pass, so a pass with 2 reads both resolving to
the same earlier writer pass calls `addEdge(writer, i)` twice with identical
arguments (the WAW writes-loop, lines 306-331, has the exact same
per-usage shape and the exact same duplicate risk for a pass with 2+ writes
whose prior writer is the same earlier pass).

**Trace through why this is nonetheless harmless, given Step 3.3's chosen
in-degree design (`inDegree[to]` computed as `predecessors[to].size()`,
filtered by `kept`)**: if `(from, to)` is pushed twice, both
`predecessors[to]` and `successors[from]` gain the duplicate in lockstep (the
lambda always pushes to both in the same call) — so `inDegree[to]` is
computed as `2` for that pair instead of `1`, but when `from` is popped in
Kahn's main loop, its `successors[from]` list ALSO lists `to` twice, so the
decrement happens twice, back-to-back, within that SAME single processing of
`from` — `inDegree[to]` still reaches exactly `0` at the same logical moment
it would have with a de-duplicated single edge, and the `readyByEffectivePosition`
`std::set` silently absorbs a duplicate insertion attempt as a no-op. The
same argument applies to the backward-reachability walk (3.2): a duplicated
predecessor entry is marked `kept` once (the `!kept[predecessor]` guard
skips the second occurrence) and only pushed to the stack once. **Conclusion:
given this phase's own Step 3.3 in-degree design, duplicate edges are
self-consistently harmless for both determinism and correctness — this is
NOT a required fix to avoid a live bug.** De-duplicating anyway (below) is
still the RECOMMENDED choice, for two independent, weaker reasons: (1) it
keeps the adjacency lists' size bounded by true edge count rather than usage
count, matching the source document's own "O(P+E)" framing more tightly in
pathological high-fan-in cases; (2) it makes the produced lists a 1:1 mirror
of the original matrix's own edge set, which is exactly what makes this
phase's own dedicated double-check (see below) able to "manually trace
through the in-degree/successor-list bookkeeping" with the least possible
cognitive overhead. If de-duplicating, the cleanest approach:
```cpp
auto addEdge = [&successors, &predecessors](std::int32_t from, std::int32_t to) {
    if (from < 0 || from == to) {
        return;
    }
    auto& succ = successors[static_cast<std::size_t>(from)];
    if (std::find(succ.begin(), succ.end(), to) != succ.end()) {
        return; // Already recorded - matches the original matrix's own natural idempotence.
    }
    succ.push_back(to);
    predecessors[static_cast<std::size_t>(to)].push_back(from);
};
```
`std::find()` here is `O(current out-degree of from)`, not `O(P)` — bounded
by however many OTHER passes directly depend on `from` — `<algorithm>` is
already included at the top of this file, no new include needed. Whichever
way you choose (de-duplicate, or leave duplicates and rely on the
self-consistency argument above), **write down which one was chosen and why
in `PHASE4_COMPLETION_REPORT.md`** — this is exactly the kind of easy-to-get-
subtly-wrong reasoning a future reader should not have to re-derive from
scratch.

### 3.2 — Backward reachability: predecessors instead of a matrix column scan

Replace Step 2's `for (predecessor = 0; predecessor < passCount;
++predecessor) if (edgeExists[predecessor][node] && !kept[predecessor])`
(lines 395-401) with:
```cpp
while (!stack.empty()) {
    const std::int32_t node = stack.back();
    stack.pop_back();
    for (const std::int32_t predecessor : predecessors[static_cast<std::size_t>(node)]) {
        if (!kept[static_cast<std::size_t>(predecessor)]) {
            kept[static_cast<std::size_t>(predecessor)] = true;
            stack.push_back(predecessor);
        }
    }
}
```
This visits each edge at most once overall (standard DFS/BFS reachability),
`O(P + E)` total instead of `O(P^2)`. The root-marking scan immediately
above this loop (lines 367-390, `ContainsTextureHandle`/
`ContainsVolumeTextureHandle` against `finalOutputs`/
`finalVolumeTextureOutputs`) is untouched — it was never part of the
`O(P^2)` problem.

### 3.3 — Kahn's algorithm: successors instead of a matrix row scan, in-degree
via `predecessors.size()`

Replace the in-degree double loop (lines 418-429) with a direct read of
`predecessors[to].size()` restricted to KEPT predecessors:
```cpp
std::vector<std::int32_t> inDegree(static_cast<std::size_t>(passCount), 0);
for (std::int32_t to = 0; to < passCount; ++to) {
    if (!kept[static_cast<std::size_t>(to)]) {
        continue;
    }
    for (const std::int32_t from : predecessors[static_cast<std::size_t>(to)]) {
        if (kept[static_cast<std::size_t>(from)]) {
            ++inDegree[static_cast<std::size_t>(to)];
        }
    }
}
```
This is `O(P + E)` (each predecessor list entry visited once total across
the outer loop). Replace the main Kahn loop's successor scan (lines 451-458)
with:
```cpp
for (const std::int32_t successor : successors[static_cast<std::size_t>(node)]) {
    if (kept[static_cast<std::size_t>(successor)]) {
        if (--inDegree[static_cast<std::size_t>(successor)] == 0) {
            readyByEffectivePosition.insert(effectivePosition[static_cast<std::size_t>(successor)]);
        }
    }
}
```
**Determinism preservation — critical**: the existing tie-break
(`std::set<std::int32_t> readyByEffectivePosition`, ordered ascending by
EFFECTIVE POSITION, never by raw pass index or by adjacency-list insertion
order) is COMPLETELY UNCHANGED by this rewrite — it does not depend on
`edgeExists`'s storage shape at all, only on which nodes become ready and
when, which (per 3.1's own duplicate-edge argument) is identical either way.
This is exactly why byte-identical `executionOrder` is achievable: the
rewrite changes iteration MECHANICS (matrix scan → list walk), never the
LOGICAL edge set or the tie-break rule that makes output deterministic.

### 3.4 — Fold contradiction detection into `Compile()`'s own edge-construction
walk, reusing its last-writer bookkeeping directly

This is the trickier half. `DetectRenderPassEventContradictions()` itself —
the standalone, public, independently-tested function — **must NOT be
modified at all**: its existing signature, its existing self-contained
`O(P^2 * R)` implementation, and every existing test that calls it directly
(with a hand-built `processingOrder`, completely independent of a real
`Compile()` call) all stay byte-for-byte unchanged. It remains the permanent
CORRECTNESS ORACLE. What changes is **what `Compile()` itself calls
internally** — today it calls the standalone function once, up front, before
`edgeExists`/`lastXWriter` even exist (lines 211-232); after this phase, it
must instead compute an equivalent result using the SAME `lastTextureWriter`/
`lastBufferWriter`/`lastVolumeTextureWriter` state Step 1 already
incrementally maintains, and report it with the exact same stderr/assert
code already at lines 217-231 (unchanged), just fed by a differently-computed
vector.

**Why this cannot be "a separate O(P) pass placed anywhere convenient" —
the one subtlety that makes this phase's own strategy document's original
"work it out during implementation" hand-wave dangerous**: `lastTextureWriter[resource]`
(etc.) only holds the correct "nearest writer AS OF THIS READ'S POSITION IN
EFFECTIVE ORDER" value while Step 1's own forward loop is walking passes IN
effective order and consulting/updating it incrementally, read-usage by
read-usage. A pass's reads MUST be checked using `lastXWriter`'s state
*before* that same pass's own writes (later in the same iteration) update
it — this ordering already exists in Step 1's loop body (reads processed
first, writes processed second, per iteration) and must not change. Trying
to run the contradiction check as an isolated pass EITHER strictly before
Step 1 (no `lastXWriter` state exists yet — you would have to duplicate an
entire second incremental walk, defeating "reuse the same bookkeeping") OR
strictly after Step 1 completes (by then `lastXWriter` holds each resource's
FINAL/last writer overall, not "the nearest writer as of when a particular
earlier read happened" — for a resource with more than one writer, this
gives a flatly WRONG answer for any read that happened before the final
writer) is not just an efficiency compromise, it is an actual correctness
bug waiting to happen. **The contradiction check must therefore be folded
directly INTO Step 1's existing per-pass, per-read-usage loop (lines
284-304), executed inline exactly where `writer` is already computed for
edge-construction purposes, reusing that exact same local value — not
recomputed separately.**

The one piece Step 1's own forward walk genuinely cannot answer on its own
is the `OrphanReadWithLaterWriter` case: "no writer found before this read —
but is there one somewhere LATER?" A forward-only incremental walk cannot
know about a pass it hasn't reached yet. This needs exactly one small,
separate, PRE-computed table, built by a cheap preliminary O(P + W) walk
over `pass.writes` only (W = total write usages across all passes) — the
resource's FIRST writer (by original pass index, exactly like
`lastXWriter`'s own storage convention — no position/index-translation
mismatch to worry about) anywhere in effective order:

```cpp
// render-pass-6 campaign, PHASE4 (item 2.3) - a small, separate, O(P+W)
// preliminary walk (W = total write usages), run ONCE before Step 1's own
// loop - firstXWriter[resource] is the ORIGINAL pass index (matching
// lastXWriter's own storage convention) of the FIRST pass, in effective
// order, that writes this resource, or -1 if never written by anyone.
// This is the ONLY piece Step 1's incremental forward walk cannot answer by
// itself (it cannot know about a pass it hasn't reached yet) - everything
// else the contradiction check needs (the "nearest writer so far") is
// already sitting in lastTextureWriter/lastBufferWriter/lastVolumeTextureWriter
// by the time Step 1 processes each read.
std::vector<std::int32_t> firstTextureWriter(input.textureDescs.size(), -1);
std::vector<std::int32_t> firstBufferWriter(input.bufferDescs.size(), -1);
std::vector<std::int32_t> firstVolumeTextureWriter(input.volumeTextureDescs.size(), -1);
for (std::int32_t pos = 0; pos < passCount; ++pos) {
    const std::int32_t i = effectiveOrder[static_cast<std::size_t>(pos)];
    for (const ResourceUsage& usage : input.passes[static_cast<std::size_t>(i)].writes) {
        switch (usage.kind) {
        case ResourceKind::Texture:
            if (usage.texture.index < firstTextureWriter.size() && firstTextureWriter[usage.texture.index] == -1) {
                firstTextureWriter[usage.texture.index] = i;
            }
            break;
        case ResourceKind::Buffer:
            if (usage.buffer.index < firstBufferWriter.size() && firstBufferWriter[usage.buffer.index] == -1) {
                firstBufferWriter[usage.buffer.index] = i;
            }
            break;
        case ResourceKind::VolumeTexture:
            if (usage.volumeTexture.index < firstVolumeTextureWriter.size()
                && firstVolumeTextureWriter[usage.volumeTexture.index] == -1) {
                firstVolumeTextureWriter[usage.volumeTexture.index] = i;
            }
            break;
        }
    }
}
```
Why this is exactly right: if a read's `lastXWriter[...]` is `-1` at the
moment Step 1 processes it (no writer seen "so far"), then whichever pass
`firstXWriter[resource]` names — if not `-1` — is NECESSARILY positioned
strictly AFTER the reader in effective order (nothing wrote this resource
before or at the reader's position, by the `lastXWriter == -1` premise, so
the resource's overall first writer cannot be at or before the reader
either), and is in fact the SAME "nearest later writer" the standalone
function's own forward linear scan (`for otherPos = readerPos+1...`, taking
the FIRST match) would find — because nothing between the reader and
`firstXWriter[resource]`'s position can also be a writer (that would
contradict it being the FIRST one).

**Required self-exclusion guard — the one easy way to get this subtly
wrong**: a pass that reads AND writes the SAME resource itself (e.g. the
existing `SelfLoopReadAndWriteOfSameResourceDoesNotCrashOrCycle` test's
depth-attachment shape) can legitimately BE that resource's own
`firstXWriter` entry (if nothing wrote the resource before it and it writes
it itself). The standalone function's own forward scan starts at
`otherPos = readerPos + 1`, which structurally EXCLUDES the reader's own
position — so it never reports a pass as contradicting itself. The inline
fast-path check must reproduce this explicitly: **only treat
`firstXWriter[resource]` as an `OrphanReadWithLaterWriter` hit if it is both
not `-1` AND not equal to the current pass's own index `i`** — otherwise a
single self-loop pass would wrongly report a contradiction against itself,
a false positive the standalone function never produces and a regression
test (3.5 below) must specifically catch.

Putting it together, inside Step 1's existing reads-loop (right where
`writer` is computed for edge-construction, lines 284-304), add:
```cpp
if (writer == -1) {
    std::int32_t laterWriter = -1;
    switch (usage.kind) {
    case ResourceKind::Texture:
        laterWriter = usage.texture.index < firstTextureWriter.size() ? firstTextureWriter[usage.texture.index] : -1;
        break;
    case ResourceKind::Buffer:
        laterWriter = usage.buffer.index < firstBufferWriter.size() ? firstBufferWriter[usage.buffer.index] : -1;
        break;
    case ResourceKind::VolumeTexture:
        laterWriter = usage.volumeTexture.index < firstVolumeTextureWriter.size()
            ? firstVolumeTextureWriter[usage.volumeTexture.index]
            : -1;
        break;
    }
    if (laterWriter != -1 && laterWriter != i) { // self-exclusion guard - see above.
        fastContradictions.push_back(RenderPassEventContradiction{
            RenderPassEventContradictionKind::OrphanReadWithLaterWriter, i, laterWriter, usage.kind,
            /* resourceIndex: same three-way ternary already used elsewhere in this file */ 0 });
    }
} else if (input.passes[static_cast<std::size_t>(writer)].renderPassEvent
    > input.passes[static_cast<std::size_t>(i)].renderPassEvent) {
    fastContradictions.push_back(RenderPassEventContradiction{
        RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency, i, writer, usage.kind, 0 });
}
```
(fill in `resourceIndex` using the exact same `usage.kind == ResourceKind::Texture
? usage.texture.index : ...` three-way pattern the standalone function
already uses at lines 108-110/124-126 — do not invent a new pattern). Declare
`std::vector<RenderPassEventContradiction> fastContradictions;` once, before
Step 1's loop begins, and — **after Step 1's loop finishes** (still strictly
before Step 2's culling, since this remains a pure, non-mutating diagnostic
pass) — feed it through the EXACT SAME reporting code currently at lines
217-231 (the `for (const RenderPassEventContradiction& contradiction :
contradictions)` stderr loop plus the `assert(contradictions.empty() && ...)`),
just renamed to read from `fastContradictions` instead of calling
`DetectRenderPassEventContradictions()`. **Remove the old call site (lines
211-232) entirely** — leaving it in place alongside the new inline logic
would silently defeat the entire point of this sub-phase (the `O(P^2*R)`
scan would still run, every frame, unchanged).

Because this genuinely relocates the check (from "before Step 1" to
"threaded through Step 1, reported once Step 1 finishes"), also update:
`RenderGraphCompiler.h`'s own doc comment on `Compile()` ("Compile() now ALSO
runs `DetectRenderPassEventContradictions()` once, at the very top...") and
the matching comment inside `RenderGraphCompiler.cpp` — both currently say
"at the very top", which stops being literally true. Reword to something
like "once, reusing the same last-writer bookkeeping its own RAW/WAW
edge-construction walk builds, before any pass is culled" — the OBSERVABLE
behavior (stderr text, assert firing before culling/reordering ever happens)
is unchanged, only the sentence describing WHERE in the function body this
now happens needs to stay accurate.

Add a **new Tier-1 test** (see 3.5) that runs the standalone
`DetectRenderPassEventContradictions()` AND `Compile()`'s own internal
`fastContradictions` computation against the same fixtures used by the
existing `DetectRenderPassEventContradictions`-focused tests
(`NormalWriterBeforeReaderProducesNoContradictions`,
`OrphanReadWithLaterWriterIsDetected`,
`EdgeContradictingDeclaredEventOrderIsDetected`,
`SameEventTierNeverProducesAContradiction`) and asserts they report IDENTICAL
contradiction sets — this is the actual proof that the fast path is a real
optimization, not a silent behavior change. Since `fastContradictions` is
Compile()'s own internal state, this test will need either a small,
dedicated internal test hook (documented in the completion report) or must
observe the effect indirectly (e.g. a debug build with the contradiction
graph fed into `Compile()` and confirmed to still assert, mirroring
`CallingCompileWithAConsistentGraphNeverAborts`'s existing pattern in
reverse) — pick whichever is less invasive to the production header and
document the choice; this is an implementation-detail choice within the
phase's fixed goal, not a locked design decision requiring `ask_questions`
unless it forces a public signature/header change to
`RenderGraphCompiler.h` itself (in which case, `ask_questions` first).

### 3.5 — Tests (mandatory, this is the phase's whole acceptance gate)

- Run the FULL existing `RenderGraphCompilerTests.cpp` suite before making
  any change, and record every test's exact `executionOrder` output (or
  simply confirm the suite passes as today's baseline — since the tests
  already assert on exact `executionOrder` shape per the file's own
  existing assertions).
- After the rewrite, run the SAME suite again — every single existing case
  must still pass, unmodified, with byte-identical `executionOrder`
  assertions. Do NOT loosen or delete any existing assertion to make it
  pass — per `AGENTS.md`'s own rule, a newly-failing test is a real
  regression to understand, never something to work around.
- Add NEW test cases specifically targeting the adjacency-list rewrite's
  own new risk surface:
  - A pass with 2+ reads all resolving to the SAME earlier writer pass
    (the duplicate-edge scenario from 3.1) — confirm `executionOrder`
    still places the writer before the reader exactly once, and no crash/
    infinite loop occurs.
  - A pass with 2+ WRITES to different resources that both happen to share
    the SAME earlier writer pass (the WAW-side sibling of the read-side
    duplicate above — same per-usage `addEdge()` shape at lines 306-331) —
    confirm the same "no crash, correct single ordering" result.
  - A resource with a long WAW chain (3+ writers in a row) — confirm the
    adjacency-list edges correctly chain writer1→writer2→writer3→reader,
    matching the matrix version's behavior.
  - The existing "self-loop" test (a pass that reads AND writes the same
    resource) — confirm the `from == to` exclusion in `addEdge()` still
    holds with the list-based implementation.
  - A large-ish synthetic pass count (e.g. 50-100 passes in a chain/fan-out
    shape) exercising the new `O(P+E)` path measurably differently from a
    tiny 3-5 pass fixture — not a performance-timing assertion (this
    engine doesn't have a timing-based test convention), just a
    correctness stress test at a size where an `O(P^2)` vs `O(P+E)` bug
    (e.g. a missed edge, a duplicate causing wrong in-degree) would be far
    more likely to surface than at single-digit pass counts.
  - **The fast-path/standalone-function equivalence test described at the
    end of 3.4** — mandatory, not optional, since it is the only direct
    proof the internal reuse is behavior-preserving.
  - **A dedicated self-exclusion regression test**: a single pass that both
    reads and writes the same never-otherwise-written resource (the exact
    `SelfLoopReadAndWriteOfSameResourceDoesNotCrashOrCycle` shape) run
    through `Compile()` and confirmed to produce ZERO contradictions/no
    assert fire — this is the specific false-positive trap called out in
    3.4's "required self-exclusion guard" and deserves its own named test,
    not just incidental coverage.
  - **A never-written-resource equivalence case**: a read of a resource
    nothing ever writes (`firstXWriter[resource] == -1`) must produce NO
    contradiction from either the standalone function or the internal fast
    path — confirms the "no writer anywhere" case is handled identically,
    not just the "writer exists later" case.

### 3.6 — Comment & doc-comment hygiene (part of this phase's own Definition
of Done, not optional cleanup)

- Rewrite the "Step 1: build the dependency graph" comment block
  immediately above the old `edgeExists` declaration (the one asserting
  "Pass counts in this engine are single digits today... an O(passCount^2)
  adjacency matrix is the right, simplest tool here") — this premise is
  exactly what `PHASE0_MASTER_STRATEGY.md`'s own Step 2 evidence corrects
  (20-40 real passes per frame, not single digits), and it is precisely
  what this phase's own rewrite makes obsolete; leaving it verbatim next to
  adjacency-list code would directly contradict both the new code and this
  campaign's own stated motivation.
- Update `RenderGraphCompiler.h`'s `Compile()` doc comment and the matching
  comment in `RenderGraphCompiler.cpp` that currently describe
  `DetectRenderPassEventContradictions()` running "once, at the very top" —
  see 3.4's own note on why this phrase stops being literally accurate once
  the check is folded into Step 1's walk.
- Any other comment in this file that explicitly references `edgeExists`,
  "the matrix", or an O(P^2) characterization of a section this phase
  changes must be updated to describe the new adjacency-list shape instead
  — do a final grep for `edgeExists` after the rewrite to confirm zero
  stale references remain anywhere in `RenderGraphCompiler.h`/`.cpp`.

### 3.7 — What NOT to do in this phase

- Do NOT change `Compile()`'s public signature, `CompiledGraph`'s shape, or
  `ResourceLifetime`'s shape.
- Do NOT change `DetectRenderPassEventContradictions()`'s own public
  signature, its existing standalone implementation, or any of its existing
  direct-call tests — it remains the untouched correctness oracle.
- Do NOT change the effective-order computation (Step in lines 185-201) —
  unrelated to this phase's algorithmic target.
- Do NOT touch `RenderGraphBuilder.h`/`CompiledGraphInput`'s shape — that is
  PHASE5's job (this phase runs BEFORE PHASE5 per the master strategy's
  ordering, so `Compile()` still reads the OLD 9-parallel-vector shape here;
  PHASE5 will need to re-verify this phase's new adjacency-list code still
  compiles correctly against the new slot-vector shape once it lands).
- Do NOT introduce a brand-new, independent `switch (usage.kind)`/
  `switch (ResourceKind)` construct anywhere as part of this phase — the new
  `firstXWriter` prescan and the inline contradiction check both legitimately
  EXTEND the exact same existing exhaustive three-way switch pattern already
  present in Step 1's reads-loop (lines 286-302) and elsewhere in this file;
  they are not a wholly new switch outside that established convention. No
  `default:` case is introduced or needed anywhere this phase touches.

## Definition of Done for this phase

- `edgeExists` (the `O(P^2)` matrix) is gone; `successors`/`predecessors`
  adjacency lists replace it.
- `DetectRenderPassEventContradictions()`'s existing public function, its
  existing signature, and its existing tests are BYTE-FOR-BYTE UNCHANGED.
  `Compile()` no longer calls it internally at all — the old call site
  (lines 211-232) is removed and replaced by an inline, `firstXWriter`/
  `lastXWriter`-based equivalent folded into Step 1's own walk, reported via
  the same stderr/assert code once Step 1 finishes (3.4).
- The self-exclusion guard (`laterWriter != i`) is present and covered by its
  own dedicated regression test (3.5).
- The fast-path/standalone-function equivalence test (3.4/3.5) passes,
  proving the internal reuse produces identical contradiction results to the
  untouched standalone function across every existing contradiction-focused
  fixture.
- **Every existing `RenderGraphCompilerTests.cpp` test case passes with
  byte-identical `executionOrder` output** — this is verified explicitly,
  by actually running the test suite (a fast, targeted `ctest -R
  RenderGraphCompiler` style incremental run is acceptable here — matches
  `gtest_discover_tests`' own per-`TEST()` naming convention already used by
  this test binary, e.g. `RenderGraphCompilerTest.EmptyGraphCompilesToEmptyResult`
  — this one targeted test run, unlike a FULL `ctest`, is explicitly
  warranted given this phase's own correctness-critical nature; it is not
  the same as PHASE0's "only PHASE7 runs full build/ctest" full-suite rule,
  which refers to the WHOLE engine's test suite, not one targeted, fast,
  already-scoped compiler test binary).
- New tests targeting the duplicate-edge/WAW-chain/self-loop/larger-graph/
  self-exclusion/never-written-resource scenarios (3.5) are added and
  passing.
- Every stale comment identified in 3.6 is rewritten — confirmed by a final
  grep for `edgeExists` and for the phrase "at the very top" showing zero
  remaining stale hits.
- `PHASE4_COMPLETION_REPORT.md` is written, explicitly stating: the exact
  duplicate-edge handling implemented (de-duplicated vs left-as-is-and-
  reasoned-harmless, per 3.1), how the fast-path/standalone equivalence test
  was implemented (3.4's own note on the internal-hook-vs-indirect-observation
  choice), and the exact evidence (test names/counts) proving byte-identical
  `executionOrder`.
- Changes are committed via `git_add`/`git_commit`.

## Dedicated Double-Check Instructions (for the `delegate_task` this phase
spawns immediately after landing)

The dedicated double-check for this phase must specifically re-verify, by
reading the actual diff and running the targeted compiler test suite itself
(not just trusting the completion report):

1. Byte-identical `executionOrder` for every pre-existing test fixture,
   confirmed by actually re-running `RenderGraphCompilerTests.cpp`, not by
   re-reading the completion report's claim alone.
2. The duplicate-edge handling (3.1) — whichever of the two allowed
   approaches was actually implemented — is correctly reasoned about;
   construct one extra scratch scenario if the existing new tests don't
   already cover it clearly enough, and manually trace through the
   in-degree/successor-list bookkeeping to confirm it balances.
3. The old call to the standalone `DetectRenderPassEventContradictions()`
   function is genuinely REMOVED from `Compile()`'s own body (not left in
   place alongside a redundant new fast path) — confirm via `git diff` that
   `Compile()` no longer calls that function at all, while the function's
   own definition and its own direct-call tests are byte-for-byte unchanged.
4. The self-exclusion guard (`laterWriter != i`) is present, and the
   dedicated self-loop regression test (3.5) actually exercises it and
   actually fails without the guard (verify by temporarily removing the
   guard locally, confirming the test catches it, then restoring the guard
   — never leave the guard removed).
5. The fast-path/standalone equivalence test (3.4) genuinely compares both
   paths' results, not just checks that `Compile()` doesn't crash.
6. No `default:` case was introduced anywhere in this phase's new code
   touching `ResourceKind` switches, and no wholly new/independent switch
   construct was added outside the established pattern (see 3.7's own note
   — this phase legitimately EXTENDS existing switches in Step 1's loop,
   which is expected and fine; a brand new, separately-invented switch
   elsewhere would be the actual red flag worth investigating).
7. Every stale-comment fix from 3.6 actually landed (grep for `edgeExists`
   and "at the very top" in the final diff).
8. Overwrite this file (`PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`) in
   place with any correction found — never create a new numbered file.

## Double-check confirmation (dedicated post-landing review)

A dedicated second reviewer re-verified this phase independently, by reading
the actual diff/current source and by actually rebuilding and re-running the
targeted `RenderGraphCompilerTests.cpp` suite — never by trusting
`PHASE4_COMPLETION_REPORT.md`'s own claims alone. **Result: no correction
needed — every claim in the completion report checked out.** Evidence,
item by item (mirroring the 8-point checklist above):

1. **Byte-identical `executionOrder`** — confirmed by actually building
   (`cmake --build build --target GreatTamanaEngineTests`) and running
   `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraphCompiler*`
   from a fresh session: **34/34 tests pass** (all 33 pre-existing
   `RenderGraphCompilerTest` cases, byte-for-byte unmodified per `git show
   4e24435`'s own diff — the diff only ever APPENDS new test cases after the
   pre-existing ones, never touching a single pre-existing assertion — plus
   1 new `RenderGraphCompilerDeathTest` case).
2. **Duplicate-edge handling** — confirmed `addEdge()`'s `std::find()`-based
   de-duplication is present and correctly reasoned: independently traced a
   read-side duplicate (2 reads of the same resource resolving to the same
   writer) AND a synthetic combined read+write scenario (3 dedup'd reads
   feeding a downstream 2 dedup'd reads) through the in-degree/successor-list
   bookkeeping by hand — every real edge is counted/decremented exactly
   once, `inDegree` reaches 0 at the same logical moment either way, matching
   both the plan's own self-consistency argument and the actually-chosen
   de-duplicated design. The two dedicated new tests
   (`PassWithTwoReadsOfSameResourceResolvingToSameEarlierWriterOrdersCorrectlyWithNoDuplicateEdgeIssues`/
   `PassWithTwoWritesToDifferentResourcesSharingTheSameEarlierWriterOrdersCorrectlyWithNoDuplicateEdgeIssues`)
   already cover both shapes and pass.
3. **Standalone-function call site genuinely removed** — confirmed via
   `git show 4e24435` and a direct grep of
   `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`/`.h`:
   `DetectRenderPassEventContradictions` is only ever *defined* and
   *documented*, never *called*, anywhere in `Compile()`'s own body. The
   function's own definition (lines 49-134) and its five direct-call tests
   (`NoReadsOrWritesProducesNoContradictions`,
   `NormalWriterBeforeReaderProducesNoContradictions`,
   `OrphanReadWithLaterWriterIsDetected`,
   `EdgeContradictingDeclaredEventOrderIsDetected`,
   `SameEventTierNeverProducesAContradiction`) fall entirely outside the
   PHASE4 diff's touched line ranges — confirmed byte-for-byte unchanged
   directly from `git show`, not merely assumed.
4. **Self-exclusion guard** — verified LIVE, not just read: temporarily
   edited `RenderGraphCompiler.cpp`'s `if (laterWriter != -1 && laterWriter
   != i)` down to `if (laterWriter != -1)`, rebuilt, and re-ran
   `SelfExclusionGuardPreventsFalsePositiveContradictionForSelfLoopReadWriteOfNeverOtherwiseWrittenResource`
   in isolation — it crashed immediately with the expected
   `fastContradictions.empty()` assert, stderr correctly naming `"DepthPass"`
   as contradicting itself. Restored the guard, rebuilt, and re-ran the full
   34-test targeted suite — all 34 passed again. The working tree is clean
   (`git status`) after restoring, confirming no stray edit was left behind.
5. **Fast-path/standalone equivalence test** — confirmed
   `RenderGraphCompilerDeathTest.FastPathDetectsOrphanReadWithLaterWriterAndAbortsJustLikeTheStandaloneFunctionWould`
   genuinely proves equivalence (not merely "doesn't crash"): it reproduces
   the EXACT graph shape `OrphanReadWithLaterWriterIsDetected` already proves
   the standalone function flags as a contradiction, then asserts `Compile()`
   itself — driven entirely by the new inline fast path — DOES abort
   (`EXPECT_DEATH`), which is the correct/only way to observe internal,
   non-publicly-exposed `fastContradictions` state. Independently re-derived
   the `DeclaredEventOrderDisagreesWithRealDependency`-unreachability claim
   from first principles (not just re-read it): `Compile()`'s inline check
   only ever resolves `writer != -1` from `lastXWriter`, which is only ever
   set by a WAW write processed at a STRICTLY earlier `effectiveOrderPos`
   than the current read's own pass (writes are processed after reads within
   the same pass's own loop iteration, so a pass's own writes can never
   supply its own reads' `writer` value) — and since `effectiveOrder` is a
   stable sort ascending by `RenderPassEvent`, a strictly-earlier
   `effectiveOrderPos` guarantees `writer.renderPassEvent <=
   reader.renderPassEvent` (never `>`), so the
   `DeclaredEventOrderDisagreesWithRealDependency` condition
   (`writer.renderPassEvent > reader.renderPassEvent`) is mathematically
   unreachable through the inline fast path — confirms the report's claim
   exactly; no counter-example exists to construct.
6. **No `default:` case, no new switch construct** — confirmed by grepping
   `src/Renderer/RenderGraph/` for the literal string `default:`: every hit
   is inside an explanatory comment about the "no default: case, ever"
   convention itself, never an actual `case` label. (Note: by the time of
   this review, PHASE6 had already landed and converted PHASE4's own
   hand-rolled `switch (usage.kind)` blocks — the `firstXWriter` prescan and
   the inline contradiction check — into calls to the shared
   `DispatchByKind()` dispatcher; this is expected, already-anticipated
   downstream evolution per PHASE6's own strategy doc, not a PHASE4 defect,
   and does not change this item's answer.)
7. **Stale-comment fixes** — confirmed by direct grep of
   `src/Renderer/RenderGraph/`: `edgeExists` has exactly **one** hit
   (`RenderGraphCompiler.cpp`, an explanatory comment describing what the OLD
   code used to do), and `"at the very top"` has **zero** hits in
   `RenderGraphCompiler.h`/`.cpp` (its only remaining hit anywhere in the
   folder is an unrelated sentence in `RenderGraph.h` about GPU timing
   readback, exactly as the completion report describes).
8. This file is being overwritten in place with this exact section — no new
   numbered file was created.

No code changes were required as a result of this double-check; PHASE4's
implementation is confirmed correct and complete as landed.
