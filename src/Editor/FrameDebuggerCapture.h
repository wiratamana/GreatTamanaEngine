#pragma once

#include "../Core/FrameDebuggerCaptureRecorder.h"
#include "../Core/FrameDebuggerEventSink.h"
#include "../Math/Mat4.h"
#include "../Renderer/RenderTexture.h" // m_replayStepPreviews below.
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h" // ResourceState/RequiredStateFor() - per-draw event capture.
#include "../Renderer/RenderGraph/RenderGraphEventSnapshotPool.h" // EventSnapshotResource/CopyImageIntoSnapshot() - per-draw event capture.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Editor-only recorder of what the Game View drew this frame, fed from RenderSystem::Draw() through the
// IFrameDebuggerCaptureRecorder interface and the per-draw FrameDebuggerEventSink hook.
// Core files must hold only a forward-declared pointer and guard dereferences with GTE_ENABLE_EDITOR.
namespace gte {

// Forward declarations for the interface overrides below.
class Registry;
class Renderer;
class Mesh;
class Pipeline;
struct MaterialTexture; // Defined as a struct, not a class.
class Game;
namespace rg {
class RenderGraphBuilder;
} // namespace rg

// Display-ready blend/Z/stencil facts of one pipeline configuration, matching FrameDebuggerEventDetails
// (src/Editor/FrameDebuggerData.h) field for field.
struct FrameDebuggerStandardPipelineState {
    std::string blendMode;
    std::string zClip;
    std::string zTest;
    std::string zWrite;
    std::string cull;
    std::string stencilRef;
    std::string stencilComp;
    std::string stencilPass;
    std::string stencilFail;
    std::string stencilZFail;
};

// One real draw call's attribution facts, in draw order. Never deduplicated: one entry per draw.
struct FrameDebuggerDrawRecord {
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string displayName; // e.g. "terrain", or the "Entity <index>" fallback.
    std::string pipelineDebugName;
    std::string materialTextureDebugName; // empty for an untextured draw.
    std::uint32_t triangleCount = 0;
};

// Blend/Z/stencil facts of the standard mesh pipeline (see Pipeline.cpp). Pure and free.
FrameDebuggerStandardPipelineState DescribeStandardPipelineState();

// Same for the sky background pipeline (depth test EQUAL, depth write off). Pure and free.
FrameDebuggerStandardPipelineState DescribeSkyBackgroundPipelineState();

// Passive per-frame recorder of what RenderSystem::Draw() resolved for the Game View. It only accumulates what it
// is told; the caller decides when it is armed. A caller that passes nullptr pays only one null check per draw.
class FrameDebuggerCaptureContext : public IFrameDebuggerCaptureRecorder, public FrameDebuggerEventSink {
public:
    // Records one resolved draw. Both names are deduplicated in first-seen order; empty names are never added.
    // `viewProjection` is the column-major matrix the whole Game-View pass used.
    void RecordDraw(
        const std::string& pipelineDebugName, const std::string& materialTextureDebugName, const Mat4& viewProjection);

    // Records one draw's attribution facts in addition to RecordDraw(). `triangleCount` follows
    // DrawStats.h::AccumulateDrawStats().
    void RecordEntityDraw(std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& displayName,
        const std::string& pipelineDebugName, const std::string& materialTextureDebugName,
        std::uint32_t triangleCount);

    // IFrameDebuggerCaptureRecorder overrides; bodies live in FrameDebuggerDrawRecording.cpp and
    // FrameDebuggerReplayPasses.cpp.
    void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
        const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) override;

