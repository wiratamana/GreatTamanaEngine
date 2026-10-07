#pragma once

// task_manager/better-render-pass-1 campaign, PHASE3
// (PHASE3_ENGINE_COMMAND_BUFFER_AND_TYPE_SAFE_PUSH_CONSTANTS.md) - R1/R4: a
// brand-new, engine-owned, pass-author-facing facade over the real Vulkan
// recording a compute/graphics render-graph pass issues today via several
// separate calls made by hand at every real pass's own `execute` callback
// (see CullingPipelines.cpp/AtmosphereLutRenderer.cpp/
// ComputeBlurValidation.cpp for ~26 existing
// `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
// renderer.Dispatch(...); renderer.EndGraphPassRecording();` call sites).
// `gte::rg::PassContext::Cmd()` (RenderGraph.h) builds one of these, fresh,
// every time a pass's `execute` callback calls it - this class is NEVER
// stored/held onto beyond that one call, mirroring PassContext's own
// lifetime discipline exactly (see RenderGraph.h's own PassContext doc
// comment).
//
// PURE, ADDITIVE INFRASTRUCTURE - this phase migrates ZERO real pass body.
// Every existing pass keeps calling Renderer::BeginGraphPassRecording()/
// Dispatch()/Submit()/EndGraphPassRecording() directly, completely
// unmodified - CommandBuffer is a NEW, alternative, opt-in way to do the
// exact same thing, not something existing call sites are forced onto
// (future phases - PHASE4 onward - migrate real passes onto it one batch at
// a time).
//
// --- Dispatch-accumulation state machine (confirmed via `ask_questions`
// during this phase's own implementation - see PHASE3_COMPLETION_REPORT.md)
// ---
//
// BindComputePipeline()/BindDescriptorSet()/SetPushConstants() only
// ACCUMULATE state on this CommandBuffer instance - none of them touch the
// GPU. This is a deliberately DIFFERENT shape than Renderer::Dispatch()'s
// existing all-in-one signature (which already binds the pipeline +
// optionally binds ONE descriptor set + optionally pushes constants +
// dispatches, all in a single call) - CommandBuffer exists specifically so a
// pass author can write the client's own rough two-statement sketch
// (`cmd.BindComputePipeline(...); cmd.SetPushConstants(...);
// cmd.Dispatch(...);`) instead. Only Dispatch()/DispatchOverSize() itself
// actually issues the real Renderer::BeginGraphPassRecording()/Dispatch()/
// EndGraphPassRecording() bracket, using whatever was most recently
// bound/set.
//
// --- Draw() shape ---
//
// Draw() is a single, thin, ONE-CALL forwarder straight to Renderer::Submit()
// - this engine's whole graphics draw model is already matrix-push-constant-
// based, never a separate BindPipeline()+Draw() two-step the way compute is,
// so there is no graphics-side "BindPipeline()" on this class at all.
// Deliberately only ONE draw method - no separate DrawIndexed(): confirmed
// directly against Renderer::Submit()'s real implementation (Renderer.cpp)
// and FrameRecorder::RecordFrame() that both already branch on
// Mesh::HasIndexBuffer() internally and issue the correct indexed/
// non-indexed draw command either way, so a single Mesh parameter already
// transparently covers both cases.
//
// Both Dispatch()/DispatchOverSize() AND Draw() open/close their own
// Renderer::BeginGraphPassRecording()/EndGraphPassRecording() bracket
// internally, automatically, EVERY call - the pass author never calls either
// of those two Renderer methods themselves when going through CommandBuffer
// (R1's own explicit requirement: "CommandBuffer must NOT require its caller
// to also separately call renderer.BeginGraphPassRecording()/
// EndGraphPassRecording()").

#include <volk.h>

#include "../DrawStats.h"
#include "../../Core/FrameDebuggerEventSink.h"
#include "../../Math/Mat4.h"

#include <cstdint>
#include <functional>

namespace gte {
class ComputePipeline;
class Mesh;
class Pipeline;
class Renderer;
} // namespace gte

namespace gte::rg {

// Pure, Tier-1-testable logic behind SetPushConstants<T>()'s debug-only
// size-mismatch assertion (R4) - `reflectedSize == 0` means "this
// ComputePipeline was built via the MANUAL path (ComputePipeline.h) and has
// no reflected push-constant metadata to check against at all", which must
// always report a match regardless of `suppliedSize` (a manually-built
// pipeline's own caller-supplied VkPushConstantRange is simply trusted,
// exactly as it was before this phase existed).
constexpr bool PushConstantSizeMatches(std::uint32_t suppliedSize, std::uint32_t reflectedSize) noexcept
{
    return reflectedSize == 0 || suppliedSize == reflectedSize;
}

class CommandBuffer {
public:
    // Which real color/depth attachment this pass's own write target is -
    // see PassContext::Cmd() (RenderGraph.h), the one real caller. Defaulted
    // (every field VK_NULL_HANDLE/VK_FORMAT_UNDEFINED) for a pass with no
    // such attachment, or any pre-existing call site built before this
    // field existed.
    struct WriteTargetInfo {
        VkImage colorImage = VK_NULL_HANDLE;
        VkExtent2D colorExtent{};
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkImage depthImage = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    };

