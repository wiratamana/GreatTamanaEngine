#pragma once

// network-impl-4 campaign, Phase 1
// (task_manager/network-impl-4/PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md) -
// the pure, Vulkan-header-only (no live VkDevice/VmaAllocator anywhere in
// this file) name -> physical-texture-snapshot table behind GET
// /get_texture and GET /list_textures. Deliberately its own tiny,
// Tier-1-testable module (mirrors RenderGraphNameSlotTable.h's own "small,
// persistent, name-keyed table, extracted into its own header specifically
// so it's directly testable with hand-fabricated inputs" precedent) - see
// RenderGraph.h for how this is actually kept up to date every frame
// (Phase 2) and Application.cpp for the one, narrow, per-call-site
// "correction" hook a graph-external manual barrier needs (Phase 3).
//
// Every texture RenderGraphBuilder::CreateTexture()/ImportTexture() ever
// declares, in EITHER ExecuteTimingMode regime, is automatically eligible
// to appear here - see PHASE0_MASTER_STRATEGY.md's Locked Design Decision 6.
// This class itself has no opinion about HOW it gets populated - it is a
// dumb, passive store; RenderGraph (Phase 2) is what actually calls
// Upsert() every ExecuteCompiledGraph() call.

#include "RenderGraphBarrierPlanner.h"
#include "../RenderTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared here to avoid a circular include; both enumerators are re-declared nowhere else. Confirmed safe by a standalone compile check - see PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md's Step 3.1 closing note.

// One texture's full current knowledge - color required, depth optional.
// Deliberately a plain, copyable value (no pointers/handles owned) - safe
// to hand back out of the registry by value with no lifetime concerns
// (mirrors RenderGraphSnapshot.h's own "every string/value copied, never
// referenced" rule, applied here to Vulkan handles instead of strings -
// note a VkImage/VkImageView handle copied out of this registry is only
// meaningful for as long as the real underlying resource is still alive,
// exactly like any other raw Vulkan handle this engine already threads
// around by value, e.g. RenderTarget itself).
struct DebugTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    RenderTarget target; // target.image/imageView/extent/format is the COLOR half; target.depthImage/depthImageView/depthFormat is the optional depth half (see RenderTarget.h).
    bool hasDepth = false;

    ResourceState colorState; // layout/stage/access this registry LAST believes the color image is actually in.
    ResourceState depthState; // meaningful only when hasDepth is true.

    // The value of the registry's own monotonically increasing frame
    // counter (see RenderGraph::Upsert() call site, Phase 2) at the moment
    // this entry was last written by Upsert() - NOT touched by
    // ApplyColorStateOverride() (see below), which corrects state only,
    // never "freshness" (a graph-external manual barrier is not a new
    // capture of the texture's CONTENTS, just a bookkeeping fix for its
    // LAYOUT - see PHASE0_MASTER_STRATEGY.md's own Step 2 analysis).
    std::uint64_t lastUpdatedFrameCounter = 0;
};

// The registry itself - a flat vector, scanned linearly (see this file's
// own header comment for why a hash map is unwarranted at this engine's
// scale). Every method is a plain, synchronous, single-threaded call -
// this class has NO locking of its own; it is only ever touched from the
// main thread (RenderGraph::ExecuteCompiledGraph()/Application::Run()),
// exactly like every other RenderGraph-adjacent class in this engine (see
// AGENTS.md, "Networking" - a route handler NEVER touches this directly,
// only through FrameCaptureBridge, Phase 4).
class RenderGraphDebugTextureRegistry {
public:
    RenderGraphDebugTextureRegistry() = default;

    // Inserts a brand-new entry for `name`, or overwrites an existing one
    // in place (by value - every field, including lastUpdatedFrameCounter,
    // is fully replaced). `name` is copied into the stored entry (unlike
    // RenderGraphNameSlotTable's own by-pointer/string-literal convention -
    // see this method's own doc comment below for why a copy is required
    // here, not merely allowed).
    //
    // IMPORTANT naming-lifetime note: RenderGraphBuilder::CreateTexture()/
    // ImportTexture()'s own `name` parameter is a `const char*` that must be
    // a string literal / static-storage-duration pointer (see
    // RenderGraphBuilder.h's own class comment) - but THIS registry must
    // remain valid and correct across MANY frames, and a future caller of
    // Upsert() (Phase 2) only ever has that SAME static-storage `const
    // char*` available anyway, so storing a std::string copy here (rather
    // than keeping the raw pointer, which would ALSO have been safe given
    // the static-storage-duration rule) is a deliberate, extra-safe choice:
    // it keeps this class's own correctness independent of that upstream
    // convention being upheld perfectly forever, and is what makes
    // ListAll()/FindByName() usable with an arbitrary caller-supplied
    // std::string (e.g. straight out of an HTTP query parameter, Phase 4/5)
    // via plain std::string comparison - never a dangling/lifetime concern
    // either way.
    void Upsert(const DebugTextureSnapshot& snapshot);

    // Overwrites JUST the color ResourceState of the entry named `name` (a
    // safe no-op if no such entry exists yet) - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 7. Deliberately
    // does NOT touch `lastUpdatedFrameCounter` - a state correction is not
    // a fresh capture of contents.
    void ApplyColorStateOverride(const std::string& name, const ResourceState& newColorState);

    // Returns a copy of the entry named `name`, or std::nullopt if this
    // registry has never seen that name at all this session. `name`
    // comparison is a plain std::string == (see Upsert()'s own doc comment
    // for why this is safe/correct against an HTTP-supplied string).
    std::optional<DebugTextureSnapshot> FindByName(const std::string& name) const;

    // Every currently-known name, in FIRST-SEEN order (stable, so
    // GET /list_textures - Phase 5 - returns a predictable, non-shuffling
    // order across repeated calls within one session) - the primitive
    // behind that endpoint. Returns copies (DebugTextureSnapshot, not just
    // names) so a caller building /list_textures's response body needs no
    // second lookup per name.
    std::vector<DebugTextureSnapshot> ListAll() const;

private:
    std::vector<DebugTextureSnapshot> m_entries;
};

} // namespace gte::rg
