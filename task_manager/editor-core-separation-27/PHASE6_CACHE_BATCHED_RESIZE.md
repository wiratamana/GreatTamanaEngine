# PHASE6 — Bounded, Batched Resize (One `vkDeviceWaitIdle()` Per Real Frame, Not One Per Entry)

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`. Also re-read `PHASE5_COMPLETION_REPORT.md` —
this phase further extends the SAME `Resolve()` method (now taking
`owner, name, desc, currentFrame`) and the SAME two cache files.

## Step 1: The Goal (Where are we going?)

FR4/FR8/Section 5.5: when an existing persistent entry's requested
`desc.width`/`desc.height` differs from its CURRENT extent, this is a
genuine resize request (e.g. the user is dragging the Game/Scene panel's
border). Handling this INLINE (destroy-then-recreate the moment it's
detected, per-entry) would mean a single window-resize event that affects
N persistent entries pays N separate `vkDeviceWaitIdle()` stalls in one
real frame — the exact avoidable, multiplicative cost this phase exists to
prevent. Instead: the request is QUEUED, this call still returns the
CURRENT (soon-to-be-stale but valid) texture for THIS frame, and exactly
ONE combined `vkDeviceWaitIdle()` flushes the WHOLE batch at the end of
that same real frame.

## Step 2: The Situation (Where are we now?)

- **A real, load-bearing correction found by direct code read**: the
  source document's own prose just says "issues exactly one
  `vkDeviceWaitIdle()`" without naming a specific call. Two candidates
  exist in this codebase, and they are NOT interchangeable:
  - `Renderer::WaitForGpuIdle()` (`Renderer.h` ~line 337-354) — a full,
    blocking `vkDeviceWaitIdle()` wrapper, but its OWN doc comment
    explicitly says **"NEVER call this from any per-frame/performance-
    sensitive path"** — reserved for `GET /get_texture` and
    `ProjectAssemblyHost::UnloadProjectAssembly()` only. This phase's own
    resize flush DOES run on a per-frame path (`RenderGraph::ExecuteCompiledGraph()`'s
    own tail, PHASE8) — calling `WaitForGpuIdle()` here would directly
    contradict that method's own documented restriction.
  - `RenderFeatureCompositor::EnsureTextureSized()` (`RenderFeatureCompositor.cpp`
    ~line 531-551) — the ALREADY-SHIPPED, EXACT precedent for "a rare,
    user-driven resize gets a direct, bare `vkDeviceWaitIdle(m_device)`
    call" (confirmed, ~line 548: `vkDeviceWaitIdle(m_device);` then
    `texture->Resize(width, height);`), where `m_device` is a plain
    `VkDevice` this class stores directly — NOT routed through
    `Renderer::WaitForGpuIdle()` at all.
  - **This phase follows the `RenderFeatureCompositor` precedent, not
    `Renderer::WaitForGpuIdle()`** — `RenderGraphPersistentResourceCache`
    must store its own plain `VkDevice` (fetched once, at construction,
    exactly like `RenderGraphTimestampPool` already does — see below) and
    call the raw Vulkan function directly.
- `Renderer::GetVulkanContextInfo()` (`Renderer.h` ~line 875, returns a
  `VulkanContextInfo` struct with a `device` field, ~line 853) is the
  ALREADY-ESTABLISHED way a `RenderGraph`-adjacent class obtains a raw
  `VkDevice`/`VkQueue`/etc. without reaching into `Renderer`'s own private
  internals — `RenderGraph.h`'s own doc comment confirms
  `RenderGraphTimestampPool` is already constructed this exact way
  ("constructed from `Renderer::GetVulkanContextInfo()`'s own
  device/graphicsQueue/graphicsQueueFamily/timestampCapability fields").
  `RenderGraphPersistentResourceCache`'s constructor (PHASE4) currently
  only stores `Renderer* m_renderer` — this phase EXTENDS that
  constructor's body (not its signature — it still just takes
  `Renderer&`) to additionally call `renderer.GetVulkanContextInfo()` once
  and cache the returned `.device` into a new `VkDevice m_device` member.
- `RenderTexture::Resize()` (`RenderTexture.h` ~line 99-107) is already the
  exact "destroy-then-recreate at a new size" primitive this phase needs —
  confirmed it re-attaches the SAME `m_debugName`/`m_depthDebugName`
  pointers it was originally constructed with (no change needed there —
  this cache's own map key, passed as `debugName` at construction time per
  PHASE4, stays correctly attached across any number of future `Resize()`
  calls, since the map key's own address never moves, TR4).

## Step 3: The Plan

### 3.1 — Cache the raw `VkDevice` (constructor extension)

```cpp
// RenderGraphPersistentResourceCache.h - add:
VkDevice m_device = VK_NULL_HANDLE;
```

```cpp
// .cpp - extend the existing constructor body:
RenderGraphPersistentResourceCache::RenderGraphPersistentResourceCache(Renderer& renderer) noexcept
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
{
}
```

(Re-confirm `Renderer::GetVulkanContextInfo()` is callable from a
`noexcept` constructor context safely — it is a plain getter over
already-live state, confirmed no throwing work inside it via a quick read
of its `.cpp` body before relying on this.) Declare the new `m_device`
member immediately AFTER the existing `Renderer* m_renderer` field (not
appended after `m_entries`/`m_nextEntryEpoch`) so its position in the class
matches its position in the constructor's member-initializer list above —
avoids a `-Wreorder` mismatch (harmless on this project's own toolchain,
per AGENTS.md's "no `-Wall`/`-Werror`" note, but free to avoid regardless).

**A genuine, concrete compile-time gap this phase must ALSO close, confirmed
by direct code read**: `ExecuteTimingMode` (needed below, Section 3.2, as a
new `Resolve()` parameter type) is NOT declared anywhere `RenderGraphPersistentResourceCache.h`
already includes (`RenderGraphTypes.h`/`../RenderTexture.h`, per PHASE4) — it
is declared, in full, inside `RenderGraph.h` itself (confirmed:
`enum class ExecuteTimingMode : std::uint8_t { SynchronousImmediateReadback,
PipelinedDeferredReadback };`), and `RenderGraphPersistentResourceCache.h`
must NOT `#include "RenderGraph.h"` (PHASE7 makes `RenderGraph.h` include
THIS header, so a reverse include here would be a genuine header-to-header
cycle). This is the EXACT same shape `RenderGraphDebugTextureRegistry.h`
already solves for the identical reason — mirror it verbatim:
1. Add, near the top of `RenderGraphPersistentResourceCache.h`, inside
   `namespace gte::rg { ... }`:
   ```cpp
   enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared here to avoid a circular include, mirroring RenderGraphDebugTextureRegistry.h's own identical precedent.
   ```
   (a forward declaration WITH a fixed underlying type is a complete-enough
   type for a function parameter/member declaration - confirmed by
   `RenderGraphDebugTextureRegistry.h`'s own already-shipped
   `ExecuteTimingMode regime{};` struct field using this exact same forward
   declaration successfully today.)