    // `renderer`/`passDrawStats` may be nullptr only for a default-
    // constructed/never-handed-to-a-real-pass PassContext (see
    // RenderGraph.h) - every method below defensively asserts/no-ops rather
    // than dereferencing a null Renderer, mirroring PassContext's own
    // resolve*() methods' existing defensive-null-check discipline.
    // `writeTarget`/`eventSink` are trailing and defaulted so every
    // pre-existing call site keeps compiling unmodified - `eventSink` stays
    // null until an Editor-tier caller installs one via
    // RenderGraph::SetFrameDebuggerEventSink().
    CommandBuffer(VkCommandBuffer cmd, Renderer* renderer, DrawStats* passDrawStats,
        WriteTargetInfo writeTarget = {}, FrameDebuggerEventSink* eventSink = nullptr) noexcept;

    // Raw escape hatch for anything this class doesn't cover yet.
    VkCommandBuffer Native() const noexcept { return m_cmd; }

    // Remembers `pipeline` for the NEXT Dispatch()/DispatchOverSize() call on
    // THIS CommandBuffer instance - does not itself touch the GPU.
    void BindComputePipeline(const ComputePipeline& pipeline) noexcept { m_boundComputePipeline = &pipeline; }

    // Escape hatch (R1) - the one descriptor set Dispatch() binds as set 0,
    // for any pass not yet using a bindless/reflection-driven binding model.
    void BindDescriptorSet(VkDescriptorSet set) noexcept { m_boundDescriptorSet = set; }

    // Remembers `data`/`size` for the NEXT Dispatch()/DispatchOverSize() call
    // - `data` must stay alive until that call actually happens (the same
    // lifetime contract Renderer::Dispatch()'s own `pushConstants` parameter
    // already has today - a pass's `execute` callback keeps its own local
    // push-constant struct alive across these calls, same as every existing
    // manual call site does). Debug-asserts `size` against the currently-
    // bound ComputePipeline's own reflected PushConstantSize() whenever one
    // is bound and that pipeline actually has reflected push-constant
    // metadata (PushConstantSizeMatches() above) - turning the old silent
    // hand-sync bug class (a hand-restated `VkPushConstantRange::size`
    // literal silently drifting out of sync with the real GLSL block) into a
    // caught assertion (R4).
    void SetPushConstants(const void* data, std::uint32_t size) noexcept;

    template <typename T>
    void SetPushConstants(const T& data) noexcept
    {
        SetPushConstants(&data, static_cast<std::uint32_t>(sizeof(T)));
    }

    // Issues the real Renderer::BeginGraphPassRecording()/Dispatch()/
    // EndGraphPassRecording() bracket, using whichever ComputePipeline/
    // descriptor set/push-constant bytes were most recently bound/set on
    // THIS CommandBuffer via the methods above. Asserts (debug builds) that
    // a compute pipeline is actually bound and that this CommandBuffer has a
    // real Renderer to issue the dispatch against - a safe no-op in release
    // otherwise, mirroring Renderer::Dispatch()'s own existing "assert in
    // debug, safe no-op in release" discipline for the exact same underlying
    // precondition.
    void Dispatch(std::uint32_t groupX, std::uint32_t groupY = 1, std::uint32_t groupZ = 1);

    // Ceiling-division sibling of Dispatch() above - computes a correct
    // groupX/Y/Z from the bound ComputePipeline's own reflected
    // LocalGroupSize() (PHASE2) + ComputeDispatch.h's existing
    // ComputeGroupCount3D(), then calls Dispatch() with the result. Same
    // "bound compute pipeline required" precondition as Dispatch() above.
    void DispatchOverSize(std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1);

    // Thin, single-call forwarder straight to Renderer::Submit() - see this
    // file's own header comment for why there is deliberately no separate
    // DrawIndexed(). Opens/closes its own BeginGraphPassRecording()/
    // EndGraphPassRecording() bracket, same as Dispatch() above, so a pass
    // author never has to call either Renderer method directly even for a
    // pure graphics draw issued through CommandBuffer.
    void Draw(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE);

private:
    // Shared by Dispatch()/Draw() - both open their own
    // BeginGraphPassRecording()/EndGraphPassRecording() bracket and need the
    // exact same recordDrawStats-shaped callback (mirrors
    // PassContext::RecordDrawFn's own body exactly - see RenderGraph.h -
    // this class cannot reuse that type directly since CommandBuffer.h is
    // included BEFORE PassContext is fully defined, see RenderGraph.h's own
    // include order).
    std::function<void(bool, std::uint32_t, std::uint32_t)> MakeRecordDrawStatsCallback() const;

    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    Renderer* m_renderer = nullptr;
    DrawStats* m_passDrawStats = nullptr;
    WriteTargetInfo m_writeTarget;
    FrameDebuggerEventSink* m_eventSink = nullptr;

    const ComputePipeline* m_boundComputePipeline = nullptr;
    VkDescriptorSet m_boundDescriptorSet = VK_NULL_HANDLE;
    const void* m_pushConstantData = nullptr;
    std::uint32_t m_pushConstantSize = 0;
};

} // namespace gte::rg
