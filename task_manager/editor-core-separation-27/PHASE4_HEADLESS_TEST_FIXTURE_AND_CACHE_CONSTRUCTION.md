# PHASE4 — Headless GPU Test Fixture + `RenderGraphPersistentResourceCache` Construction/Ownership Core

Campaign folder: `task_manager/editor-core-separation-27/`

**This is this campaign's own single highest-risk phase** (mirrors PHASE6's
role in `editor-core-separation-26`). Budget extra care here. Consider
using `dispatch_sub_agent` to independently double-check this phase's own
work before writing its completion report (see `PHASE0_MASTER_STRATEGY.md`,
Rule 4) — do NOT have that sub-agent create its own report file.

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`, especially Locked Decisions 3, 5, 6 and
Correction 3. Also re-read `PHASE1_COMPLETION_REPORT.md` (the
`createDepthCompanion` parameter this phase directly consumes) and
`PHASE2_COMPLETION_REPORT.md` (`PersistentTextureCacheToken`,
`IsStaleCacheEntry()` already live in `RenderGraphPersistentResourceCache.h`
— this phase EXTENDS that same file, never creates a second one).

## Step 1: The Goal (Where are we going?)

Two deliverables:

1. **`tests/Fakes/HeadlessRenderGraphFixture.h`** — a small, reusable test
   fixture wrapping a real, headless (`VK_EXT_headless_surface`) `Renderer`
   + a real `RenderGraph`, with a helper to drive one real
   `SynchronousImmediateReadback` `Execute()` frame through a caller-
   supplied build lambda — with NO `Core`/`Game`/`EditorLayer`/SDL/ImGui
   involved at all. This becomes the primary, automated, `ctest`-driven
   proof mechanism for every live-GPU acceptance criterion in this
   campaign (PHASE4 onward) and for BIG STEP 4 later.
2. **`RenderGraphPersistentResourceCache`'s real, live-`VkDevice`-touching
   core** — the class that actually owns entries, keyed by a mechanically-
   collision-safe `(owner, name)` string, constructed via the source
   document's mandatory, exception-safe, two-phase recipe (Section 6.1),
   enforcing "color only, no depth companion" (Section 7) physically, not
   just as a bookkeeping flag.

When this phase is done: a hand-written Tier-2 test, using the new
fixture, can call `cache.Resolve("Test", "Test", desc)` across 3 separate,
real, driven `Execute()` frames and observe the SAME `RenderTexture*`
(and, via `DebugTextureSnapshotFor()`, the same underlying `VkImage`)
returned every time — the single most important, single most novel proof
in this whole campaign, landing here first, before anything else depends
on it.

**Non-goals for this phase specifically** (later phases add these onto the
SAME class/method): age-tracking, the same-frame double-request refusal,
eviction, the debug token-identity misuse guard, resize handling, and the
honest-layout-recording wiring. `Resolve()`'s signature in THIS phase is
deliberately minimal (`owner`, `name`, `desc` only) — later phases grow it.
Since NOTHING calls `Resolve()` in production yet (that only happens in
PHASE8), growing its signature between phases breaks no real caller.

## Step 2: The Situation (Where are we now?)

- `tests/Fakes/HeadlessSurfaceProvider.h` (confirmed, read in full) is a
  real, already-proven, already-used `ISurfaceProvider` requiring NOTHING
  but a `VkInstance` — it throws `std::runtime_error` (never UB) if this
  machine's driver lacks `VK_EXT_headless_surface`.
- `Renderer`'s constructor is `explicit Renderer(ISurfaceProvider&
  surfaceProvider);` (`src/Renderer/Renderer.h` ~line 83) — genuinely ONE
  parameter, confirmed by direct read. No `Core`/`Window`/`Game` needed to
  construct one.
- `Renderer::BeginOffscreenRenderGraphRecording()` /
  `EndOffscreenRenderGraphRecording()` (`Renderer.h` ~line 155-159) are
  PUBLIC methods on `Renderer` itself — confirmed these are what
  `Core::BuildFrame()` uses today (`Core.cpp` ~line 1171/1500) around its
  own `m_renderGraph.Execute(...)` call. A test can call the EXACT same
  pair directly, with no `Core` involved.
  `RenderGraph`'s constructor is `explicit RenderGraph(Renderer&
  renderer);` (`RenderGraph.h` ~line 150) — also genuinely one parameter.
- No existing test file drives a real `RenderGraph::Execute()` frame
  through a headless `Renderer` yet (confirmed: the five existing
  `HeadlessSurfaceProvider` consumers all only construct a full `Core` and
  exercise bookkeeping methods, e.g.
  `RenderFeatureCompositorProjectFeatureTests.cpp`). This phase's new
  fixture is the FIRST of its kind — treat it carefully, it will be reused
  by every later phase and likely by BIG STEP 4.
- `Renderer::CreateRenderTexture()` (confirmed exact signature,
  `Renderer.h` ~line 262-264):
  ```cpp
  RenderTexture CreateRenderTexture(int width, int height, VkFormat format = VK_FORMAT_UNDEFINED,
      const char* debugName = nullptr, const char* depthDebugName = nullptr,
      bool allowStorageImageAccess = false, bool allowDepthSampledAccess = false) const;
  ```
  PHASE1 appends `bool createDepthCompanion = true` as an EIGHTH, trailing
  parameter — re-confirm this landed exactly as PHASE1's own completion
  report says before relying on the call shape below.
- `RenderGraphResourcePool::AcquireTexture()` (`RenderGraphResourcePool.cpp`
  ~line 28) calls `m_renderer->CreateRenderTexture(...)` — CONFIRMED this
  is the correct, established layer this new cache also calls through
  (never `GpuResourceFactory` directly) — mirrors the one real,
  already-proven render-graph-owned-resource creation path in this engine.
- `RenderGraphPersistentResourceCache.h`/`.cpp` already exist after PHASE2,
  containing `PersistentTextureCacheToken` and `IsStaleCacheEntry()` only —
  this phase ADDS the real class into the SAME two files.
- `RenderTexture`'s own doc comment (`RenderTexture.h` ~line 41-40, and its
  constructor comment ~line 50-57) already documents the exact hazard
  Section 6 of the source document warns about: `debugName` is stored BY
  REFERENCE (a bare, non-owned `const char*`), re-attached unchanged on
  every future `Resize()`. This is why the map's OWN key (a
  `std::unordered_map<std::string, Entry>` node, stable for the entry's
  entire lifetime) must be what gets passed as `debugName` — never the
  caller's own `owner`/`name` arguments, and never a local/temporary
  derived from them.

## Step 3: The Plan

### 3.1 — `tests/Fakes/HeadlessRenderGraphFixture.h` (new file)

```cpp
#pragma once

