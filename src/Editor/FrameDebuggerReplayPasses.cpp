// editor-core-separation-2 campaign, PHASE2 - this file now defines
// FrameDebuggerCaptureContext::AddReplayPasses(), the concrete
// implementation of IFrameDebuggerCaptureRecorder::AddReplayPasses()
// (src/Core/FrameDebuggerCaptureRecorder.h), reachable from gte_core-tier
// Core::BuildFrame() only through a virtual call on a null-checked
// IFrameDebuggerCaptureRecorder* pointer - never a gte_editor-only
// free-function symbol by name. This closes the exact undefined-reference
// hazard editor-core-separation-1's own PHASE2
// (PHASE2_FRAME_DEBUGGER_CAPTURE_POINTER_SAFETY_FIX.md) left open (see
// task_manager/editor-core-separation-2/PHASE0_MASTER_STRATEGY.md's own
// "Defect B"): that phase's own body used to live in
// src/Application/RenderPasses.cpp, wrapped in `#if GTE_ENABLE_EDITOR`
// (calling real methods - `capture.SetReplayStepPreviews(...)` - on the
// complete FrameDebuggerCaptureContext type), with an `#else` no-op stub for
// the OFF configuration, then moved to a dedicated free function
// (AddFrameDebuggerReplayPasses(), declared in
// src/Application/RenderPasses.h, defined only here) - but a free function
// called BY NAME from gte_core-tier code (Core::BuildFrame()) is still,
// itself, exactly that same hazard. Behavior is UNCHANGED from the old free
// function's own body - only its shape (a member function instead of a free
// function taking a trailing FrameDebuggerCaptureContext& parameter, which
// becomes the implicit `this` instead) and its call site (Core::BuildFrame()
// now calls `frameDebuggerCapture->AddReplayPasses(...)` through the
// interface pointer, instead of
// `AddFrameDebuggerReplayPasses(..., *frameDebuggerCapture)` by name)
// changed.

#include "../Application/RenderPasses.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "FrameDebuggerCapture.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace gte {

