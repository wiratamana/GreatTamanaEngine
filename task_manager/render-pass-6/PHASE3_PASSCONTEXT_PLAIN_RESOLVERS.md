# PHASE3 — `PassContext`: `std::function` → Plain Resolver Struct (item 2.7)

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first). Also read
`PHASE2_COMPLETION_REPORT.md` — this phase edits the `BuildPassContext()`
method PHASE2 just extracted, and depends on its exact final signature.

## Step 1: The Goal (Where are we going?)

Every pass, every `RenderGraph::Execute()` call (twice per frame), currently
allocates six fresh, independently-constructed `std::function` closures on
`PassContext` (`resolveReadTexture`, `resolveTexture`, `resolveBuffer`,
`resolveVolumeTexture`, `recordDraw`, `recordIndirectDraw`), each closing
over 1-2 local vector references. This is the single highest-value "increase
speed" item in the whole refactor plan because it is purely internal and
touches no external call site.

After this phase:

- `PassContext`'s six fields are replaced by a small set of plain,
  non-owning pointers to the three `physicalX` vectors and a `DrawStats*`.
- `resolveReadTexture()`/`resolveTexture()`/`resolveBuffer()`/
  `resolveVolumeTexture()`/`recordDraw()`/`recordIndirectDraw()` become
  ordinary (non-virtual, non-type-erased) **member functions** on
  `PassContext` itself, indexing those pointers directly.
- **Every pass author's call site is byte-for-byte unchanged** —
  `ctx.resolveReadTexture(handle)`, `ctx.recordDraw(...)`, etc. compile and
  behave identically. This is an internals-only change to `PassContext`.
- Zero heap-capture-shaped closures, zero indirect (vtable-style) calls, on
  this per-pass, per-frame hot path.

## Step 2: The Situation (Where are we now?)

`PassContext` (`src/Renderer/RenderGraph/RenderGraph.h`, lines 89-180) is:

```cpp
struct PassContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkExtent2D colorAttachmentExtent{};
    struct ResolvedTexture { VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE; };
    std::function<ResolvedTexture(TextureHandle)> resolveReadTexture;
    std::function<ResolvedTexture(TextureHandle)> resolveTexture; // alias of resolveReadTexture
    std::function<VkBuffer(BufferHandle)> resolveBuffer;
    struct ResolvedVolumeTexture { VkImageView view = VK_NULL_HANDLE; };
    std::function<ResolvedVolumeTexture(VolumeTextureHandle)> resolveVolumeTexture;
    std::function<void(bool, std::uint32_t, std::uint32_t)> recordDraw;
    std::function<void()> recordIndirectDraw;
};
```

After PHASE2, all six of these are constructed inside `RenderGraph::
BuildPassContext(...)` (extracted from the old inline block), each as a
capturing lambda over `physicalTextures`/`physicalBuffers`/
`physicalVolumeTextures`/`passDrawStats` references passed into
`BuildPassContext()`'s own parameters. The bodies (verbatim, unchanged since
Phase 6 of the original render-graph campaign) are:

```cpp
ctx.resolveReadTexture = [&physicalTextures](TextureHandle handle) -> PassContext::ResolvedTexture {
    if (handle.index < physicalTextures.size() && physicalTextures[handle.index].resolved) {
        const PhysicalTexture& tex = physicalTextures[handle.index];
        return PassContext::ResolvedTexture{ tex.target.imageView, tex.sampler };
    }
    return PassContext::ResolvedTexture{};
};
ctx.resolveTexture = ctx.resolveReadTexture;
ctx.resolveBuffer = [&physicalBuffers](BufferHandle handle) -> VkBuffer {
    if (handle.index < physicalBuffers.size() && physicalBuffers[handle.index].resolved) {
        return physicalBuffers[handle.index].buffer;
    }
    return VK_NULL_HANDLE;
};
ctx.resolveVolumeTexture = [&physicalVolumeTextures](VolumeTextureHandle handle) -> PassContext::ResolvedVolumeTexture {
    if (handle.index < physicalVolumeTextures.size() && physicalVolumeTextures[handle.index].resolved) {
        return PassContext::ResolvedVolumeTexture{ physicalVolumeTextures[handle.index].target.imageView };
    }
    return PassContext::ResolvedVolumeTexture{};
};
DrawStats passDrawStats;
ctx.recordDraw = [&passDrawStats](bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) {
    AccumulateDrawStats(passDrawStats, hasIndexBuffer, vertexCount, indexCount);
};
ctx.recordIndirectDraw = [&passDrawStats]() { AccumulateIndirectDrawStats(passDrawStats); };
```

