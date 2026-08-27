# RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md
### (Part 1 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

**Fixes a real correctness bug found in v1: `TextureDesc` no longer carries
a `debugName` field at all.**

v1's `TextureDesc` was:

```cpp
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool hasDepth = false;
    const char* debugName = nullptr;
    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};
```

`operator== = default` compares EVERY field, including `debugName` - which
means it compares the raw POINTER, not the string content. Phase 4's entire
resource-pooling scheme ("does an already-pooled resource have a `desc` that
equals this one?") depends on `TextureDesc::operator==` being a purely
STRUCTURAL comparison. Two logically-identical resource requests (same
width/height/format/hasDepth) issued from two different call sites - or
even the exact same call site, if its debug-name string ever isn't the
literal same pointer across frames - would compare UNEQUAL purely because
of `debugName`, silently defeating pooling and reintroducing the exact "a
fresh `RenderTexture`/`vmaCreateImage` every single frame" regression
Phase 4 exists to prevent. Worse, a naive hand-written test for this
(constructing two `TextureDesc` values from the same string literal in the
same test function) would never catch it, since the compiler is free to
(and typically does) fold identical string literals to the same address
within one translation unit - the bug would only bite in real, multi-call-
site production code, exactly the kind of thing that's expensive to
diagnose later.

**Fix: `debugName` is removed from `TextureDesc`/`BufferDesc` entirely.**
A resource's human-readable name is now threaded as a separate, sibling
piece of data everywhere it's needed - see `RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md`'s
`CreateTexture(name, desc)` (name was already a separate parameter there in
v1 - it just also, redundantly and buggily, duplicated into `desc.debugName`)
and `RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md`'s
`AcquireTexture(desc, debugName)` (also already a separate parameter in v1).
With `debugName` gone from `TextureDesc`, `operator== = default` is now
correct by construction - there is no longer any field in the struct that
isn't part of the resource's actual physical shape, so there is no longer
any way for a future field addition to silently reintroduce this exact bug
without a reviewer immediately asking "should this new field really affect
whether two descriptions describe interchangeable physical resources?".
That question is now made explicit in this document's own Step 3.1 below,
as a standing rule for any FUTURE field added to either desc struct.

Nothing else in this phase changes from v1.

---

## Step 1: The Goal (Where are we going?)

Build the render graph's entire **vocabulary** first, and build NOTHING
else. By the end of this phase, `src/Renderer/RenderGraph/RenderGraphTypes.h`
(and its companion `.cpp` for the handful of non-trivial pure functions)
will define every handle, enum, and plain descriptor struct the rest of the
campaign needs to even talk about "a transient texture," "a pass," or "a
read/write dependency" - with **zero Vulkan header dependency, zero
`Renderer`/`GpuResourceFactory` dependency, and zero behavior**. This
mirrors, deliberately and exactly, the precedent `src/Renderer/DrawStats.h`
and `src/Renderer/GpuTiming.h` already set in this engine: the pure
data/math half of a Vulkan-adjacent feature is designed and tested FIRST,
completely independent of the live-device half that consumes it later.

Nothing produced in this phase is called from anywhere yet. That is
intentional and correct - see Phase 0's own "Step 4" for why leaving this
inert until Phase 7 is the whole point of a phased campaign.

## Step 2: The Situation / The Problem (Where are we now?)

This engine has no vocabulary at all for "a resource my renderer doesn't
own yet, but will, for the duration of a computed lifetime." Every GPU
resource today is either:

- A concretely-typed, already-constructed C++ object (`Buffer`,
  `RenderTexture`, `Mesh`, `Pipeline`, `MaterialTexture`) returned by value
  from a `Renderer::Create*()` factory call, or
- Addressed afterwards via a `ResourcePool<T, HandleT>`-minted handle
  (`MeshHandle`/`PipelineHandle`/`TextureHandle`, see
  `src/Renderer/ResourcePool.h`) once `RenderSystem` has taken ownership of
  it.

Both of these describe resources that ALREADY EXIST - there is no concept
anywhere of "describe a resource I want, by format/size/usage, and let
something else decide whether/when/how to actually allocate it." That
missing concept - a resource DESCRIPTION plus a handle that stands in for
"whatever the compiler eventually decides this resolves to" - is the single
foundational primitive every later phase builds on. Phase 3 cannot compute
resource lifetimes without a resource description to reason about; Phase 4
cannot pool/reuse a physical `RenderTexture` across frames without a stable
key (the description) to match against; Phase 5 cannot synthesize a barrier
without an enum describing what KIND of access a pass declared
(`ColorAttachmentWrite` vs. `ShaderRead` vs. `DepthAttachmentReadWrite`).

`GpuResourceHandle` (`src/Renderer/Memory/GpuMemoryTracker.h`) and `Entity`
(`src/ECS/Entity.h`) are this engine's two existing precedents for "a cheap,
generational, index+generation POD identifying something, minted by the one
system that owns the real thing" - this phase's `RgTextureHandle`/
`RgBufferHandle`/`RgPassHandle` are a third and fourth application of that
exact same, already-proven pattern, not a new invention.

## Step 3: The Plan (How will we get there?)

### 3.1 - New file: `src/Renderer/RenderGraph/RenderGraphTypes.h`

Everything below lives in `namespace gte::rg` (a nested namespace,
`gte::rg`, chosen so every render-graph symbol is unambiguously
grep-able and never collides with `Renderer`'s own `Mesh`/`Buffer`/
`Texture2D` names - e.g. `rg::TextureDesc` vs. plain `Texture2D`).

**Handles** (mirrors `Entity`'s exact shape - `src/ECS/Entity.h`):

```cpp
struct TextureHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;
    bool IsValid() const noexcept { return index != kInvalidIndex; }
    friend bool operator==(TextureHandle, TextureHandle) noexcept = default;
};
struct BufferHandle { /* identical shape */ };
struct PassHandle { /* identical shape */ };
```

Three distinct types (never a shared `template<Tag> struct Handle`) so the
compiler catches "passed a `TextureHandle` where a `BufferHandle` was
expected" at compile time - the same reasoning `MeshHandle`/`PipelineHandle`/
`TextureHandle` are already three distinct types in
`src/Renderer/MeshHandle.h`/`PipelineHandle.h`/`TextureHandle.h` rather than
one generic template instantiated three ways with implicit convertibility.

**Resource kind enums** - deliberately small, deliberately named after
WHAT THEY DO, not raw Vulkan enum values (mirrors `BufferMemoryUsage` in
`Buffer.h`, which is `GpuOnly`/`CpuToGpu`/`GpuToCpu`, never a raw
`VkMemoryPropertyFlags`):

```cpp
enum class ResourceAccess : std::uint8_t {
    ColorAttachmentWrite,
    DepthStencilAttachmentReadWrite,
    ShaderRead,          // sampled in a fragment shader (e.g. a material texture / a previous pass's output)
    TransferSrc,
    TransferDst,
};
```

`ResourceAccess` is Phase 5's raw material - every declared read/write in
Phase 2's builder API records one of these per resource per pass; Phase 5
turns a `(previousAccess, nextAccess)` pair into a concrete
`VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2`. This enum is intentionally
scoped to exactly what Phases 1-8's graphics-only MVP needs - `Phase 9`'s
compute-pass backlog is where `ShaderReadWrite`/`ShaderWrite` (storage
image/buffer access, needed for a compute pass) would be added, not now.

**Resource descriptors** - the "what do you want," independent of "what you
got." **v2: no `debugName` field on either struct (see this document's own
Revision Notes above) - a resource's name is threaded as a wholly separate
parameter everywhere a name is needed, never as part of a value compared
for pool-matching equality:**

```cpp
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED; // VK_FORMAT_UNDEFINED == "match Renderer::ColorFormat()", same convention as CreateRenderTexture()
    bool hasDepth = false;   // whether this logical resource also carries a companion DepthBuffer, mirroring RenderTexture's own shape
    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};
struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    friend bool operator==(const BufferDesc&, const BufferDesc&) noexcept = default;
};
```

`operator==` is REQUIRED, not incidental - Phase 4's resource pool matches
"do I already have a physical resource whose desc equals this one" purely
by value equality, so a `TextureDesc`/`BufferDesc` must be a plain,
comparable, hashable-by-extension value type where EVERY field genuinely
describes the resource's PHYSICAL SHAPE (what determines whether two
requests can share one underlying `VkImage`/`VkBuffer`), and nothing else.
**Standing rule for anyone adding a field to either desc struct in a later
phase: before adding it, ask "does this field change whether two
requests can share one physical allocation?" If the answer is no (e.g. a
cosmetic label, an author-supplied hint that doesn't affect the actual
`vmaCreateImage`/`vmaCreateBuffer` call), it does NOT belong in this
struct - thread it as a separate parameter instead, exactly as `debugName`
now is, so `operator== = default` never silently drifts back into
comparing something it shouldn't.** `VkFormat`/`VkDeviceSize`/
`VkBufferUsageFlags` are the ONLY Vulkan-header types allowed to appear
here - forward-declared/typedef'd through a minimal `<volk.h>` include
exactly like `FrameRecorder.h` already does (it includes `<volk.h>` purely
for `VkPipeline`/`VkBuffer`/etc. handle typedefs, with zero device-level
Vulkan CALLS in that same header) - this keeps `RenderGraphTypes.h` in the
same "Vulkan-header-present but Vulkan-call-free, hence still hand-
verifiable/testable with no live device" bucket as `FrameRecorder.h`'s own
`DrawItem`, not the fully Vulkan-free bucket `GpuTiming.h` achieves. This is
a deliberate, documented difference from `GpuTiming.h` - handle-shaped
Vulkan enums (`VkFormat`, a plain `uint32_t`-sized enum) cost nothing to
depend on and are unavoidable if a resource descriptor is going to describe
a REAL Vulkan resource at all; only actual Vulkan **function calls** need to
stay out of Tier-1-tested code, and none exist in this file.

