#pragma once

#include <cstdint>

// editor-core-separation-27 campaign (BIG STEP 3 of 4 -
// BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt).
// PHASE2 of this campaign creates this file containing ONLY
// PersistentTextureCacheToken/IsStaleCacheEntry() - PHASE4 ADDS the real
// RenderGraphPersistentResourceCache class into this SAME file (never a
// second header), plus PHASE5/PHASE6 extend it further. See
// task_manager/editor-core-separation-27/PHASE0_MASTER_STRATEGY.md.

namespace gte::rg {

// PHASE6 - forward-declared here (never defined here) to avoid a circular
// include: ExecuteTimingMode is declared, in full, inside RenderGraph.h,
// and RenderGraph.h (PHASE7) is planned to include THIS header, so a
// reverse include here would be a genuine header-to-header cycle - mirrors
// RenderGraphDebugTextureRegistry.h/RenderGraphDebugVolumeTextureRegistry.h's
// own identical precedent exactly. A forward declaration with a fixed
// underlying type is a complete-enough type for a function
// parameter/member declaration - it is NOT enough for a .cpp body that
// needs the actual enumerator VALUES, which is why
// RenderGraphPersistentResourceCache.cpp (only the .cpp, never this
// header) additionally includes "RenderGraph.h" directly.
enum class ExecuteTimingMode : std::uint8_t;

// Opaque outside RenderGraphPersistentResourceCache.cpp - see PHASE4.
struct PersistentResourceCacheEntry;

// Source document Section 4 - a small, POD, caller-owned token. Default-
// constructed as "never resolved yet" (entry == nullptr). Safe to keep,
// copy, and reuse across any number of frames.
struct PersistentTextureCacheToken {
    PersistentResourceCacheEntry* entry = nullptr;
    std::uint64_t entryEpoch = 0;
};

// Source document Section 8 - pure, Tier-1-testable. True iff `lastUsedFrame`
// is more than `staleThresholdFrames` frames behind `currentFrame`.
bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept;

} // namespace gte::rg

// PHASE4 (editor-core-separation-27 campaign) - the real
// RenderGraphPersistentResourceCache class, added into this SAME file per
// this phase's own .md (Step 2's "this phase EXTENDS that same file, never
// creates a second one"). Needs RenderGraphTypes.h (TextureDesc) and
// RenderTexture.h (the actual GPU resource an entry owns).
#include "RenderGraphTypes.h"
#include "../RenderTexture.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
        // PHASE6 - a resize atomically resets this back to UNDEFINED too
        // (see FlushPendingResizes() below) - a freshly recreated VkImage
        // really is VK_IMAGE_LAYOUT_UNDEFINED again (RenderTexture::Create()'s
        // own imageInfo.initialLayout).
    const std::string* ownKeyForDebugAssert = nullptr; // PHASE5 - Section 4's
        // debug-only misuse guard input. Points at this entry's OWN key
        // inside m_entries (stable forever - TR4, std::unordered_map node
        // stability) - never re-pointed after first construction.
};

// PHASE5 - Locked Decision 4 (PHASE0_MASTER_STRATEGY.md) - default
// stale-eviction threshold: an entry unused for this many real frames is
// evicted by BeginFrame() (Section 8). NAMESPACE-SCOPE, never a class
// `static constexpr` member - mirrors RenderGraphNameSlotTable.h's own
// `kNoNameSlot` precedent exactly, so a DIFFERENT class (PHASE7's
// RenderGraph.cpp) can reference it unqualified merely by including this
// header.
inline constexpr std::uint64_t kPersistentResourceStaleThresholdFrames = 300;

