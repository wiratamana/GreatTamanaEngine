# RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md
### (Part 3 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign)

## Step 1: The Goal (Where are we going?)

Turn Phase 2's inert `CompiledGraphInput` (a bag of declared passes and
resource descriptions, in whatever order the caller happened to call
`AddPass()`) into a genuinely **compiled** artifact: a linear pass EXECUTION
ORDER that respects every declared read-after-write dependency, with every
pass that provably contributes nothing to a final output CULLED out
entirely, and with every resource's exact LIFETIME (the first pass index
that touches it, the last pass index that reads it) computed once, up
front. This is pure graph algorithm - topological sort plus reachability
analysis - and it is the single most "compiler-shaped" phase of the whole
campaign, hence it gets built and tested completely independent of Vulkan,
`Renderer`, or anything GPU-shaped at all.

## Step 2: The Situation / The Problem (Where are we now?)

`Application::Run()` hand-orders exactly three passes today by literally
writing them in sequence in C++ source: Game view, then Scene view, then
Present. This works only because there are exactly three passes, their
relative order never needs to change, and none of them ever reads another
one's output (Game/Scene both read only from `RenderSystem::Draw()`'s own
queued mesh submissions; Present only reads Dear ImGui's own draw data,
which itself SAMPLES the Game/Scene `RenderTexture`s via `ImGui::Image()` -
but that sampling happens on the CPU/ImGui-widget side, not as a declared
GPU resource dependency the engine's rendering code is aware of).

The moment a real cross-pass GPU dependency exists (a lighting pass that
must run after a G-buffer pass, a bloom pass that must run after a
tonemapping pass reads the previous frame's - or same frame's - HDR
target), hand-ordering breaks down: it requires every future pass author to
correctly reason about the ENTIRE existing pass list's ordering by hand,
forever, with no automated check that catches "I added a pass that reads a
resource nobody wrote yet" or "I introduced a cycle." A compiler that
derives order FROM declared dependencies removes this whole class of bug
structurally - the same reasoning this codebase already applies to, e.g.,
`RenderSystem::ResolveActiveCameraViewProjection()` deriving the active
camera from data rather than a hand-maintained "current camera" global.

## Step 3: The Plan (How will we get there?)

### 3.1 - New file: `src/Renderer/RenderGraph/RenderGraphCompiler.h/.cpp`

```cpp
struct CompiledGraph {
    std::vector<PassHandle> executionOrder;      // topologically sorted, culled passes only
    std::vector<ResourceLifetime> textureLifetimes; // parallel to CompiledGraphInput's texture table
    std::vector<ResourceLifetime> bufferLifetimes;
};
struct ResourceLifetime {
    std::int32_t firstUsePassIndex = -1; // index into executionOrder; -1 == "never used" (fully culled resource)
    std::int32_t lastUsePassIndex = -1;
};

CompiledGraph Compile(const CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs);
```

`finalOutputs` is the compiler's REQUIRED root set - the handles the caller
actually needs at the end of this frame (e.g. the swapchain image `Present`
writes, and nothing else) - because a render graph has no other notion of
"externally observable effect": a pass with no path to any `finalOutputs`
entry is dead code, full stop, and gets culled.

### 3.2 - Algorithm, in three passes over the data (each independently unit
tested):

1. **Build a dependency graph.** For every pass P that WRITES resource R,
   and every later-declared pass Q that READS resource R, add an edge
   P -> Q ("P must execute before Q"). Because `AddPass()` calls happen in
   a definite call order already, "later-declared" is well-defined without
   needing a resource's writer to be looked up by anything fancier than "the
   most recent pass, among those declared so far, that wrote it" - a
   resource written twice (e.g. two passes both clearing/writing the same
   imported swapchain image in sequence) is legal and simply means the
   second writer depends on the first (write-after-write ordering,
   preserved by construction, exactly like real hardware/API ordering
   requires).
2. **Reachability from `finalOutputs`, backwards.** Starting from every
   pass that writes something in `finalOutputs`, walk backwards along the
   dependency edges built above, marking every reached pass as "kept."
   Every pass never reached is `isCulled = true` (see Phase 1's
   `PassRecord::isCulled` field - this is precisely where it gets written)
   and is excluded from `executionOrder` and from every lifetime
   computation entirely - a culled pass's declared reads/writes must never
   extend any resource's lifetime, or a culled shadow-pass-that-writes-a-
   texture-nobody-reads would still force that texture to be allocated in
   Phase 4, defeating the entire point of culling.
3. **Topological sort of the kept passes**, respecting every edge computed
   in step 1, restricted to kept passes only - Kahn's algorithm (a plain
   queue of zero-in-degree nodes), chosen over DFS-based sort specifically
   because Kahn's algorithm naturally DETECTS a cycle (any node never
   dequeued because its in-degree never reaches zero) without a separate
   visited/gray/black three-color pass - a cycle here (pass A reads what
   pass B writes, and pass B reads what pass A writes) is a genuine
   authoring bug and MUST be reported loudly (`throw std::runtime_error` -
   this is a program-error condition, not a "degrade gracefully" one, since
   it can only be caused by the ENGINE'S OWN pass declarations, never by
   user-authored asset/scene data), never silently resolved by picking an
   arbitrary order.
4. **Lifetime computation** falls out of `executionOrder` almost for free
   once it exists: for each kept resource, scan `executionOrder` once,
   recording the first index at which ANY pass reads or writes it and the
   last index at which ANY pass reads it (a resource's last WRITE with no
   subsequent read is still kept alive through that write's own pass, since
   the write itself needs the resource to exist).

### 3.3 - Determinism as a first-class requirement