**Pass metadata** (the pure record Phase 2's builder fills in, Phase 3
reads):

```cpp
struct ResourceUsage {
    TextureHandle texture; // or BufferHandle bufferResource - see Phase 2 for the tagged-union shape actually used
    ResourceAccess access = ResourceAccess::ShaderRead;
};
struct PassRecord {
    const char* name = nullptr; // must be a string literal / static storage, mirrors GTE_PROFILE_SCOPE's own rule
    std::vector<ResourceUsage> reads;
    std::vector<ResourceUsage> writes;
    bool isCulled = false; // written by Phase 3's compiler, read by Phase 6's executor
};
```

`PassRecord::name` intentionally stays where a resource's `debugName` used
to live: as a plain, un-compared-for-equality field, since `PassRecord`
values are never matched against each other for pooling/identity purposes
the way `TextureDesc`/`BufferDesc` are (see Phase 2 v2 - a pass is never
"upserted by name"; every `AddPass()` call mints a brand new `PassRecord`
every frame). There is no equivalent risk here.

### 3.2 - New file: `src/Renderer/RenderGraph/RenderGraphTypes.cpp`

Only the handful of genuinely non-trivial pure functions belong here, kept
separate from the header exactly like `GpuTiming.h`/`GpuTiming.cpp` split
declarations from logic:

- `bool IsWriteAccess(ResourceAccess) noexcept` - a small, exhaustive
  `switch` (no `default:` - see Phase 3's own reasoning on why an
  unhandled enumerator must be a compile warning, not a silent
  `false`) used by Phase 3's dependency-edge computation.
- `const char* ToString(ResourceAccess) noexcept` - for Phase 8's debug
  dump and any future assertion message; every enum in this campaign gets
  one of these from day one, mirroring `GpuTimingSample::Status`'s own
  eventual `ToString()`-shaped consumers in `MemoryPanelData.cpp`.

### 3.3 - Tests: `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`

Entirely hand-constructed, zero live device, following
`tests/Renderer/DrawStatsTests.cpp`'s own table-driven style:

- Handle equality/inequality/`IsValid()` for both a default-constructed
  (invalid) and an explicitly-constructed (valid) handle of each of the
  three handle types.
- `TextureDesc`/`BufferDesc` value equality - two descs built with
  identical fields compare equal; changing any single field (width, format,
  `hasDepth`, usage flags) makes them compare unequal - this is the exact
  behavior Phase 4's pool-matching logic will depend on, so it is pinned
  here first, independently, before Phase 4 ever exists. **v2: since
  `debugName` no longer exists on either struct, add an explicit
  regression test asserting that `TextureDesc` has NO field whose
  in-place mutation would need special-casing for pooling purposes - in
  practice, this means: construct two `TextureDesc` values field-by-field
  from entirely separately-allocated (never string-literal-folded, since
  there's no string at all anymore) inputs and confirm they still compare
  equal when structurally identical. This is the direct regression test
  for the bug this v2 revision fixes - it must keep passing even if a
  future phase re-adds some kind of cosmetic/author-facing field to
  either struct, per this document's own "standing rule" in 3.1.**
- `IsWriteAccess()` - a case for every enumerator (a genuine
  regression test: adding a future `ResourceAccess::ShaderReadWrite` in
  Phase 9 without updating this switch must fail to compile, per the
  "no `default:`" rule above - and this test must be extended in the SAME
  change that adds the new enumerator, per AGENTS.md's "every change to
  Tier 1 code must come with a matching test change").
- `ToString()` - one assertion per enumerator, non-empty, non-null.

### 3.4 - Acceptance criteria for this phase

- `RenderGraphTypesTests.cpp` compiles and passes with ZERO other
  `src/Renderer/RenderGraph/` files existing yet beyond `Types.h/.cpp`.
- No existing file anywhere in `src/` includes
  `RenderGraph/RenderGraphTypes.h` yet - grep-confirmable, and expected:
  nothing consumes this until Phase 2.
- `CMakeLists.txt`/`tests/CMakeLists.txt` gain exactly one new source pair
  and one new test file, added the same way `GpuTiming.h/.cpp` and
  `tests/Renderer/GpuTimingTests.cpp` were added in Phase 4A of the GPU
  timestamp campaign - copy that PR's shape for the build-system diff.
- **v2: `TextureDesc`/`BufferDesc` contain no field that is not a genuine
  determinant of physical-resource shareability** - reviewed explicitly
  against the "standing rule" in 3.1 before this phase is considered done.

## Step 4: What We Will NOT Do (Focus)

- We will **not** write `RenderGraphBuilder`, `RenderGraph`, or any class
  with behavior in this phase - only data, enums, and small pure free
  functions operating on that data. If a function needs a `Registry&`, a
  `Renderer&`, or a `VkDevice`, it does not belong in this phase.
- We will **not** decide yet exactly how a pass's execute callback is
  shaped (`std::function<void(PassContext&)>` vs. something else) - that is
  Phase 2's decision, once the builder API around it exists to give it
  context. This phase only defines the DATA a pass carries about itself
  (name, declared reads/writes), never the CODE a pass runs.
- We will **not** add `ResourceAccess` values this MVP doesn't need yet
  (storage-image/buffer read-write for a future compute pass, transfer
  operations beyond src/dst, present-src as a first-class access). Keep the
  enum exactly as large as Phases 1-8 need and not one value larger - Phase
  9 is where it grows.
- We will **not** give `TextureDesc`/`BufferDesc` ANY field that exists
  purely for human/debug consumption (a name, a category tag, anything not
  read by the actual `vmaCreateImage`/`vmaCreateBuffer` call it eventually
  drives) - see this document's own Revision Notes and the "standing rule"
  in Step 3.1 for exactly why this restriction now exists and must not be
  quietly relaxed later.

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, your entire deliverable is two small
  files plus one test file, and your entire review checklist is: "does this
  compile with zero Vulkan device, zero Renderer, zero Registry anywhere in
  it, does every enum have an exhaustive switch (no `default:`) somewhere
  that will force a compile error the day someone adds a new enumerator
  without updating every consumer, and does `TextureDesc`/`BufferDesc`
  contain nothing but fields that genuinely determine physical-resource
  shareability?"
- Do not be tempted to "just also write the builder while I'm in here" -
  the entire value of this phased campaign is that Phase 1's diff is small
  enough to review in five minutes and impossible to get subtly wrong,
  which is exactly what makes Phase 2 safe to build on top of it
  afterwards. Small, verifiably-correct foundations compound; large,
  everything-at-once diffs do not. **This phase's own v1 diff was, in
  fact, subtly wrong (see Revision Notes) precisely because a cosmetic-
  looking field (`debugName`) was added to a struct whose whole job is
  value-equality-based matching, without anyone asking whether it should
  participate in that equality - treat that as the concrete cautionary
  example for this bullet, not just an abstract principle.**
- When you reach Phase 2 and need "one more enum value" or "one more field
  on `PassRecord`," come back and amend THIS file/phase, with its own test
  update, rather than smuggling a silent addition into Phase 2's own diff -
  keep the phase boundary honest so a future reader of
  `RENDERGRAPH_PHASE1_COMPLETION_REPORT.md` can trust that it actually
  describes what Phase 1 shipped.
