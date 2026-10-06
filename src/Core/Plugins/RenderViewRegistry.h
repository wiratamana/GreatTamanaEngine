#pragma once

#include "../../Renderer/RenderGraph/RenderPipeline.h" // rg::RenderViewId
#include "../../Renderer/RenderTexture.h"

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <volk.h>

namespace gte {

class Renderer;

// better-render-pass-3 campaign, BLOCK 2 (Arbitrary Render Views). Owns
// every DYNAMICALLY created view's persistent RenderTexture, keyed by
// NAME. "Create once, reuse every frame, live for the process's whole
// lifetime" - the same contract RenderGraphPersistentResourceCache already
// proves works in this codebase (see that class's own doc comments), just
// without that class's resize/eviction/token-fast-path machinery, which
// this registry deliberately does not need (PHASE0_MASTER_STRATEGY.md,
// Locked Design Decisions 2/3: no resize-on-demand, no teardown API - a
// dynamic view's resolution and lifetime are both fixed at creation).
struct RenderViewDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasColor = true;   // false => createColorImage = false (PHASE1) -
                             // a genuinely colorless RenderTexture, not a
                             // bookkeeping-only flag.
    bool hasDepth = true;   // false => createDepthCompanion = false.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED; // UNDEFINED == match
                             // Renderer::ColorFormat(). MEANINGLESS when
                             // hasColor is false - see
                             // DescsMatchForSameName() below, which
                             // deliberately excludes this field from
                             // comparison in that case.

    // Opts the depth image into VK_IMAGE_USAGE_SAMPLED_BIT + a real
    // sampler, so a later pass can read this view's depth back as a
    // texture. Meaningless when hasDepth is false.
    bool allowDepthSampledAccess = false;
};

// Equality used ONLY to detect "same name, different desc" misuse in
// CreateOrGetView() below - deliberately NOT a defaulted `operator==`,
// because colorFormat must be ignored whenever EITHER side has hasColor
// == false (a depth-only view requested twice with two different,
// both-irrelevant colorFormat values must never be flagged as a caller
// bug).
bool DescsMatchForSameName(const RenderViewDesc& a, const RenderViewDesc& b) noexcept;

// Single source of truth for every view name this engine itself already
// owns - CreateOrGetView() checks a caller-supplied name against this
// before doing anything else. A plain, linearly scanned array is correct
// here (this list is expected to stay at single digits forever).
inline constexpr const char* kReservedViewNames[] = { "Game", "Scene" };
bool IsReservedViewName(const char* name) noexcept;

class RenderViewRegistry {
public:
    explicit RenderViewRegistry(Renderer& renderer) noexcept;

    // Idempotent by name - see this header's own class-level doc comment
    // and PHASE2's own strategy document for the full refusal-rules list
    // (reserved name / null-or-empty name / mismatched desc on an
    // existing name). Returns RenderViewId::Shared() (and inserts nothing)
    // on every refusal path. May throw std::runtime_error if the
    // underlying RenderTexture construction genuinely fails (e.g.
    // out-of-device-memory) - never caught/swallowed here; the registry
    // itself remains safely retryable afterward for the same name (see
    // .cpp for the exact two-phase recipe).
    rg::RenderViewId CreateOrGetView(const char* name, const RenderViewDesc& desc);

    // nullptr if `view` was never created via CreateOrGetView() above
    // (including every refusal case, which never actually insert
    // anything).
    RenderTexture* FindViewTarget(rg::RenderViewId view) const noexcept;

private:
    Renderer* m_renderer;

    // Set once at construction, on the engine's own main thread.
    // CreateOrGetView() and FindViewTarget() both assert every call still
    // comes from this thread.
    const std::thread::id m_mainThreadId = std::this_thread::get_id();

    struct Entry {
        rg::RenderViewId id;
        RenderViewDesc desc;
        bool hasLoggedDescMismatch = false;
        std::optional<RenderTexture> target;
    };

    // Keyed by NAME (not by RenderViewId) - the name is the caller-facing
    // identity; the RenderViewId is derived FROM the name
    // (rg::RenderViewId::Named(name)) and is recoverable from it at any
    // time, so a second, reverse (RenderViewId -> name) index is never
    // needed. FindViewTarget() instead does a small linear scan over this
    // SAME map (see .cpp's own doc comment for why this is an accepted,
    // documented v1 simplicity trade-off, not an oversight).
    std::unordered_map<std::string, Entry> m_views;
};

} // namespace gte