namespace {

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
// 3.3b) - permanent (whole-process-lifetime), ever-growing pool of
// "FrameDebuggerReplayStepN" pass names. MUST NOT be a per-frame/per-
// capture temporary: RenderGraphBuilder::AddPass()'s own `name` parameter,
// and RenderGraphNameSlotTable's own persistent-across-Execute()-calls
// name table, both require a name that is valid for the rest of this
// process's lifetime, never just "this frame" - a stack buffer or a
// function-local std::string/std::vector would produce a real, confirmed
// dangling-pointer / use-after-free bug the very next time ANY capture
// happens (the second, third, ... capture this session) - see this
// campaign's own phase document for the full reasoning. std::deque (never
// std::vector) so growing this pool NEVER moves an already-handed-out
// std::string's own character storage - a std::vector<std::string>
// growing/reallocating would invalidate every c_str() pointer already
// stored inside a PREVIOUSLY-declared PassRecord::name, which is exactly
// the same class of bug this whole mechanism exists to avoid.
std::deque<std::string>& ReplayStepPassNamePool()
{
    static std::deque<std::string> pool;
    return pool;
}

// Returns a STABLE, permanent const char* naming replay step `index` -
// lazily grows the pool the first time `index` is ever requested, then
// reuses the SAME std::string (and therefore the SAME pointer) for that
// index forever afterwards, across every future capture this session.
const char* ReplayStepPassName(std::size_t index)
{
    std::deque<std::string>& pool = ReplayStepPassNamePool();
    while (pool.size() <= index) {
        pool.push_back("FrameDebuggerReplayStep" + std::to_string(pool.size()));
    }
    return pool[index].c_str();
}

} // namespace

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md) - see
// this method's own doc comment in src/Core/FrameDebuggerCaptureRecorder.h
// for the full contract.
std::vector<rg::TextureHandle> FrameDebuggerCaptureContext::AddReplayPasses(rg::RenderGraphBuilder& builder,
    Game& game, Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    std::vector<rg::TextureHandle> destHandles;

    // editor-core-separation-21 campaign, PHASE4 (fixing PHASE3's
    // confirmed-lie finding #24) - a single, whole-mechanism on/off switch,
    // checked BEFORE any per-step RenderTexture/pass declaration - see this
    // method's own doc comment in FrameDebuggerCaptureRecorder.h for why
    // this is one umbrella toggle rather than one per dynamically-named
    // "FrameDebuggerReplayStepN" pass.
    if (toggleRegistry != nullptr && !toggleRegistry->NoteDeclaredAndCheckEnabled("FrameDebuggerReplay")) {
        return destHandles;
    }

    // frame-debugger-8 campaign, PHASE2 - `includeSkyStep`/`totalStepCount`
    // REPLACE the old `if (objectCount == 0) return;` early-out. A real
    // scene with ZERO mesh entities (e.g. Camera + Directional Light only)
    // still genuinely draws the sky every frame - it must still get
    // exactly one real, selectable replay step, not zero (this is a real,
    // confirmed, second fix this phase makes as a natural side effect of
    // the redesign below - see PHASE0_MASTER_STRATEGY.md's own Definition
    // of Done, "zero mesh entities" bullet).
    const bool includeSkyStep = static_cast<bool>(recordSkyBackground);
    const std::size_t totalStepCount = objectCount + (includeSkyStep ? 1 : 0);
    if (totalStepCount == 0) {
        return destHandles;
    }
    destHandles.reserve(totalStepCount);

    // Same width/height/format as the real GameView target, read directly
    // off `gameTarget` (never hardcoded - see AGENTS.md's "Render Target
    // Format Matching") - `gameTarget` itself is NEVER written to here.
    const VkExtent2D extent = gameTarget.Extent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    const VkFormat format = gameTarget.Format();

    std::vector<RenderTexture> destinations;
    destinations.reserve(totalStepCount); // ESSENTIAL - every destHandle below imports a POINTER-STABLE
                                           // Target() from this vector; it must never reallocate after this point.
    for (std::size_t i = 0; i < totalStepCount; ++i) {
        // Step 3.3b - these debugName/depthDebugName stack buffers are a
        // COMPLETELY SEPARATE, unrelated concern from the pass NAME below
        // (ReplayStepPassName()) - safe ONLY because these RenderTextures
        // are always freshly (re)created every capture, NEVER Resize()d in
        // place (mirrors FrameDebuggerHistory.cpp's own identical
        // reasoning for its own retained-preview textures).
        char debugNameBuffer[48];
        std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerReplayStep%zuColor", i);
        char depthDebugNameBuffer[48];
        std::snprintf(depthDebugNameBuffer, sizeof(depthDebugNameBuffer), "FrameDebuggerReplayStep%zuDepth", i);
        // Depth is created automatically (Renderer::CreateRenderTexture()
        // always builds it against Renderer::DepthFormat() internally) -
        // see Step 3.3a: a RenderTexture already carries its own paired
        // depth buffer, no second array needed.
        destinations.push_back(
            renderer.CreateRenderTexture(width, height, format, debugNameBuffer, depthDebugNameBuffer));
    }

    for (std::size_t i = 0; i < totalStepCount; ++i) {
        const char* passName = ReplayStepPassName(i); // Step 3.3b - NEVER a per-call temporary.

        const rg::TextureHandle destHandle =
            builder.ImportTexture(passName, destinations[i].Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        // frame-debugger-8 campaign, PHASE2 - THE fix. `isSkyStep` is true
        // for EXACTLY ONE index: the extra, dedicated step this phase adds
        // (only reachable when includeSkyStep is true, and only ever equal
        // to `objectCount` - i.e. the very last index in [0, totalStepCount)).
        // Every OTHER index (a real per-object step, i in [0, objectCount))
        // NEVER draws sky, no matter what - this is the actual bug fix:
        // the OLD code's own "if (i + 1 == objectCount && recordSkyBackground)"
        // branch inside the per-object loop is GONE, not just moved.
        const bool isSkyStep = includeSkyStep && (i == objectCount);
        // Real per-object steps redraw objects [0..i] (maxDrawCount = i+1,
        // UNCHANGED from before). The one dedicated sky step redraws EVERY
        // real object (maxDrawCount = objectCount) and then, ADDITIONALLY,
        // the sky - matching the real "GameView" pass's own true order
        // (every entity, then sky, see AddGameViewPass()).
        const std::size_t maxDrawCount = isSkyStep ? objectCount : (i + 1);

        // Render Pass campaign (task_manager/render-pass-1), PHASE4
        // (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.3b) - these N
        // debug-only replay passes now declare through the AddRenderPass()
        // chokepoint (PHASE1), tagged rg::RenderPassCategory::Debug (pulled
        // forward from PHASE5's originally-planned scope) - this is what lets
        // BuildRealFrameDebuggerSnapshot()'s own "view region" walk
        // (FrameDebuggerData.cpp) skip these passes instead of leaking them
        // into the tree as spurious extra leaves, even on the exact capture
        // frame that declares them. Same `name`/`setup`/`execute`, zero
        // behavior change beyond this new stamped metadata.
        builder.AddRenderPass(passName, rg::PassKind::Graphics, rg::ViewScope::GameView, rg::RenderPassCategory::Debug,
            [destHandle, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(destHandle, kGameClearColor);
                pass.WriteDepthStencilAttachment(destHandle, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
            },
            [&game, &renderer, aspectWidthOverHeight, maxDrawCount, isSkyStep, recordSkyBackground](
                rg::PassContext& ctx) {
                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // frameDebuggerCapture is ALWAYS nullptr here, NEVER the
                // real, armed capture context - see this function's own
                // pre-existing correctness-critical comment (unchanged
                // reasoning, still applies): RecordDraw()/RecordEntityDraw()
                // are not idempotent/deduplicated by draw identity, so
                // feeding a real capture pointer into these N+1 replay
                // passes' own game.Render() calls would silently balloon
                // capture.DrawRecords() into O(N^2) duplicated entries.
                game.Render(renderer, aspectWidthOverHeight, nullptr, /*frameDebuggerCapture=*/nullptr, maxDrawCount);
                renderer.EndGraphPassRecording();
                // Sky is drawn ON EXACTLY ONE dedicated step now - the
                // frame-debugger-8 campaign's own fix for the "last
                // object's own preview silently already included sky"
                // bug (see PHASE0_MASTER_STRATEGY.md Step 2, point 5).
                if (isSkyStep && recordSkyBackground) {
                    recordSkyBackground(ctx.cmd);
                }
            });

        destHandles.push_back(destHandle);
    }

    SetReplayStepPreviews(std::move(destinations));
    return destHandles;
}

} // namespace gte
