#pragma once

// network-impl-6 campaign, Phase 1
// (task_manager/network-impl-6/PHASE1_DEBUG_VOLUME_TEXTURE_REGISTRY.md) - the
// exact volume-texture counterpart of RenderGraphDebugTextureRegistry.h
// (network-impl-4 campaign) - a pure, Vulkan-header-only (no live
// VkDevice/VmaAllocator anywhere in this file) name -> physical-volume-
// texture-snapshot table, deliberately its own tiny, Tier-1-testable module,
// following that file's own precedent exactly. See RenderGraph.h for how
// this is actually kept up to date every frame (Phase 2 of this campaign)
// and Application.cpp for how a requested texture name that resolves to a
// volume (rather than a 2D texture) is served (Phase 4).
//
// Every volume texture RenderGraphBuilder::ImportVolumeTexture() ever
// declares, in EITHER ExecuteTimingMode regime, is automatically eligible to
// appear here - see PHASE0_MASTER_STRATEGY.md's Locked Design Decision 1.
// This class itself has no opinion about HOW it gets populated - it is a
// dumb, passive store; RenderGraph (Phase 2) is what actually calls
// Upsert() every ExecuteCompiledGraph() call.

#include "RenderGraphBarrierPlanner.h"
#include "../VolumeTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared here to avoid a circular include, mirroring RenderGraphDebugTextureRegistry.h's own identical forward-declare; both enumerators are re-declared nowhere else.

// One volume texture's full current knowledge - the VolumeTarget
// (RenderGraphDebugTextureRegistry.h's own DebugTextureSnapshot) counterpart,
// but with no depth half at all: a VolumeTexture has no depth-companion
// concept whatsoever (see VolumeTarget.h's own doc comment), so there is
// only ONE ResourceState here, never a color/depth pair. Deliberately a
// plain, copyable value (no pointers/handles owned) - safe to hand back out
// of the registry by value with no lifetime concerns, exactly like
// DebugTextureSnapshot itself (a VkImage/VkImageView handle copied out of
// this registry is only meaningful for as long as the real underlying
// resource is still alive).
struct DebugVolumeTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    VolumeTarget target; // image/imageView/extent (VkExtent3D: width/height/depth)/format.

    // The layout/stage/access this registry LAST believes the volume image
    // is actually in. Not split into a color/depth pair - see this struct's
    // own header comment above.
    ResourceState state;

    // The value of the registry's own monotonically increasing frame
    // counter (see RenderGraph::Upsert() call site, Phase 2) at the moment
    // this entry was last written by Upsert() - NOT touched by
    // ApplyStateOverride() (see below), which corrects state only, never
    // "freshness" (a graph-external manual barrier is not a new capture of
    // the volume's CONTENTS, just a bookkeeping fix for its LAYOUT - see
    // PHASE0_MASTER_STRATEGY.md's own Step 2 analysis, mirroring
    // RenderGraphDebugTextureRegistry.h's identical reasoning).
    std::uint64_t lastUpdatedFrameCounter = 0;
};

// The registry itself - a flat vector, scanned linearly (see
// RenderGraphDebugTextureRegistry.h's own header comment for why a hash map
// is unwarranted at this engine's scale - the exact same reasoning applies
// here, likely at an even smaller scale given there are far fewer volume
// textures than 2D ones). Every method is a plain, synchronous,
// single-threaded call - this class has NO locking of its own; it is only
// ever touched from the main thread (RenderGraph::ExecuteCompiledGraph()/
// Application::Run()), exactly like every other RenderGraph-adjacent class
// in this engine (see AGENTS.md, "Networking" - a route handler NEVER
// touches this directly, only through FrameCaptureBridge).
class RenderGraphDebugVolumeTextureRegistry {
public:
    RenderGraphDebugVolumeTextureRegistry() = default;

    // Inserts a brand-new entry for `name`, or overwrites an existing one in
    // place (by value - every field, including lastUpdatedFrameCounter, is
    // fully replaced). `name` is copied into the stored entry, for the exact
    // same "keep this class's own correctness independent of an upstream
    // string-literal-lifetime convention being upheld perfectly forever"
    // reasoning RenderGraphDebugTextureRegistry::Upsert()'s own doc comment
    // already explains in full - read that one for the complete rationale,
    // it applies here verbatim.
    void Upsert(const DebugVolumeTextureSnapshot& snapshot);

    // Overwrites JUST the ResourceState of the entry named `name` (a safe
    // no-op if no such entry exists yet). Deliberately does NOT touch
    // `lastUpdatedFrameCounter` - a state correction is not a fresh capture
    // of contents. Named without a "Color"/"Depth" prefix (unlike
    // RenderGraphDebugTextureRegistry::ApplyColorStateOverride(), which only
    // needs one to disambiguate from a hypothetical depth override that
    // doesn't exist for volumes) since there is exactly one state to
    // override here.
    void ApplyStateOverride(const std::string& name, const ResourceState& newState);

    // Returns a copy of the entry named `name`, or std::nullopt if this
    // registry has never seen that name at all this session. `name`
    // comparison is a plain std::string == (safe/correct against an
    // HTTP-supplied string, same reasoning as Upsert()'s own doc comment).
    std::optional<DebugVolumeTextureSnapshot> FindByName(const std::string& name) const;

    // Every currently-known name, in FIRST-SEEN order (stable, so a future
    // GET /list_textures caller sees a predictable, non-shuffling order
    // across repeated calls within one session). Returns copies
    // (DebugVolumeTextureSnapshot, not just names) so a caller building a
    // response body needs no second lookup per name.
    std::vector<DebugVolumeTextureSnapshot> ListAll() const;

private:
    std::vector<DebugVolumeTextureSnapshot> m_entries;
};

} // namespace gte::rg