2. This forward declaration is NOT enough for `Resolve()`'s own `.cpp` BODY
   below (Section 3.2), which needs the actual ENUMERATOR VALUES
   (`ExecuteTimingMode::PipelinedDeferredReadback`) - a forward-declared enum
   only allows the TYPE to be named, never its specific enumerators. Add
   `#include "RenderGraph.h"` to `RenderGraphPersistentResourceCache.cpp`
   (the .cpp file only - never the .h) - completely safe regardless of
   whether PHASE7 has landed yet, since a .cpp file is never itself included
   by anything else, so this can never participate in a header cycle.

### 3.2 — `PendingResize` + queuing, inside `Resolve()`

Add, as a new private nested type + member:

```cpp
struct PendingResize {
    PersistentResourceCacheEntry* entry = nullptr;
    std::uint32_t newWidth = 0;
    std::uint32_t newHeight = 0;
};
std::vector<PendingResize> m_pendingResizes;

void QueueResize(PersistentResourceCacheEntry* entry, std::uint32_t newWidth, std::uint32_t newHeight);
```

```cpp
void RenderGraphPersistentResourceCache::QueueResize(
    PersistentResourceCacheEntry* entry, std::uint32_t newWidth, std::uint32_t newHeight)
{
    // Last request THIS frame wins for the SAME entry - matches "single
    // builder, single frame" reasoning used elsewhere in this engine.
    for (PendingResize& pending : m_pendingResizes) {
        if (pending.entry == entry) {
            pending.newWidth = newWidth;
            pending.newHeight = newHeight;
            return;
        }
    }
    m_pendingResizes.push_back(PendingResize{ entry, newWidth, newHeight });
}
```