`PhysicalTexture`/`PhysicalBuffer`/`PhysicalVolumeTexture` are private
nested structs of `RenderGraph` (`RenderGraph.h`, lines 386-417) — `Physical*`
vectors are owned locally inside `ExecuteCompiledGraph()`'s stack frame
(`std::vector<PhysicalTexture> physicalTextures(...)`, etc., lines 291-294),
one fresh set per `Execute()` call, and `PassContext` (and now
`BuildPassContext()`) only ever needs READ access to them except
`passDrawStats`, which needs write access via `AccumulateDrawStats()`/
`AccumulateIndirectDrawStats()` (`src/Renderer/DrawStats.h`).

Every real call site of `PassContext`'s six members today (grep confirms
these are the only production consumers, plus their own test/validation
counterparts): pass `execute` callbacks across `src/Application/
RenderPasses.cpp`, `src/Editor/ComputeBlurValidation.cpp`, `src/Editor/
GBufferValidation.cpp`, GPU Skinning's own pass bodies, GPU-driven batching's
pass bodies (render-pass-5), and Atmosphere's LUT/composite pass bodies —
all call `ctx.resolveReadTexture(handle)`/`ctx.resolveTexture(handle)`/
`ctx.resolveBuffer(handle)`/`ctx.resolveVolumeTexture(handle)`/
`ctx.recordDraw(...)`/`ctx.recordIndirectDraw()` as plain function-call
syntax — **this exact call syntax must keep compiling unmodified**, which is
naturally true if these become ordinary member functions with the same
names and parameter lists.

## Step 3: The Plan (How do we get there?)

### 3.1 — New `PassContext` shape in `RenderGraph.h`

Replace the six `std::function` fields with:

```cpp
struct PassContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkExtent2D colorAttachmentExtent{};

    struct ResolvedTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };
    struct ResolvedVolumeTexture {
        VkImageView view = VK_NULL_HANDLE;
    };

    // render-pass-6 campaign, PHASE3 (item 2.7) - REPLACES six freshly-
    // constructed std::function closures (one heap-capture-shaped object
    // + one indirect vtable-style call per resolve/record, PER PASS, PER
    // Execute() call) with plain, non-owning pointers into
    // RenderGraph::ExecuteCompiledGraph()'s own stack-local physicalX
    // vectors, plus a DrawStats* for recordDraw()/recordIndirectDraw().
    // Never null for a PassContext actually handed to a pass's `execute`
    // callback (RenderGraph::BuildPassContext() always sets every one of
    // these) - only a DEFAULT-CONSTRUCTED PassContext (never handed to a
    // real pass) leaves them null. PassContext is never stored/copied
    // beyond one pass's own `execute` call (see this struct's own
    // pre-existing doc comment) so these raw pointers' lifetime is always
    // safely bounded by that same call.
    const std::vector<PhysicalTexture>* m_textures = nullptr;
    const std::vector<PhysicalBuffer>* m_buffers = nullptr;
    const std::vector<PhysicalVolumeTexture>* m_volumeTextures = nullptr;
    DrawStats* m_drawStats = nullptr;

    // Ordinary, non-virtual member functions - same names, same parameter
    // lists, same return types, same BEHAVIOR as the six std::function
    // fields they replace (see RenderGraph.cpp's original
    // ExecuteCompiledGraph() body / PHASE2's BuildPassContext() for the
    // exact logic each mirrors). Every existing pass author call site
    // (`ctx.resolveReadTexture(handle)`, `ctx.recordDraw(...)`, etc.)
    // compiles and behaves identically - this is an INTERNALS-ONLY change.
    ResolvedTexture resolveReadTexture(TextureHandle handle) const noexcept;
    // Phase 6 of the compute-shader campaign - see this method's own
    // original doc comment (preserved verbatim at its new definition site):
    // a plain alias of resolveReadTexture() with a name that no longer
    // implies "reads only" - safe for a write-only (compute RWTexture)
    // handle too, since every declared read AND write is resolved before a
    // pass's own `execute` callback runs.
    ResolvedTexture resolveTexture(TextureHandle handle) const noexcept { return resolveReadTexture(handle); }
    VkBuffer resolveBuffer(BufferHandle handle) const noexcept;
    ResolvedVolumeTexture resolveVolumeTexture(VolumeTextureHandle handle) const noexcept;
    void recordDraw(bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) const;
    void recordIndirectDraw() const;
};
```

Notes:
- `resolveTexture()` stays a trivial one-line forwarder (now a real inline
  function call instead of a copied `std::function`, which is itself a
  small extra win — no second closure/copy at all now).