// editor-core-separation-27 campaign, PHASE4 - a small, reusable, real,
// headless (VK_EXT_headless_surface) GPU test fixture: a real Renderer +
// a real RenderGraph, with NO Core/Game/EditorLayer/SDL/ImGui involved at
// all. Built on top of HeadlessSurfaceProvider.h (editor-core-separation-1,
// PHASE18) - the SAME "GTEST_SKIP() if this machine's Vulkan driver/loader
// doesn't report VK_EXT_headless_surface" discipline every existing
// consumer of that fixture already follows. Unlike every existing
// consumer, this ALSO drives real RenderGraph::Execute() frames - the
// first fixture in this codebase to do so.
//
// Usage:
//   HeadlessRenderGraphFixture fixture;
//   if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
//   fixture.RunSynchronousFrame([&](rg::RenderGraphBuilder& b) -> std::vector<rg::TextureHandle> {
//       ... declare passes ...
//       return {};
//   });

#include "../../src/Renderer/Renderer.h"
#include "../../src/Renderer/RenderGraph/RenderGraph.h"
#include "HeadlessSurfaceProvider.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gte {

class HeadlessRenderGraphFixture {
public:
    HeadlessRenderGraphFixture()
    {
        try {
            m_renderer = std::make_unique<Renderer>(m_surfaceProvider);
            m_renderGraph = std::make_unique<rg::RenderGraph>(*m_renderer);
        } catch (const std::exception& e) {
            m_skipReason = std::string("HeadlessRenderGraphFixture: construction failed - this machine's "
                                        "Vulkan driver/loader most likely does not support "
                                        "VK_EXT_headless_surface (see HeadlessSurfaceProvider.h). Underlying "
                                        "error: ") + e.what();
        }
    }