`Resolve()` grows a FIFTH parameter (PHASE5 already grew it to four, adding
`currentFrame` — this phase appends one more), `ExecuteTimingMode timingMode`
(still safe to change freely — no production caller exists until PHASE8).
Insert this new branch into the EXISTING body, immediately AFTER the
"construct if not already constructed" block (i.e. only when
`it->second.texture.has_value()` was ALREADY true on entry — a genuinely
pre-existing entry, never a brand-new one):

```cpp
if (/* entry already existed before this call, i.e. !inserted */ !inserted) {
    if (desc.format != it->second.desc.format) {
        // FR4 - a format (or, by extension, hasDepth) change on an
        // EXISTING entry is ALWAYS a caller bug, independent of regime -
        // logged loudly, never silently reinterpreted, never queued as a
        // resize.
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "\"" + key + "\" was re-requested with a DIFFERENT format than its existing entry - "
              "this is always a caller bug, refusing this call.");
        return std::nullopt;
    }
    if (desc.width != it->second.desc.width || desc.height != it->second.desc.height) {
        if (timingMode == ExecuteTimingMode::PipelinedDeferredReadback) {
            // FR4 - a resize request from the pipelined/Present regime is
            // refused immediately: keep the entry's CURRENT extent,
            // ignore desc.width/height for THIS call only - never queued,
            // never batched, since that regime must never issue a
            // vkDeviceWaitIdle() at all.
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" resize requested from the PipelinedDeferredReadback regime - refusing "
                  "the resize (keeping the current extent) for this call only; a resize must be "
                  "requested from the SynchronousImmediateReadback regime.");
        } else {
            QueueResize(&it->second, desc.width, desc.height);
        }
        // Either way, THIS call's own returned texture/extent is still the
        // entry's CURRENT (old) one - never the newly-requested size.
    }
}
```

### 3.3 — `FlushPendingResizes()`

```cpp
// Header
void FlushPendingResizes();
```

```cpp
void RenderGraphPersistentResourceCache::FlushPendingResizes()
{
    if (m_pendingResizes.empty()) {
        return;
    }
    // Exactly ONE combined stall for the WHOLE batch, no matter how many
    // entries need resizing this frame (Section 5.5/FR8) - mirrors
    // RenderFeatureCompositor::EnsureTextureSized()'s own direct
    // vkDeviceWaitIdle(VkDevice) call, NEVER Renderer::WaitForGpuIdle()
    // (see this phase's own Step 2 "Situation" for why that wrapper is
    // wrong here).
    vkDeviceWaitIdle(m_device);
    for (const PendingResize& pending : m_pendingResizes) {
        pending.entry->texture->Resize(
            static_cast<int>(pending.newWidth), static_cast<int>(pending.newHeight));
        pending.entry->desc.width = pending.newWidth;
        pending.entry->desc.height = pending.newHeight;
        // Section 5.1 - a resize atomically resets the remembered layout:
        // a freshly vmaCreateImage()'d VkImage is always VK_IMAGE_LAYOUT_UNDEFINED
        // (see RenderTexture::Create()'s own imageInfo.initialLayout).
        pending.entry->lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    m_pendingResizes.clear();
}
```

**Do not wire `FlushPendingResizes()` into `RenderGraph::ExecuteCompiledGraph()`
yet** — that call site is PHASE8's own job (right alongside
`RecordFinalLayout()`'s loop, same cadence, same `!isPipelined` gating).
This phase only builds the mechanism and proves it directly via the
PHASE4 headless fixture, calling `cache.FlushPendingResizes()` by hand from
a test.