class RenderGraphPersistentResourceCache {
public:
    // PHASE6 - now ALSO caches this Renderer's own raw VkDevice (via
    // Renderer::GetVulkanContextInfo().device, a plain, non-throwing getter
    // over already-live state) for FlushPendingResizes()'s own direct
    // vkDeviceWaitIdle(VkDevice) call below - mirrors
    // RenderGraphTimestampPool's own identical construction precedent, and
    // RenderFeatureCompositor::EnsureTextureSized()'s own precedent for WHY
    // the raw Vulkan call is used directly instead of
    // Renderer::WaitForGpuIdle() (that wrapper's own doc comment forbids any
    // per-frame-path caller - FlushPendingResizes() runs on exactly such a
    // path, PHASE8). Defined out-of-line (.cpp) now, since calling a
    // Renderer member function needs Renderer's full definition, which this
    // header deliberately still only forward-declares.
    explicit RenderGraphPersistentResourceCache(Renderer& renderer) noexcept;

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
    // 6.1, 6.2) + "color only" enforcement (Section 7). PHASE5 scope: age
    // stamping + the same-real-frame double-request refusal (Section 5.3).
    // PHASE6 scope (this phase): on a genuinely PRE-EXISTING entry, a
    // DIFFERENT desc.format is always a caller bug (refused regardless of
    // regime); a DIFFERENT desc.width/height QUEUES a resize (Section
    // 5.5/FR4/FR8) instead of resizing inline - THIS call still returns the
    // entry's CURRENT (soon-to-be-stale but valid) texture/extent - unless
    // `timingMode` is `ExecuteTimingMode::PipelinedDeferredReadback`, in
    // which case the resize portion is refused outright (logged, extent
    // left unchanged, nothing queued) since that regime must never trigger
    // FlushPendingResizes()'s own vkDeviceWaitIdle(). Returns std::nullopt
    // for a validation refusal (empty owner/name, desc.hasDepth == true, a
    // same-frame double request, or a format change on an existing entry) -
    // each refusal is asserted in debug builds (where applicable) and
    // GTE_LOG_ERROR'd in release, mirroring this codebase's "assert in
    // debug, log-and-refuse in release, never crash" discipline. May THROW
    // std::runtime_error if the underlying RenderTexture construction
    // genuinely fails (Section 6.1) - never caught/swallowed here.
    std::optional<ResolvedTexture> Resolve(const char* owner, const char* name, const TextureDesc& desc,
        std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    // Section 8's eviction sweep - the ONE method RenderGraph::
    // BeginPersistentResourceFrame() (PHASE7) calls, from a DIFFERENT class
    // (cannot be private). Also stamps this frame's own age bookkeeping via
    // Resolve() above - see that method for the double-request guard.
    void BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames);

    // Section 9 - external consumers (a future Editor "Render Graph" panel/
    // HTTP handler) query how many frames remain before an identity is
    // evicted. PRIMARY overload takes the already-combined identity string
    // (the SAME string a persistent-cache-backed DebugTextureSnapshot::name
    // already exposes); the convenience overload builds the same combined
    // key internally and forwards.
    std::optional<std::uint64_t> FramesUntilEviction(
        const std::string& combinedIdentity, std::uint64_t currentFrame) const;
    std::optional<std::uint64_t> FramesUntilEviction(
        const char* owner, const char* name, std::uint64_t currentFrame) const;

#ifndef NDEBUG
    // Section 4's debug-only misuse guard - infrastructure only in PHASE5;
    // PHASE8's RenderGraphBuilder token overload is the real caller. Guarded
    // so a release build never even declares/compiles this.
    bool DebugTokenIdentityMatches(const PersistentResourceCacheEntry* entry, const char* owner, const char* name) const;
#endif

    // Token liveness check - deliberately NOT #ifndef NDEBUG-guarded (the
    // real functional fast/slow-path gate PHASE8 needs in release builds
    // too, not merely a debug-only assert helper). Deliberately does NOT
    // dereference `entry` through m_entries in any way that requires it to
    // still be a live node - see this campaign's PHASE5 .md "Situation"
    // section for the accepted, narrow risk this implies, and the required
    // live test proving it in practice on this project's actual toolchain.
    bool IsTokenLive(const PersistentResourceCacheEntry* entry, std::uint64_t entryEpoch) const noexcept
    {
        return entry != nullptr && entry->epoch == entryEpoch && entry->epoch != 0;
    }