- Mark the resolve methods `const noexcept` (they never throw, never
  mutate `PassContext`) and `recordDraw`/`recordIndirectDraw` `const` (they
  mutate the pointed-to `*m_drawStats`, not `PassContext` itself — a `const`
  method mutating through a non-const pointer member is completely legal
  and correct here, mirroring how a `const` method can freely call through
  a raw pointer/reference member).
- Field names (`m_textures` etc.) are a first suggestion — if this
  codebase's own convention for a plain struct's data members differs
  (check a few other plain "just data + no `m_` prefix" structs in this same
  file, e.g. `ResourceUsage`, `ColorAttachmentDesc` — neither uses `m_`),
  prefer NO `m_` prefix instead, to match `PassContext`'s own sibling
  structs' existing convention in this file. Confirm by reading
  `RenderGraphTypes.h`'s plain data structs before finalizing — this is a
  small stylistic detail, not a locked design decision, but should match
  house style.

### 3.2 — Definitions in `RenderGraph.cpp`

```cpp
PassContext::ResolvedTexture PassContext::resolveReadTexture(TextureHandle handle) const noexcept
{
    if (m_textures != nullptr && handle.index < m_textures->size() && (*m_textures)[handle.index].resolved) {
        const RenderGraph::PhysicalTexture& tex = (*m_textures)[handle.index];
        return ResolvedTexture{ tex.target.imageView, tex.sampler };
    }
    return ResolvedTexture{};
}

VkBuffer PassContext::resolveBuffer(BufferHandle handle) const noexcept
{
    if (m_buffers != nullptr && handle.index < m_buffers->size() && (*m_buffers)[handle.index].resolved) {
        return (*m_buffers)[handle.index].buffer;
    }
    return VK_NULL_HANDLE;
}

PassContext::ResolvedVolumeTexture PassContext::resolveVolumeTexture(VolumeTextureHandle handle) const noexcept
{
    if (m_volumeTextures != nullptr && handle.index < m_volumeTextures->size()
        && (*m_volumeTextures)[handle.index].resolved) {
        return ResolvedVolumeTexture{ (*m_volumeTextures)[handle.index].target.imageView };
    }
    return ResolvedVolumeTexture{};
}

void PassContext::recordDraw(bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) const
{
    if (m_drawStats != nullptr) {
        AccumulateDrawStats(*m_drawStats, hasIndexBuffer, vertexCount, indexCount);
    }
}

void PassContext::recordIndirectDraw() const
{
    if (m_drawStats != nullptr) {
        AccumulateIndirectDrawStats(*m_drawStats);
    }
}
```