    bool IsUsable() const noexcept { return m_renderer != nullptr && m_renderGraph != nullptr; }
    const std::string& SkipReason() const noexcept { return m_skipReason; }

    Renderer& GetRenderer() { return *m_renderer; }
    rg::RenderGraph& GetRenderGraph() { return *m_renderGraph; }

    // Drives exactly ONE real SynchronousImmediateReadback Execute() call -
    // mirrors Core::BuildFrame()'s own Begin/Execute/End sequence exactly
    // (Renderer::BeginOffscreenRenderGraphRecording() ->
    // RenderGraph::Execute() -> Renderer::EndOffscreenRenderGraphRecording()),
    // with no gameTarget/sceneTarget/Editor concept involved at all -
    // `build` may declare whatever passes/resources a test needs.
    void RunSynchronousFrame(
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&)>& build)
    {
        const VkCommandBuffer cmd = m_renderer->BeginOffscreenRenderGraphRecording();
        m_renderGraph->Execute(cmd, rg::ExecuteTimingMode::SynchronousImmediateReadback, build);
        m_renderer->EndOffscreenRenderGraphRecording();
    }

private:
    HeadlessSurfaceProvider m_surfaceProvider;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<rg::RenderGraph> m_renderGraph;
    std::string m_skipReason;
};

} // namespace gte
```

**Settled by direct code read — this fixture's shape above is already
complete, no further live confirmation is needed**: `Renderer::
BeginOffscreenRenderGraphRecording()`/`EndOffscreenRenderGraphRecording()`
genuinely work standalone, with no `gameTarget`/`RenderTexture`/`Core`/
`Game`/`EditorLayer` involved at all, and need no additional one-time setup
call before use. Confirmed by tracing the real call chain:
`Renderer::BeginOffscreenRenderGraphRecording()` (`Renderer.cpp`) forwards
straight to `FramePresenter::BeginOffscreenRecording()`
(`FramePresenter.cpp`), which only touches `m_offscreenCommandBuffer`
(`vkResetCommandBuffer`/`vkBeginCommandBuffer`) and
`m_frameSync.OffscreenFence()` (`vkWaitForFences`/`vkResetFences`) — both are
allocated UNCONDITIONALLY inside `FramePresenter`'s own constructor
(`CreateCommandObjects()` allocates `m_offscreenCommandBuffer`; the
`m_frameSync(device, kFramesInFlight, m_swapchain.ImageCount())`
member-initializer builds the fence pair), which itself runs unconditionally
inside `Renderer`'s own constructor — there is no lazy/deferred provisioning
tied to any render target of any kind. `EndOffscreenRenderGraphRecording()`
mirrors this exactly (`vkEndCommandBuffer`/`vkQueueSubmit`/`vkWaitForFences`
against those same two objects). The fixture sketch above needs no
adjustment.

### 3.2 — `RenderGraphPersistentResourceCache`'s real class (extends the EXISTING `RenderGraphPersistentResourceCache.h`/`.cpp` from PHASE2)

```cpp
// RenderGraphPersistentResourceCache.h - ADD, after the existing
// PersistentTextureCacheToken/IsStaleCacheEntry (PHASE2):

#include "RenderGraphTypes.h"
#include "../RenderTexture.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace gte {
class Renderer;
}

