# RENDERGRAPH_PHASE3_COMPLETION_REPORT.md

Session report for **Phase 3 — The Smart Planner**, the third implementation
chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md` — nothing beyond that
document's own "Step 3: The Plan" was implemented, per its own "Step 4:
What We Will NOT Do".

## What shipped

Two new, additively-compiled files plus one new test file — nothing else in
the engine was touched, and nothing outside `src/Renderer/RenderGraph/`
includes any of it yet:

- **`src/Renderer/RenderGraph/RenderGraphCompiler.h`/`.cpp`** — the pure
  graph-algorithm compiler that turns Phase 2's inert `CompiledGraphInput`
  into a genuinely compiled `CompiledGraph`:
  - **`ResourceLifetime`** — `firstUsePassIndex`/`lastUsePassIndex`, both
    expressed as indices **into `CompiledGraph::executionOrder`** (never a
    raw declaration index) — `-1` means "never used" (fully culled or
    never referenced). `lastUsePassIndex` covers both reads AND writes of
    a resource, so a resource whose last touch is a write with no
    subsequent read is still correctly kept alive through that write's own
    pass.
  - **`CompiledGraph`** — `executionOrder` (a topologically sorted,
    already-culled `std::vector<PassHandle>`, each `PassHandle::index`
    referring back to the pass's original declaration index into
    `CompiledGraphInput::passes`, generation always `1`), plus
    `textureLifetimes`/`bufferLifetimes` parallel to
    `CompiledGraphInput::textureDescs`/`bufferDescs`.
  - **`Compile(CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs)`**
    — the four-step algorithm from the strategy document's own Step 3.2,
    implemented in order:
    1. **Dependency graph construction** — a plain `std::vector<std::vector<bool>>`
       adjacency matrix (never a hash-keyed container, matching this
       engine's "no hashing on the hot path" philosophy — see AGENTS.md).
       For every pass's declared reads, an edge is added from "the most
       recently declared pass (so far) that wrote this resource" (a RAW
       edge); for every pass's declared writes, an edge is added from the
       previous writer of that same resource if one exists (a WAW edge,
       preserving write-after-write declaration order — e.g. two passes
       both writing the same imported swapchain image in sequence), and
       that pass becomes the new "last writer" for anything declared
       after it. A pass reading and writing the SAME resource (e.g. a
       depth attachment) never gains a self-edge (explicitly guarded).
    2. **Backward reachability from `finalOutputs`** — every pass that
       writes a `finalOutputs` texture is a root; walking backwards along
       the dependency graph from every root marks every pass that
       contributes (directly or transitively) to a final output as
       "kept". Every pass not reached is dead code: `PassRecord::isCulled`
       is written `true` for it (and explicitly `false` for every kept
       pass, so re-`Compile()`-ing an already-compiled input against a
       different `finalOutputs` set is always correct, never leaves a
       stale `true` behind) — this is exactly where Phase 1's
       `PassRecord::isCulled` field, unused since it was added, is first
       ever written.
    3. **Deterministic topological sort (Kahn's algorithm)**, restricted
       to the kept subgraph, with ties in the zero-in-degree "ready" set
       broken by **lowest original declaration index** — implemented with
       a `std::set<std::int32_t>` (ordered, no hashing) rather than a
       priority queue keyed any other way, giving the exact "if the
       caller's declared order already happens to be valid, `Compile()`'s
       output matches it byte-for-byte" determinism the strategy document
       requires (Step 3.3). A defensive cycle check (`order.size() !=
       keptCount` after the sort) throws `std::runtime_error` — see "A
       documented finding" below for why this specific throw path,
       though correctly implemented exactly as asked for, is structurally
       unreachable given today's edge-construction rule.
    4. **Resource lifetimes** — falls out of `executionOrder` in one final
       scan: for each kept resource, the first/last `executionOrder`
       *position* (not declaration index) at which any surviving pass
       reads or writes it.
  - `input` is taken by **non-const reference**, not the `const&` the
    strategy document's own Step 3.1 code sketch shows — see "A deliberate
    deviation from the doc's own signature sketch" below for why.
- **`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`** — 11 new
  Tier-1 tests, built through a real `RenderGraphBuilder` (readable, and
  proves the compiler works against exactly what Phase 2 actually hands
  off — the strategy document explicitly allows either that or
  hand-fabricated `CompiledGraphInput` values), covering every scenario
  from the strategy document's own Step 3.4 that is actually constructible
  (see the two findings below for the one that isn't):
  - Empty graph compiles to an empty result.
  - Linear chain: writer ordered before reader, nothing culled.
  - Dead branch: an unreachable pass is culled, excluded from
    `executionOrder`, and its resource's lifetime stays `{-1, -1}`.
  - Diamond dependency: A precedes both B and C; both precede D; B/C's own
    relative order matches their declaration order (the determinism
    requirement, directly exercised).
  - Multiple writers to the same (imported) resource preserve
    write-after-write declaration order.
  - Lifetime bounds: T0 (written by A, read by both B and C) spans from
    A's position through the LATER of B's/C's positions, not the earlier
    one; a resource touched only by the LAST pass in `executionOrder` gets
    `lastUsePassIndex` equal to that pass's own position, never
    one-past-the-end (the exact off-by-one the strategy document calls
    out).
  - Determinism: two independently-constructed, structurally-identical
    graphs compile to byte-identical `executionOrder`s; compiling the same
    input object twice is idempotent (same result, `isCulled` stays
    correct).
  - Self-loop: a pass that both reads and writes the same resource (a
    depth attachment shape) compiles without crashing, without a spurious
    self-cycle, and with a correct single-position lifetime.
  - A fully disconnected pass (no reads or writes at all) is culled.
  - A `finalOutputs` handle nothing ever writes degrades gracefully to "no
    root, everything culled" rather than crashing or throwing.

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/RenderGraph/RenderGraphCompiler.h`/`.cpp` to `gte_core`'s
  source list, right after the existing `RenderGraphBuilder.h`/`.cpp`
  entry.