Note the added defensive `nullptr` checks (the original `std::function`
fields, if default-constructed/never assigned, would throw
`std::bad_function_call` on invocation — a raw pointer needs an explicit
guard instead to stay equally safe for a `PassContext` that was somehow
default-constructed and used directly, which should never happen in
practice but costs nothing to guard against, exactly mirroring this
codebase's general "defensive, cheap check even when a real call site
already guarantees it won't happen" style seen throughout `RenderGraph.cpp`).

`PhysicalTexture`/`PhysicalBuffer`/`PhysicalVolumeTexture` are private
nested types of `RenderGraph` — since `PassContext` is declared in the same
header (`RenderGraph.h`) but NOT nested inside `RenderGraph` itself, check
whether these definitions need `RenderGraph::PhysicalTexture` qualification
(as shown above) or whether visibility rules require a different
arrangement (e.g. moving `PhysicalTexture`/`PhysicalBuffer`/
`PhysicalVolumeTexture` out of `RenderGraph`'s private section into
namespace scope, since `PassContext` — a namespace-scope struct — cannot
name a PRIVATE nested type of another class it isn't a member/friend of).
**This is a real, load-bearing implementation detail to resolve carefully**:
confirm during implementation whether `PhysicalTexture` etc. need to move to
plain `gte::rg` namespace scope (outside `RenderGraph`, alongside
`PassContext` itself) — if so, that is a small, mechanical, additional
change this phase must make (update `RenderGraph.h`'s forward declarations
and every internal use accordingly), not a blocker. Use `ask_questions` if
genuinely unsure how to resolve this cleanly without changing anything
about `RenderGraph`'s own public surface.

### 3.3 — Update `BuildPassContext()` (PHASE2's extraction) to populate the
new fields instead of constructing lambdas

```cpp
PassContext RenderGraph::BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
    DrawStats& passDrawStats)
{
    PassContext ctx;
    ctx.cmd = cmd;
    ctx.m_textures = &physicalTextures;
    ctx.m_buffers = &physicalBuffers;
    ctx.m_volumeTextures = &physicalVolumeTextures;
    ctx.m_drawStats = &passDrawStats;
    return ctx;
}
```

This is now a small, obviously-correct four-pointer-assignment function —
exactly the "increase speed, reduce indirection" payoff this phase exists
for, made visible by PHASE2's prior extraction.

### 3.4 — Audit every real call site (verification, not modification)

Grep `src/` for `.resolveReadTexture(`, `.resolveTexture(`, `.resolveBuffer(`,
`.resolveVolumeTexture(`, `.recordDraw(`, `.recordIndirectDraw(` and confirm
every hit is plain function-call syntax on a `PassContext&`/`PassContext`
value (never `.resolveReadTexture = ...` assignment, never
`std::function`-specific operations like `.target<T>()` or a null-check via
`operator bool()`) — if any call site DOES check
`ctx.resolveReadTexture` for truthiness/emptiness the way a `std::function`
allows, that specific call site needs a small adjustment (there is no
equivalent "is this callable set" check for a plain member function — it is
always callable; if such a check exists anywhere, it was checking "did this
pass wire up a real resolver", which after this phase is unconditionally
true for a `PassContext` built by `BuildPassContext()` and irrelevant
otherwise). This audit is expected to find zero such cases (every known
production call site is a direct function call), but must be performed, not
assumed.

### 3.5 — Tests

`tests/Renderer/RenderGraph/` currently has no `PassContext`-specific test
file (it is Tier-2/GPU-adjacent — real `PhysicalTexture`/`PhysicalBuffer`
data only exists inside a live `ExecuteCompiledGraph()` call). If, while
implementing, `PassContext`'s new member functions can be exercised with
pure fabricated `PhysicalTexture`/`PhysicalBuffer` vectors and no live
`VkDevice` (they plausibly can, since `PhysicalTexture`/etc. are themselves
plain data structs with no live Vulkan calls needed to construct one by
hand), add a small new Tier-1 test file,
`tests/Renderer/RenderGraph/RenderGraphPassContextTests.cpp`, asserting:
resolving a handle within range and `resolved == true` returns the expected
view/sampler/buffer; resolving an out-of-range handle or one with
`resolved == false` returns a default/null result; `resolveTexture()`
produces the same result as `resolveReadTexture()` for the same handle;
`recordDraw()`/`recordIndirectDraw()` correctly mutate the pointed-to
`DrawStats` via the existing `AccumulateDrawStats()`/
`AccumulateIndirectDrawStats()` functions (already independently tested
elsewhere — this test only needs to confirm `PassContext` correctly forwards
into them). This is a genuine, newly-opened Tier-1 test opportunity this
phase's own refactor creates — take it if the construction genuinely needs
no live `VkDevice`; skip it (documenting why in the completion report) if it
turns out to secretly need one.

### 3.6 — What NOT to do in this phase

- Do NOT change any pass author's `execute` callback body anywhere in
  `src/Application/`, `src/Editor/`, or any Renderer-layer pass declaration
  file — every one of them must compile completely unmodified.
- Do NOT change `RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` — unrelated
  to this phase.
- Do NOT reorder or rename `resolveReadTexture`/`resolveTexture`/
  `resolveBuffer`/`resolveVolumeTexture`/`recordDraw`/`recordIndirectDraw` —
  these exact names are the entire point of "zero call-site change".

## Definition of Done for this phase

- `PassContext` in `RenderGraph.h` has zero `std::function` fields; all six
  resolve/record operations are ordinary member functions with identical
  names/signatures/behavior to before.
- Every existing pass `execute` callback across the whole codebase compiles
  completely unmodified (verified by the fast incremental compile check
  actually succeeding for the WHOLE engine target, not just
  `RenderGraph.cpp` in isolation).
- `BuildPassContext()` is now a small, obviously-correct pointer-assignment
  function.
- A live sanity check (via `run_app_background` + `gte_send_request`) shows
  rendering is visually unchanged, and the Editor's Frame Debugger/"Render
  Graph" panel still shows correct per-pass draw stats (proving
  `recordDraw`/`recordIndirectDraw` still correctly accumulate through the
  new pointer-based path).
- `PHASE3_COMPLETION_REPORT.md` is written, documenting: the final decision
  on `PhysicalTexture`/etc.'s namespace placement (3.2's open question), the
  final field-naming convention chosen (3.1's `m_` question), and whether
  the new Tier-1 test file (3.5) was added or consciously skipped (with
  reasoning either way).
- Changes are committed via `git_add`/`git_commit`.

**Remember**: use `ask_questions` for the two explicitly-flagged open
implementation questions above (private nested type visibility; field
naming convention) if they are not obviously resolved once you're looking at
the real code, and for anything else genuinely ambiguous.