namespace gte::rg {

// One entry's full state. Declared with every field this WHOLE campaign
// eventually needs (PHASE5/6/8 progressively start reading/writing the
// fields THIS phase itself never touches) - see this class's own doc
// comment for why growing this struct's USE incrementally, without
// re-declaring it, is safe and intentional.
struct PersistentResourceCacheEntry {
    std::optional<RenderTexture> texture; // std::optional so Section 6.1's
        // two-phase recipe can insert an EMPTY placeholder first, then
        // construct the real RenderTexture referencing the map's own
        // now-stable key - never constructed inline in try_emplace() itself.
    TextureDesc desc; // the desc this entry was LAST successfully (re)built
        // with - PHASE6 compares a new request's desc.width/height against
        // this to detect a resize request.
    std::uint64_t epoch = 0; // PHASE4 - Section 4's fast-path safety net.
    std::uint64_t lastUsedFrame = 0; // PHASE5 - Section 8's eviction input.
    std::uint64_t lastRequestedFrame = 0; // PHASE5 - Section 5.3's double-
        // request guard input. Deliberately distinct from lastUsedFrame:
        // lastUsedFrame is what eviction reads (must survive across BOTH
        // regimes' calls this frame); lastRequestedFrame exists purely to
        // detect a SECOND request THIS SAME frame.
    bool hasLoggedDoubleRequest = false; // PHASE5 - "log exactly once ever
        // per identity", never once per frame forever.
    VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED; // PHASE8 -
        // Section 5.1's honest layout. PHASE4 never sets this to anything
        // other than its own default (UNDEFINED) - correct, since "nothing
        // to remember yet" IS the honest value for a brand-new entry.
};

class RenderGraphPersistentResourceCache {
public:
    explicit RenderGraphPersistentResourceCache(Renderer& renderer) noexcept
        : m_renderer(&renderer)
    {
    }

    // What a successful Resolve() call hands back - everything
    // GetOrCreatePersistentTexture() (PHASE8) needs to mint this frame's
    // TextureHandle. Never default-constructed/returned on failure - a
    // failed Resolve() is std::nullopt (a refusal already logged inside
    // this method) OR a thrown exception (a genuine RenderTexture
    // construction failure - Section 6.1 - propagated, never swallowed).
    struct ResolvedTexture {
        RenderTexture* texture = nullptr;
        VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const std::string* combinedKey = nullptr; // stable for this entry's whole lifetime.
        PersistentResourceCacheEntry* entry = nullptr; // opaque handle for a future token (PHASE5).
        std::uint64_t entryEpoch = 0;
    };

