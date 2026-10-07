#pragma once

// Pass-author-facing facade over the real Vulkan recording a compute/
// graphics render-graph pass issues (bind pipeline, push constants,
// dispatch/draw). gte::rg::PassContext::Cmd() (RenderGraph.h) builds one of
// these, fresh, every time a pass's `execute` callback calls it - never
// stored/held onto beyond that one call.
//
// BindComputePipeline()/BindDescriptorSet()/SetPushConstants() only
// ACCUMULATE state on this instance - none of them touch the GPU. Only
// Dispatch()/DispatchOverSize() actually issues the real
// Renderer::BeginGraphPassRecording()/Dispatch()/EndGraphPassRecording()
// bracket, using whatever was most recently bound/set.
//
// Draw() is a single, thin forwarder straight to Renderer::Submit() - no
// separate BindPipeline()+Draw() two-step like compute, and no separate
// DrawIndexed() (Renderer::Submit() already branches on
// Mesh::HasIndexBuffer() internally for both).
//
// Both Dispatch()/DispatchOverSize() AND Draw() open/close their own
// BeginGraphPassRecording()/EndGraphPassRecording() bracket internally, every
// call - a pass author never calls either Renderer method directly when
// going through CommandBuffer.

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
// size-mismatch assertion - `reflectedSize == 0` means "this ComputePipeline
// was built via the MANUAL path (ComputePipeline.h) and has no reflected
// push-constant metadata to check against at all", which must always report
// a match regardless of `suppliedSize` (a manually-built pipeline's own
// caller-supplied VkPushConstantRange is simply trusted).
constexpr bool PushConstantSizeMatches(std::uint32_t suppliedSize, std::uint32_t reflectedSize) noexcept
{
    return reflectedSize == 0 || suppliedSize == reflectedSize;
}

// A pass's write-target attachment - see PassContext::Cmd() (RenderGraph.h).
// Free, namespace-scope struct (not nested in CommandBuffer): a nested
// struct with default member initializers can't be used as `= {}` in a
// default argument of its own still-incomplete enclosing class.
struct CommandBufferWriteTargetInfo {
    VkImage colorImage = VK_NULL_HANDLE;
    VkExtent2D colorExtent{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkImage depthImage = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
};

class CommandBuffer {
public:
    // Alias so every call site spelled as `CommandBuffer::WriteTargetInfo`
    // keeps compiling unchanged.
    using WriteTargetInfo = CommandBufferWriteTargetInfo;

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

    // Escape hatch - the one descriptor set Dispatch() binds as set 0,
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
    // caught assertion.
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
    // pure graphics draw issued through CommandBuffer. This is also the one
    // place FrameDebuggerEventSink::NoteCommandResult() is wired - see that
    // interface's own doc comment for the real per-draw gating this now
    // feeds (RenderOpaque's per-entity loop, RenderSystem::Draw()).
    // `sceneServicesSet` mirrors Renderer::Submit()'s own trailing parameter
    // exactly - forwarded unchanged, never resolved here.
    void Draw(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE,
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE);

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