    std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
        float aspectWidthOverHeight, std::size_t objectCount,
        const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
        const std::function<void(const rg::RecordingContext&)>& recordBackgroundStep, RenderTexture& gameTarget,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr) override;

    // Clears every recorded fact. Call once at the top of every armed frame.
    void Reset();

    // Live per-frame event counter: one fresh index per call, numbering every draw or dispatch recorded this
    // armed frame across all passes. Separate index space from the event tree's own eventIndex.
    int NextEventIndex() noexcept { return m_nextEventIndex++; }

    // NextEventIndex() high-water mark of the last armed frame; Reset() snapshots it before zeroing the counter.
    int LastFrameEventCount() const noexcept { return m_lastFrameEventCount; }

    // Binds the renderer the retained snapshot is built through. Call before installing this as the event
    // sink; cheap and idempotent.
    void InitializeEventCapture(Renderer& renderer) noexcept;

    // Re-arms per-draw capture for an event index; takes effect from the next captured frame.
    // Only the armed index's image is kept, in one resized-in-place scratch slot, to bound VRAM.
    void ArmEventIndex(int index) noexcept { m_armedEventIndex = index; }

    // FrameDebuggerEventSink overrides.
    void NoteCommandResult(const AttachmentWrite& write) override;
    void FlushPendingCapture(const rg::RecordingContext& cmd) override;

    // True once the armed index's image has been captured since it was armed.
    bool HasRetainedEventImage() const noexcept
    {
        return m_armedEventIndex >= 0 && m_retainedEventIndex == m_armedEventIndex;
    }

    // Meaningful only when HasRetainedEventImage() is true.
    const rg::EventSnapshotResource& RetainedEventImage() const noexcept { return m_retainedEventImage; }

    // True if the retained image is a depth-only capture (color is preferred when a draw wrote both).
    // Meaningful only when HasRetainedEventImage() is true.
    bool RetainedEventIsDepth() const noexcept { return m_retainedEventIsDepth; }

    // Distinct, non-empty pipeline debug names recorded via RecordDraw() since the last Reset(), first-seen order.
    const std::vector<std::string>& PipelineDebugNames() const noexcept { return m_pipelineDebugNames; }

    // Distinct, non-empty material texture debug names recorded via RecordDraw() since the last Reset().
    const std::vector<std::string>& MaterialTextureDebugNames() const noexcept { return m_materialTextureDebugNames; }

    // RecordDraw() calls since the last Reset(): this frame's Game-View draw-call count.
    int DrawCallCount() const noexcept { return m_drawCallCount; }

    // The last view-projection passed to RecordDraw(); Mat4::Identity() if none since the last Reset().
    const Mat4& LastViewProjection() const noexcept { return m_lastViewProjection; }

    // Per-draw attribution records since the last Reset(), in draw order. Never deduplicated.
    const std::vector<FrameDebuggerDrawRecord>& DrawRecords() const noexcept { return m_drawRecords; }

    // Called by AddReplayPasses() on a capture-trigger frame: one retained RenderTexture per drawn object in
    // DrawRecords() order (after objects [0..i]), plus a trailing background step when a background callback
    // exists. The textures hold garbage until Execute() returns; move them out before the next Reset().
    void SetReplayStepPreviews(std::vector<RenderTexture>&& previews) noexcept
    {
        m_replayStepPreviews = std::move(previews);
    }

    // Empty on every frame that is not a capture-trigger frame. Mutable so capture can move the textures out.
    std::vector<RenderTexture>& ReplayStepPreviews() noexcept { return m_replayStepPreviews; }

private:
    std::vector<std::string> m_pipelineDebugNames;
    std::vector<std::string> m_materialTextureDebugNames;
    int m_drawCallCount = 0;
    Mat4 m_lastViewProjection = Mat4::Identity();
    std::vector<FrameDebuggerDrawRecord> m_drawRecords;

    // See SetReplayStepPreviews().
    std::vector<RenderTexture> m_replayStepPreviews;

    // See NextEventIndex()/LastFrameEventCount() above.
    int m_nextEventIndex = 0;
    int m_lastFrameEventCount = 0;

    // Per-draw event capture state (see ArmEventIndex(), NoteCommandResult(), FlushPendingCapture()).
    Renderer* m_eventCaptureRenderer = nullptr;
    int m_armedEventIndex = -1;
    int m_retainedEventIndex = -1;
    bool m_retainedEventIsDepth = false; // See RetainedEventIsDepth().
    std::optional<AttachmentWrite> m_pendingEventCapture;
    rg::EventSnapshotResource m_retainedEventImage;
};

} // namespace gte