Given the SAME `CompiledGraphInput` (same passes, declared in the same
order, same reads/writes), `Compile()` must produce the EXACT SAME
`executionOrder` every single call - no `std::unordered_map` iteration-order
dependency, no hidden randomness. This matters for two concrete reasons: (1)
Phase 8's Editor panel needs a STABLE pass list to display frame-over-frame
without visual churn when nothing about the declared graph actually
changed, and (2) a flaky/nondeterministic execution order would make a
future GPU-timing regression ("pass X suddenly got slower") impossible to
attribute correctly. Implement the dependency graph and Kahn's-algorithm
queue using plain `std::vector`-indexed structures (mirroring
`ComponentStorage<T>`'s own "no hashing on the hot path" philosophy, AGENTS.md)
rather than any hash-keyed container, and break ties in the zero-in-degree
queue by ORIGINAL DECLARATION INDEX (lowest first) - this is what
guarantees that if the caller's declared order already happens to be a
valid topological order (the common case - most pass authors declare passes
in roughly the order they expect them to run), `Compile()`'s output order
matches it exactly, byte-for-byte, run after run.

### 3.4 - Tests: `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`

Entirely hand-fabricated `CompiledGraphInput` values (no `RenderGraphBuilder`
dependency required, though using it to construct fixtures is fine and
arguably more readable - either is acceptable as long as no live device is
touched):

- **Linear chain**: pass A writes texture T0, pass B reads T0 and writes
  T1, `finalOutputs = {T1}` -> `executionOrder == {A, B}`, neither culled.
- **Dead branch culled**: as above, plus pass C writes texture T2 that
  nothing ever reads -> `executionOrder == {A, B}` still (C excluded
  entirely), and T2's `ResourceLifetime` is `{-1, -1}` (never used).
- **Diamond dependency**: A writes T0; B and C both read T0 and each write
  their own output; D reads both B's and C's outputs and writes T_final;
  `finalOutputs = {T_final}` -> A precedes both B and C, both B and C
  precede D, and B/C's RELATIVE order is exactly their declaration order
  (determinism check, per 3.3 above).
- **Cycle detection**: pass A reads what B writes AND writes what B reads
  -> `Compile()` throws `std::runtime_error` (or `GTE`'s established
  exception convention - match whatever `VulkanInstance`/`FramePresenter`
  already throw, e.g. `std::runtime_error` with a descriptive message, for
  consistency).
- **Multiple writers to the same imported resource** (e.g. two passes both
  writing the swapchain image handle) preserve write-after-write
  declaration order in `executionOrder`.
- **Lifetime bounds**: for the diamond case above, confirm T0's lifetime
  spans from A's index through the LATER of B's/C's index (both read it),
  and confirm a resource read by the LAST pass in `executionOrder` has
  `lastUsePassIndex` equal to that pass's own index, not one-past-the-end
  (an off-by-one this exact test exists to pin down).
- **Determinism**: call `Compile()` twice on an identical input, assert the
  two `executionOrder` vectors compare byte-identical.

## Step 4: What We Will NOT Do (Focus)

- We will **not** attempt any form of pass REORDERING for performance
  (e.g. grouping same-target passes together, or reordering for better
  GPU cache locality) beyond what a plain, deterministic topological sort
  already gives - that class of scheduling optimization is explicitly out
  of scope for the whole MVP (Phases 1-8), noted again in Phase 9.
- We will **not** support partial/incremental recompilation (e.g. "only
  this one pass changed since last frame, patch the existing compiled
  graph") - `Compile()` always runs on the WHOLE, freshly-declared graph,
  from scratch, every frame; see Phase 6's own reasoning for why that is
  the right level of simplicity given this engine's current frame budget
  and pass count (three today, likely single-digits for the foreseeable
  future).
- We will **not** let `Compile()` allocate any GPU resource, touch a
  `VkDevice`, or read `Renderer::ColorFormat()`/`DepthFormat()` - it
  operates PURELY on the plain data Phase 1/2 produced. Any temptation to
  "just peek at the real format here to validate it" belongs in Phase 4 or
  5, never here.
- We will **not** add resource ALIASING (two non-overlapping-lifetime
  resources sharing one physical allocation) in this phase, even though
  `ResourceLifetime` is exactly the data a future aliasing pass would need -
  computing it here is deliberately forward-compatible with Phase 9, but
  Phase 3 itself only ever CONSUMES lifetimes for correctness checks
  (Step 3.4's tests), never for aliasing decisions.

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, treat it as writing a small,
  from-scratch graph-algorithms module, and hold it to that bar: every
  edge case a compilers/graph-algorithms course would flag (self-loop, a
  pass that both reads AND writes the same resource, an entirely
  disconnected pass with no reads or writes at all, an empty graph, a
  `finalOutputs` handle that was never produced by anything) needs its own
  named test case, not just the six bullet-pointed scenarios above - treat
  those six as the MINIMUM bar, not the whole checklist.
- Because this phase is pure and deterministic, it is the ideal place to
  add a `RENDERGRAPH_PHASE3_COMPLETION_REPORT.md` documenting exact
  before/after test counts and a worked example (one small diamond-shaped
  graph, shown as input declarations and resulting `executionOrder`) -
  future phases (especially Phase 8's debug panel) will want to point back
  at a concrete, understood example rather than re-deriving one.
- When Phase 6 eventually calls `Compile()` for real, if you notice the
  cycle-detection error message isn't actionable enough (doesn't name which
  passes/resources form the cycle), come back and improve it here rather
  than working around a bad error message downstream - a compiler's error
  quality IS part of its correctness bar.