## Step 4: Required Tests

Extend `RenderGraphPersistentResourceCacheTests.cpp` (no new file):

1. **Basic resize**: `Resolve("Test", "Resize", desc64x64,
   ExecuteTimingMode::SynchronousImmediateReadback, frame=1)`; then
   `Resolve()` the SAME identity again with `desc128x128` (same call,
   frame=2, or a fresh `RunSynchronousFrame()`). Confirm (a) THIS second
   call's own returned `ResolvedTexture::texture->Extent()` is STILL
   64x64 (old extent — the resize hasn't happened yet), (b) after calling
   `FlushPendingResizes()` by hand, the SAME `RenderTexture*`'s `Extent()`
   is now 128x128, and (c) `lastKnownLayout` was reset to
   `VK_IMAGE_LAYOUT_UNDEFINED` on that same entry.
2. **Batched — the single most important test in this phase**: create
   THREE OR MORE distinct persistent entries via `Resolve()`, then request
   a DIFFERENT new size for all three (same frame). Instrument/count real
   `vkDeviceWaitIdle()` calls — since this project has no existing
   Vulkan-call-counting test harness, the most direct proof available is:
   wrap `FlushPendingResizes()`'s call to `vkDeviceWaitIdle()` is NOT
   itself mockable without a bigger refactor, so instead confirm this
   INDIRECTLY but rigorously: call `FlushPendingResizes()` exactly ONCE
   and confirm ALL THREE entries' extents/layouts were updated correctly
   by that SINGLE call (proving the batching logic itself is correct —
   the "exactly one call total" claim is then a direct, simple code-read
   confirmation that `FlushPendingResizes()`'s body contains exactly one,
   unconditional `vkDeviceWaitIdle()` statement outside any loop — state
   this reasoning explicitly in the completion report rather than
   asserting something this test suite cannot directly instrument).
3. **Last-request-wins**: request TWO different new sizes for the SAME
   entity within the same frame (two `Resolve()` calls, same identity, two
   different `desc.width`/`desc.height` values, before ever calling
   `FlushPendingResizes()`) — confirm only ONE entry ends up in
   `m_pendingResizes` (indirectly: confirm the FINAL flushed size matches
   the SECOND request, never the first) — expose a small,
   test-only/friend-accessible way to inspect `m_pendingResizes.size()` if
   needed, or infer it purely from the post-flush extent being the second
   request's size.
4. **Pipelined-regime resize refusal**: `Resolve()` an existing entry with
   a genuinely different size, but pass
   `ExecuteTimingMode::PipelinedDeferredReadback` this time — confirm (a)
   the call still SUCCEEDS (returns a valid `ResolvedTexture`, not
   `std::nullopt` — only the resize portion is refused, not the whole
   request), (b) the entry's extent is UNCHANGED afterward, and (c)
   `m_pendingResizes` gained NO new entry for it (confirm indirectly: a
   subsequent `FlushPendingResizes()` call does not touch this entry's
   extent at all).
5. **Format-change refusal**: `Resolve()` an existing entry with the SAME
   width/height but a DIFFERENT `desc.format` — confirm `std::nullopt` is
   returned (a hard refusal, unlike the softer width/height resize path)
   regardless of `timingMode`.

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. No new files. Watch
Rule 8 (`TR2`/`TR3`) carefully here — `vkDeviceWaitIdle` is a real, global
GPU stall; confirm this phase's own test file never leaves a
`FlushPendingResizes()` call un-invoked in a way that would leave a stale
extent mismatch bleeding into a LATER, unrelated test in the same binary
(each test should use its OWN fresh `HeadlessRenderGraphFixture`/cache
instance — confirm this is already how PHASE4's test file is structured;
if tests currently share one fixture across cases, fix that before adding
these). Incremental build + `ctest -R
RenderGraphPersistentResourceCacheTest`. End with
`PHASE6_COMPLETION_REPORT.md` + git commit.