    // PHASE8 (editor-core-separation-27 campaign,
    // PHASE8_BUILDER_GETORCREATEPERSISTENTTEXTURE_AND_HONEST_LAYOUT_WIRING.md)
    // - the token-based fast path's own resolve call: the caller
    // (RenderGraphBuilder::GetOrCreatePersistentTexture()'s token overload)
    // must already have confirmed IsTokenLive(token.entry, token.entryEpoch)
    // before reaching here - never call this against a possibly-stale
    // entry pointer. Shares ALL of Resolve()'s own validation/age-tracking/
    // resize logic via the private ResolveAgainstEntry() helper below - see
    // that method's own doc comment.
    std::optional<ResolvedTexture> ResolveFast(
        PersistentResourceCacheEntry* entry, const TextureDesc& desc, std::uint64_t currentFrame,
        ExecuteTimingMode timingMode);

    // PHASE8 - Section 5.1: records the FINAL VkImageLayout a persistent
    // texture's color image was actually left in at the end of the real
    // frame it was used - called once per persistent texture per real
    // frame, from RenderGraph::ExecuteCompiledGraph()'s own tail hook,
    // using the SAME combined "<owner>::<name>" key already flowing
    // through this whole class (TextureSlot::name / ImportTexture()'s own
    // `name` argument for a persistent-cache-backed handle). A safe no-op
    // if `key` is null or does not (or no longer) match a live entry -
    // "should not happen in steady state" defensive, mirrors
    // RegisterDebugTextureSnapshots()'s own `if (!tex.resolved) continue;`
    // guard in spirit.
    void RecordFinalLayout(const char* key, VkImageLayout layout);

    // PHASE6 - Section 5.5/FR8: flushes every QUEUED resize (see Resolve()
    // above) behind EXACTLY ONE combined vkDeviceWaitIdle(), no matter how
    // many entries need resizing this real frame - a no-op (no stall of any
    // kind) when nothing is pending. NOT wired into RenderGraph::
    // ExecuteCompiledGraph() yet - that call site is PHASE8's own job,
    // alongside RecordFinalLayout()'s loop, same cadence, same
    // !isPipelined gating.
    void FlushPendingResizes();

private:
    // PHASE6 - one entry's queued-but-not-yet-applied resize request.
    struct PendingResize {
        PersistentResourceCacheEntry* entry = nullptr;
        std::uint32_t newWidth = 0;
        std::uint32_t newHeight = 0;
    };

    // Last request THIS frame wins for the SAME entry - matches "single
    // builder, single frame" reasoning used elsewhere in this engine.
    void QueueResize(PersistentResourceCacheEntry* entry, std::uint32_t newWidth, std::uint32_t newHeight);

    // PHASE8 - the shared tail both Resolve() (right after its own
    // try_emplace/construction step has guaranteed `entry.texture.has_value()`
    // and `entry.ownKeyForDebugAssert != nullptr`) and ResolveFast() above
    // call: the same-real-frame double-request refusal (Section 5.3),
    // age-stamping, the format-change/resize-request handling (PHASE5/
    // PHASE6), and the final ResolvedTexture construction. Extracting this
    // ONE shared implementation guarantees the token-based fast path can
    // never accidentally skip a check the slow path enforces. For a
    // brand-new entry, Resolve()'s own construction step already set
    // `entry.desc = desc` before calling this, so the format/width/height
    // comparison below is always a structural no-op the very first time any
    // identity is ever resolved - equivalent to the OLD, pre-PHASE8 code's
    // explicit `if (!inserted) { ... }` guard, without needing to thread
    // `inserted` through this shared helper at all.
    std::optional<ResolvedTexture> ResolveAgainstEntry(PersistentResourceCacheEntry& entry, const TextureDesc& desc,
        std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE; // PHASE6 - cached once, at construction, from
        // Renderer::GetVulkanContextInfo().device - see FlushPendingResizes() below.
    std::unordered_map<std::string, PersistentResourceCacheEntry> m_entries;
    std::uint64_t m_nextEntryEpoch = 1; // 0 is reserved, never a real epoch.
    std::vector<PendingResize> m_pendingResizes; // PHASE6 - Section 5.5's batched-resize queue,
        // flushed by FlushPendingResizes() (see above).
};

} // namespace gte::rg
