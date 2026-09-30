#include "RenderGraphSnapshot.h"

namespace gte::rg {

// Render Pass campaign (task_manager/render-pass-1), PHASE2 - see this
// function's own doc comment in RenderGraphSnapshot.h for the full
// contract/rule.
PassGpuStats CombinePassGpuStats(const std::vector<PassGpuStats>& stats)
{
    PassGpuStats combined;

    bool anyPresent = false;
    bool anyUnsupported = false;
    double summedPresentMilliseconds = 0.0;

    for (const PassGpuStats& entry : stats) {
        combined.drawStats.drawCallCount += entry.drawStats.drawCallCount;
        combined.drawStats.triangleCount += entry.drawStats.triangleCount;

        if (entry.timing.status == GpuTimingSample::Status::Present) {
            anyPresent = true;
            summedPresentMilliseconds += entry.timing.milliseconds;
        } else if (entry.timing.status == GpuTimingSample::Status::Unsupported) {
            anyUnsupported = true;
        }
    }

    if (anyPresent) {
        combined.timing.status = GpuTimingSample::Status::Present;
        combined.timing.milliseconds = summedPresentMilliseconds;
    } else if (anyUnsupported) {
        combined.timing.status = GpuTimingSample::Status::Unsupported;
    }
    // else: leave combined.timing at its default (Status::Absent, 0.0).

    return combined;
}

namespace {

// Resolves one declared read/write's resource name, from whichever of
// CompiledGraphInput::textures/buffers/volumeTextures' own `name` field
// actually applies to its kind - mirrors how RenderGraph.cpp itself resolves a
// ResourceUsage's target (see ApplyUsageBarrierIfNeeded()), just for a NAME
// instead of a physical resource. Never reads out of bounds (a stale/invalid
// index degrades to an empty string rather than crashing) - defensive, since
// this function's whole job is to build a DISPLAY artifact, never to assert
// correctness that Phase 1-3's own code already guarantees elsewhere.
//
// frame-debugger-5 campaign, PHASE1
// (PHASE1_RENDERGRAPH_COMPUTE_DISPATCH_CHOKEPOINT_INFRASTRUCTURE.md) -
// REQUIRED companion fix: this used to be a plain two-way
// `if (kind == Texture) {...} else {assume Buffer}` shape - the exact hazard
// RenderGraphCompiler.cpp/RenderGraph.cpp both explicitly document having
// already audited and converted away from when ResourceKind::VolumeTexture
// was first added (Atmosphere Scattering campaign, Phase 2) - this one file
// was missed by that audit, silently resolving every VolumeTexture usage to
// an empty string. Was fixed to a real, exhaustive, `default:`-less
// three-way `switch (usage.kind)`, mirroring that same established
// convention, so a future fourth ResourceKind fails to compile here too,
// until this function is updated to match. render-pass-6 campaign, PHASE6
// (item 2.2) - that hand-rolled switch was converted to a DispatchByKind()
// call (RenderGraphTypes.h) - same "no default: case, ever" exhaustiveness
// guarantee, just routed through the shared dispatcher every other
// ResourceKind branch in the codebase now uses too.
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    std::string name;
    DispatchByKind(usage,
        [&](TextureHandle h) {
            if (h.index < input.textures.size() && input.textures[h.index].name != nullptr) {
                name = input.textures[h.index].name;
            }
        },
        [&](BufferHandle h) {
            if (h.index < input.buffers.size() && input.buffers[h.index].name != nullptr) {
                name = input.buffers[h.index].name;
            }
        },
        [&](VolumeTextureHandle h) {
            if (h.index < input.volumeTextures.size() && input.volumeTextures[h.index].name != nullptr) {
                name = input.volumeTextures[h.index].name;
            }
        });
    return name;
}

RenderGraphPassSnapshot BuildPassSnapshot(const PassRecord& pass, std::size_t declarationIndex,
    const CompiledGraphInput& input, bool isCulled, const std::function<PassGpuStats(const char*)>& statsLookup,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup)
{
    RenderGraphPassSnapshot snapshot;
    snapshot.name = pass.name != nullptr ? pass.name : "";
    snapshot.isCulled = isCulled;
    snapshot.kind = pass.kind; // Render Pass campaign PHASE1 (task_manager/render-pass-1) - renamed from isComputePass
    snapshot.viewScope = pass.viewScope; // frame-debugger-6, PHASE1
    snapshot.renderPassEvent = pass.renderPassEvent; // render-pass-3 campaign, PHASE1

    // editor-core-separation-25 campaign - category/drawKind/tags no
    // longer live on PassRecord. snapshot.category/.drawKind/.tags start
    // at RenderGraphPassSnapshot's own struct defaults (General/DrawMesh/0)
    // and are only overwritten when metadataLookup is supplied AND
    // actually has an entry for this exact declarationIndex - this is the
    // graceful, zero-behavior-change fallback for a headless build (no
    // sink ever installed) and for every pre-existing test/call site that
    // does not pass a metadataLookup at all.
    if (metadataLookup) {
        PassDebugMetadata metadata;
        if (metadataLookup(declarationIndex, metadata)) {
            snapshot.category = metadata.category;
            snapshot.drawKind = metadata.drawKind;
            snapshot.tags = metadata.tags;
        }
    }

    snapshot.readNames.reserve(pass.reads.size());
    snapshot.readKinds.reserve(pass.reads.size());          // frame-debugger-5, PHASE1
    for (const ResourceUsage& usage : pass.reads) {
        snapshot.readNames.push_back(ResourceUsageName(usage, input));
        snapshot.readKinds.push_back(usage.kind);           // frame-debugger-5, PHASE1
    }

    snapshot.writeNames.reserve(pass.writes.size());
    snapshot.writeKinds.reserve(pass.writes.size());        // frame-debugger-5, PHASE1
    for (const ResourceUsage& usage : pass.writes) {
        snapshot.writeNames.push_back(ResourceUsageName(usage, input));
        snapshot.writeKinds.push_back(usage.kind);          // frame-debugger-5, PHASE1
    }

    // See this file's header comment - a culled pass's stats are
    // deliberately left at PassGpuStats{}'s own default, never resolved via
    // statsLookup().
    if (!isCulled && statsLookup) {
        snapshot.stats = statsLookup(pass.name);
    }

    return snapshot;
}

} // namespace

RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup, bool timingSlotBudgetExhausted,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup)
{
    RenderGraphSnapshot snapshot;
    snapshot.timingSlotBudgetExhausted = timingSlotBudgetExhausted; // PHASE1 (render-pass-6 campaign, item 2.4)
    snapshot.passesInExecutionOrder.reserve(compiled.executionOrder.size() + input.passes.size());

    // Surviving passes first, in real execution order. handle.index IS this
    // pass's own declarationIndex - the SAME index space
    // CompiledGraphInput::passes already uses (PassHandle::index), so no new
    // correlation problem is introduced.
    for (const PassHandle& handle : compiled.executionOrder) {
        if (handle.index >= input.passes.size()) {
            continue; // Defensive - never expected against a real Compile() result.
        }
        snapshot.passesInExecutionOrder.push_back(BuildPassSnapshot(
            input.passes[handle.index], handle.index, input, /*isCulled=*/false, statsLookup, metadataLookup));
    }

    // Culled passes appended afterwards, in their original declaration
    // order - still visible, per this file's own header comment. The loop
    // index IS this pass's own declarationIndex (an explicit index loop,
    // replacing the old range-for that had no index to give
    // BuildPassSnapshot()).
    for (std::size_t i = 0; i < input.passes.size(); ++i) {
        const PassRecord& pass = input.passes[i];
        if (!pass.isCulled) {
            continue;
        }
        snapshot.passesInExecutionOrder.push_back(
            BuildPassSnapshot(pass, i, input, /*isCulled=*/true, statsLookup, metadataLookup));
    }

    snapshot.resources.reserve(input.textures.size() + input.buffers.size());

    for (std::size_t i = 0; i < input.textures.size(); ++i) {
        RenderGraphResourceSnapshot resource;
        resource.name = input.textures[i].name != nullptr ? input.textures[i].name : "";
        resource.isImported = input.textures[i].importInfo.isImported;
        if (i < compiled.textureLifetimes.size()) {
            resource.firstUsePassIndex = compiled.textureLifetimes[i].firstUsePassIndex;
            resource.lastUsePassIndex = compiled.textureLifetimes[i].lastUsePassIndex;
        }
        snapshot.resources.push_back(std::move(resource));
    }

    for (std::size_t i = 0; i < input.buffers.size(); ++i) {
        RenderGraphResourceSnapshot resource;
        resource.name = input.buffers[i].name != nullptr ? input.buffers[i].name : "";
        // GPU Vertex Skinning campaign, Phase 3
        // (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md) -
        // a buffer resource CAN now be imported (RenderGraphBuilder::
        // ImportBuffer()) - mirrors the texture branch immediately above,
        // which was already correct.
        resource.isImported = input.buffers[i].importInfo.isImported;
        if (i < compiled.bufferLifetimes.size()) {
            resource.firstUsePassIndex = compiled.bufferLifetimes[i].firstUsePassIndex;
            resource.lastUsePassIndex = compiled.bufferLifetimes[i].lastUsePassIndex;
        }
        snapshot.resources.push_back(std::move(resource));
    }

    return snapshot;
}

} // namespace gte::rg