    // PHASE4 scope: construction/ownership/exception-safety (Section 6,
    // 6.1, 6.2) + "color only" enforcement (Section 7). Returns
    // std::nullopt for a validation refusal (empty owner/name,
    // desc.hasDepth == true) - each refusal is asserted in debug builds
    // and GTE_LOG_ERROR'd in release, mirroring this codebase's "assert in
    // debug, log-and-refuse in release, never crash" discipline. May THROW
    // std::runtime_error if the underlying RenderTexture construction
    // genuinely fails (Section 6.1) - never caught/swallowed here.
    std::optional<ResolvedTexture> Resolve(const char* owner, const char* name, const TextureDesc& desc);

private:
    Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, PersistentResourceCacheEntry> m_entries;
    std::uint64_t m_nextEntryEpoch = 1; // 0 is reserved, never a real epoch.
};

} // namespace gte::rg
```

`Resolve()`'s body (`RenderGraphPersistentResourceCache.cpp`), implementing
Section 6.1's mandatory two-phase recipe EXACTLY:

```cpp
std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::Resolve(const char* owner, const char* name, const TextureDesc& desc)
{
    assert(owner != nullptr && owner[0] != '\0'
        && "RenderGraphPersistentResourceCache::Resolve requires a non-empty owner");
    assert(name != nullptr && name[0] != '\0'
        && "RenderGraphPersistentResourceCache::Resolve requires a non-empty name");
    if (owner == nullptr || owner[0] == '\0' || name == nullptr || name[0] == '\0') {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with a null/empty owner or name - refusing.");
        return std::nullopt;
    }

    assert(!desc.hasDepth
        && "RenderGraphPersistentResourceCache::Resolve: v1 is color-only, desc.hasDepth must be false");
    if (desc.hasDepth) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with desc.hasDepth == true for \"" + std::string(owner) + "::" + name
                + "\" - v1 is color-only, refusing.");
        return std::nullopt;
    }

    const std::string key = std::string(owner) + "::" + name;

    // Step 1 - insert an EMPTY placeholder first (this is the ONLY step
    // that copies the caller's owner/name arguments; every subsequent
    // step reads the key back OUT of the map).
    auto [it, inserted] = m_entries.try_emplace(key);

    // Step 2/3 - if a real RenderTexture does not exist here yet,
    // construct it, referencing the MAP'S OWN, now-stable key
    // (it->first.c_str()) as debugName - NEVER `owner`/`name`/`key`
    // directly (see this phase's own Step 2 "Situation" citation of
    // RenderTexture's own by-reference debugName hazard).
    if (!it->second.texture.has_value()) {
        try {
            it->second.texture.emplace(m_renderer->CreateRenderTexture(
                static_cast<int>(desc.width), static_cast<int>(desc.height), desc.format,
                it->first.c_str(), /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/false,
                /*allowDepthSampledAccess=*/false, /*createDepthCompanion=*/false));
            it->second.epoch = m_nextEntryEpoch++;
            it->second.desc = desc;
        } catch (...) {
            // Exception safety (Section 6.1): only erase if THIS call
            // inserted the key - an earlier call's own already-failed
            // placeholder is left in place for the NEXT call to retry
            // against the same, already-stable key.
            if (inserted) {
                m_entries.erase(it);
            }
            throw;
        }
    }

    // Step 4 - hand back what GetOrCreatePersistentTexture() needs.
    ResolvedTexture result;
    result.texture = &it->second.texture.value();
    result.lastKnownLayout = it->second.lastKnownLayout;
    result.combinedKey = &it->first;
    result.entry = &it->second;
    result.entryEpoch = it->second.epoch;
    return result;
}
```

**Critical detail — do not get this wrong**: `CreateRenderTexture()`'s new
trailing `createDepthCompanion` parameter (PHASE1) must be passed
`false` here — this is the ONE call site in the whole engine that ever
passes `false` (confirmed nowhere else does, or will, per PHASE1's own
scope). Re-verify PHASE1's exact final parameter ORDER before writing this
call (the sketch above assumes `createDepthCompanion` is the eighth,
final parameter, immediately after `allowDepthSampledAccess` — confirm,
do not assume).

**Another concrete compile gap to close, confirmed by direct read**: unlike
PHASE2's own partial `RenderGraphPersistentResourceCache.cpp` (which only
ever defined `IsStaleCacheEntry()` and needed neither logging nor asserts),
`Resolve()`'s body above calls both `assert(...)` and `GTE_LOG_ERROR(...)` —
this phase must add `#include <cassert>` and
`#include "../../Core/Logging.h"` to `RenderGraphPersistentResourceCache.cpp`
(the exact same relative include path `RenderGraph.cpp`/
`RenderPassGroupRegistry.cpp` already use from this same
`src/Renderer/RenderGraph/` folder) — neither existed in that file before
this phase, and the code above will not compile without them.

### 3.3 — Wiring into the build