- `tests/CMakeLists.txt`: added
  `Renderer/RenderGraph/RenderGraphCompilerTests.cpp` to `GTE_TEST_SOURCES`
  (right after `RenderGraphBuilderTests.cpp`), plus a matching entry in
  the file's own Tier-1 taxonomy comment block.

## Verification performed

- Reconfigured with CMake (reusing the existing `build/` Ninja
  configuration) — no network access needed, everything was already
  fetched.
- Built `GreatTamanaEngineTests` from the existing incremental build —
  compiled with zero warnings/errors introduced by the new/changed files.
- Ran the **new** Render Graph tests in isolation
  (`--gtest_filter=*RenderGraph*`) — all **65** pass (the 54 pre-existing
  Phase 1/2 tests, unchanged, plus the 11 new Phase 3 tests described
  above).
- Ran the **entire** test suite — **586 tests total**, **585 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test noted in every prior
  phase's report, unrelated to this change). **Zero regressions.**
- Built the real `GreatTamanaEngine.exe` target too — succeeded cleanly,
  confirming the new files don't break the shipping executable's build.

## Two findings worth flagging for whoever reads this next

### 1. A deliberate deviation from the strategy document's own `Compile()` signature sketch

`RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md`'s own Step 3.1 sketches
`CompiledGraph Compile(const CompiledGraphInput& input, ...)` — a **const**
reference. But the same document's Step 3.2.2 says culling "is precisely
where [`PassRecord::isCulled`] gets written", and Phase 1's own
`RenderGraphTypes.h` header comment on `PassRecord::isCulled` says the same
("Written by Phase 3's compiler... read by Phase 6's executor") — both of
which require actually *mutating* the pass records, which a `const&`
signature makes impossible. Phase 2's own completion report handoff notes
independently confirm the intent: "`PassRecord::isCulled` already exists
(from Phase 1) and defaults to `false` — Phase 3 is the first code that
will ever write `true` to it."

Given this contradiction between the v1 doc's own code sketch and the
documented intent everywhere else in the campaign, this implementation
took `input` by **non-const reference** instead, so `Compile()` can and
does write `isCulled` on every pass (`true` for culled, explicitly `false`
for kept, so a second `Compile()` call against a different `finalOutputs`
set is always correct). This matches every other phase document's stated
intent at the cost of diverging from this one phase document's own literal
signature sketch. If a future phase (5/6/7) was written assuming the
`const&` signature literally, that assumption needs revisiting against
this note.

### 2. The strategy document's own "cycle detection" test scenario is unconstructible

Step 3.4 asks for a test: "pass A reads what B writes AND writes what B
reads -> `Compile()` throws". This is **not implementable** against the
algorithm the same document's own Step 3.2.1 describes, and this is a
structural fact, not an implementation gap:

Every edge this compiler ever adds goes from "the most recently **declared
so far**" writer to the current pass — by construction, this means every
edge's source index is *strictly less than* its destination index (a pass
can only depend on a writer already declared before it; there is no rule
anywhere that lets an earlier-declared pass gain an edge from a
later-declared one). A graph where every edge strictly increases in index
is, by a basic graph-theory fact, *always* a DAG — its declaration order is
already one valid topological order. No `CompiledGraphInput` any
`RenderGraphBuilder` caller can actually produce (regardless of what
reads/writes/resources are declared, in what order, or how many
intermediate passes are involved) can therefore ever contain a genuine
cycle. This was verified by direct construction attempts (see the
reasoning kept inline in `RenderGraphCompiler.cpp`'s own header comment)
before concluding it was unconstructible, not assumed.

The defensive cycle-check code itself **is** implemented exactly as the
strategy document asks (Kahn's algorithm naturally detects a cycle via
"some kept node never reaches zero in-degree", checked via `order.size()
!= keptCount`, throwing `std::runtime_error` with a descriptive message) —
this is correct, harmless, forward-looking code, kept in place in case a
future change to the edge-construction rule (e.g. a Step-9-style
write-after-read/WAR hazard edge) ever makes a real cycle possible. It is
simply undertested today, the same way a `default:` branch of a switch
already proven exhaustive by the compiler would have no reachable test
either. `RenderGraphCompilerTests.cpp`'s own "Cycle detection" section
documents this same reasoning inline, rather than shipping a test that
either can't compile-fail-to-cycle or would be testing something other
than what Step 3.4 actually asked for.

## Acceptance criteria check (against the strategy document's own Step 3.4)

- ✅ Linear chain — writer before reader, nothing culled.
- ✅ Dead branch culled — excluded from `executionOrder`, resource
  lifetime `{-1, -1}`.
- ✅ Diamond dependency — correct precedence, deterministic sibling order.
- ⚠️ Cycle detection — implemented exactly as specified (Kahn's algorithm,
  throws `std::runtime_error`), but see Finding 2 above for why the
  specific test scenario described cannot actually be constructed and is
  therefore not present as a literal test.
- ✅ Multiple writers to the same imported resource preserve
  write-after-write declaration order.
- ✅ Lifetime bounds — diamond's shared resource spans through the later
  reader; last-pass-read resource has `lastUsePassIndex` equal to that
  pass's own position, not one-past-the-end.
- ✅ Determinism — two `Compile()` calls on an identical graph shape
  produce byte-identical `executionOrder`s.
- ✅ Every edge case called out in Step 5 ("self-loop, a pass that both
  reads AND writes the same resource, an entirely disconnected pass with
  no reads or writes at all, an empty graph, a `finalOutputs` handle that
  was never produced by anything") has its own named test.

## What was deliberately NOT done (per the strategy document's own Step 4)

- No pass REORDERING for performance beyond the deterministic topological
  sort itself — no cache-locality/same-target-grouping heuristics.
- No partial/incremental recompilation — `Compile()` always runs on the
  whole, freshly-declared graph from scratch, exactly matching how
  `RenderGraphBuilder` itself is a one-frame-lifetime object.
- No GPU/Vulkan/`Renderer` touch of any kind — `Compile()` operates purely
  on the plain data Phase 1/2 produced; no `VkDevice`, no
  `Renderer::ColorFormat()`/`DepthFormat()` read anywhere.
- No resource aliasing — `ResourceLifetime` is computed (forward-compatible
  with Phase 9's future aliasing work) but not consumed for any aliasing
  decision here; Phase 3 only consumes it for the correctness checks in
  its own test suite.

## Handoff notes for whoever picks up Phase 4

- Phase 4 (`RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md`) is the
  next unit of work in this campaign — turning virtual resource handles
  into real, pooled/reused `RenderTexture`/`Buffer` objects, most likely
  consuming `CompiledGraph::textureLifetimes`/`bufferLifetimes` to decide
  when a pooled resource can be reused vs. must stay allocated.
- `CompiledGraph::executionOrder`'s `PassHandle::index` is the original
  declaration index into `CompiledGraphInput::passes` — Phase 6's executor
  resolves a `PassHandle` back to its real `PassRecord` (and its
  `execute` callback) via `input.passes[handle.index]`, never by
  re-deriving order itself.
- `PassRecord::isCulled` is now real, written data as of this phase — any
  future code that iterates `CompiledGraphInput::passes` directly (rather
  than going through `CompiledGraph::executionOrder`, which already
  excludes culled passes) must check `isCulled` itself if it needs to
  skip them.
- See Finding 1 above before assuming `Compile()`'s signature is `const&`
  in any later phase document that might reference it — it is not, and
  the reasons why are load-bearing (isCulled must be written somewhere).
- See Finding 2 above before spending time trying to manufacture a
  genuine cycle test in a later phase's test suite — it is not possible
  against this compiler's current edge-construction rule, only against a
  hypothetical future one.