Both new/extended files (`RenderGraphPersistentResourceCache.h`/`.cpp`)
already exist in the PRODUCTION source list (the project-root
`CMakeLists.txt`) since PHASE2 — no change needed there. The new
`tests/Fakes/HeadlessRenderGraphFixture.h` needs NO `CMakeLists.txt` entry
either (a header-only fixture, included by whichever test `.cpp` uses it —
mirrors `HeadlessSurfaceProvider.h`'s own identical "header, never
separately compiled" shape). The new Tier-2 test `.cpp` file THIS phase adds
(see Step 4) DOES need one new line — but in `tests/CMakeLists.txt`'s own
TEST source list, a genuinely SEPARATE file from the project-root
`CMakeLists.txt` (confirmed by direct read of both — the root file only adds
`add_subdirectory(tests)`); see Step 4 below for the exact line to add it
beside.

## Step 4: Required Tests

New file: `tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`
(Tier 2 — real, headless GPU, `GTEST_SKIP()`-guarded exactly like every
`HeadlessSurfaceProvider` consumer). **Correction, confirmed by direct read of
both files**: the explicit, file-by-file test-source list does NOT live in
the project-root `CMakeLists.txt` — that file only has a plain
`add_subdirectory(tests)` line (plus its own, separate PRODUCTION source
list, unaffected by this phase). The actual test-source list is in
`tests/CMakeLists.txt`. Add one new line there, in the `Renderer/RenderGraph/`
section, immediately after the existing
`Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp` line (currently line
2313) and before `Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`
(currently line 2314) — re-confirm these exact line numbers via
`search_in_dir` before editing, since earlier phases in this campaign may
have already shifted them.

Required cases (this phase's own scope only — do not attempt PHASE5/6/8's
own acceptance criteria here):

1. **Basic construct-and-reuse**: `cache.Resolve("Test", "Test", desc)`
   called via `fixture.RunSynchronousFrame(...)`, three separate times
   (three separate `RunSynchronousFrame()` calls = three real, distinct
   `Execute()` frames). Confirm `result->texture` is the SAME pointer, and
   `result->texture->Image()` (a real `VkImage` handle) is IDENTICAL, all
   three times — the single most important proof in this phase, directly
   answering the source document's own first acceptance-criteria
   checkbox.
2. **Two owners, same name, same desc, never collide**: `cache.Resolve("OwnerA", "History", desc)`
   and `cache.Resolve("OwnerB", "History", desc)` (IDENTICAL `name` AND
   `desc`) — confirm two DISTINCT `RenderTexture*`/`VkImage` values. This
   is the concrete regression test for Section 6.2 population (a)'s
   collision-safety guarantee — the single most important NEW test the
   source document itself calls out (Section 12).
3. **Exception safety**: this is genuinely hard to force a REAL
   `vmaCreateImage()` failure for in an automated test (no reliable way to
   starve VRAM on demand). Instead, confirm the STRUCTURAL guarantee
   directly: after a hand-constructed `RenderGraphPersistentResourceCache`
   whose `Resolve()` is called with a deliberately absurd `desc.width`/
   `desc.height` that IS expected to throw from `vmaCreateImage()` on real
   hardware, confirm (a) the call throws (does not silently return a broken
   handle), and (b) an IMMEDIATE retry with a SANE desc for the SAME
   `(owner, name)` succeeds cleanly afterward (proves the failed placeholder
   did not permanently poison that key). **Do NOT use `0xFFFFFFFF`
   (`UINT32_MAX`) for this** — confirmed by direct read of
   `RenderTexture::Create()` (`RenderTexture.cpp`): `Resolve()` passes
   `static_cast<int>(desc.width)`/`static_cast<int>(desc.height)` into
   `Renderer::CreateRenderTexture()`, and casting `0xFFFFFFFFu` to `int`
   (this project builds as C++20, so this cast is well-defined, not UB)
   yields `-1` — `RenderTexture::Create()`'s own first two lines then CLAMP
   any non-positive width/height to `1` (`width > 0 ? width : 1`), so this
   "absurd" value silently, successfully creates an ordinary 1x1 texture
   instead of throwing anything at all, making this test a GUARANTEED false
   negative, not a machine-dependent maybe. Use a genuinely huge but
   POSITIVE value instead (e.g. `desc.width = desc.height = 100000` — stays
   positive after the `int` cast, and a 100000x100000 image at 4 bytes/pixel
   is ~40 GB, comfortably past any real GPU's VRAM) so the call actually
   reaches `vmaCreateImage()` with a request the driver has a real chance of
   rejecting. If this still does not reliably throw on the test machine's
   actual driver/GPU, document that honestly in the completion report (do
   not fabricate a false-positive pass) and flag it for PHASE9's own final
   acceptance pass to re-attempt/re-assess.
4. **`desc.hasDepth == true` refusal AND empty owner/name refusal — BOTH
   must be gtest Death Tests, not a plain `std::nullopt` check.** Confirmed
   by direct read of this phase's own `Resolve()` body above: each of these
   two refusals is guarded by a plain `assert(...)` immediately BEFORE its
   `GTE_LOG_ERROR(...); return std::nullopt;` fallback — exactly mirroring
   this codebase's own established, ALREADY-SHIPPED `#ifndef NDEBUG` /
   `TEST(XxxDeathTest, ...)` / `EXPECT_DEATH(...)` convention
   (`RenderGraphBuilderTests.cpp`'s `RenderGraphBuilderDeathTest.
   WriteColorAttachmentExceedingCapAssertsInDebug`,
   `RenderGraphBarrierPlannerTests.cpp`'s `RenderGraphBarrierPlannerDeathTest`
   suite). This project's root `CMakeLists.txt` never forces
   `CMAKE_BUILD_TYPE` to `Release` (confirmed — no such default anywhere in
   the build), so this repo's normal, everyday `cmake --build build` +
   `ctest` cycle compiles WITHOUT `NDEBUG` defined, meaning `assert()` stays
   LIVE — calling `cache.Resolve(nullptr, "X", desc)` (or any of the other
   invalid-input variants) in that normal build config does NOT gracefully
   return `std::nullopt`; it ABORTS THE WHOLE TEST PROCESS. Structure both as
   `#ifndef NDEBUG`-guarded `TEST(RenderGraphPersistentResourceCacheDeathTest, ...)`
   cases using `EXPECT_DEATH(statement, "")`, one per distinct invalid input
   (`desc.hasDepth == true`; `owner == nullptr`; `name == nullptr`;
   `owner == ""`; `name == ""`) — each `statement` must construct its OWN
   fresh `HeadlessRenderGraphFixture`/cache inside the `EXPECT_DEATH` lambda
   (gtest's default "threadsafe" death-test style re-executes from a fresh
   process, so nothing from the enclosing test body's own already-constructed
   fixture carries over into it), and `GTEST_SKIP()` inside that same lambda
   too if the machine turns out not to support headless surfaces there. The
   release-build (`NDEBUG` defined), log-and-refuse `std::nullopt`-returning
   half of this behavior is, honestly, NOT exercised by this project's normal
   `ctest` run at all — exactly the same accepted, disclosed scope limit this
   codebase's own pre-existing `WriteColorAttachmentExceedingCapAssertsInDebug`
   already lives with; state this plainly in the completion report rather
   than claiming both branches were verified.
5. **No depth companion allocated**: after `cache.Resolve("Test",
   "NoDepthCheck", desc)`, use `Renderer::GetMemoryResources()` (or
   `GpuMemoryTracker::Totals`) to confirm exactly ONE new tracked
   allocation appeared for this call (the color image) — the concrete
   regression test for Section 7's `createDepthCompanion` requirement.
   Compare a resource count/total BEFORE and AFTER the `Resolve()` call to
   make this robust against whatever else the fixture's own construction
   may have already allocated.
6. **A dozen distinct identities, container growth**: `Resolve()` at least
   12 distinct `(owner, name)` pairs in one session (enough to force
   `std::unordered_map` internal rehashing); confirm every EARLIER
   `ResolvedTexture::texture` pointer obtained from an earlier `Resolve()`
   call is STILL valid and still points at a correctly-named, correctly-
   sized `RenderTexture` afterward (read back `Image()`/`Extent()` and
   compare against what was recorded right after that entry was first
   created) — the concrete regression test for TR4's node-stability
   requirement.

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. This phase's own new
files: `tests/Fakes/HeadlessRenderGraphFixture.h` and
`tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp` —
both explicitly permitted by PHASE0's Rule 10. Strongly consider a
`dispatch_sub_agent` double-check of this phase's own diff before writing
`PHASE4_COMPLETION_REPORT.md` (per Rule 4) — this is the campaign's own
flagged highest-risk phase. Incremental build + `ctest -R
RenderGraphPersistentResourceCacheTest` (this will `GTEST_SKIP()` cleanly
on a machine without `VK_EXT_headless_surface` — confirm the skip message
is legible, and if it DOES skip on this development machine, fall back to
a manual live-Editor + `GET /get_logs` check for this phase's own basic
sanity, and say so plainly in the completion report). End with
`PHASE4_COMPLETION_REPORT.md` + git commit.
