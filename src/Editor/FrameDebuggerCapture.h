#pragma once

#include "../Core/FrameDebuggerCaptureRecorder.h"
#include "../Core/FrameDebuggerEventSink.h"
#include "../Math/Mat4.h"
#include "../Renderer/RenderTexture.h" // m_replayStepPreviews below.
#include "../Renderer/RenderGraph/RetainedEventSnapshot.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// Forward declarations for the interface overrides below.
class Registry;
class Renderer;
class Mesh;
class Pipeline;
struct MaterialTexture;
class Game;
namespace rg {
class RenderGraphBuilder;
} // namespace rg

// Display-ready blend/Z/stencil facts of one pipeline configuration. Mirrors FrameDebuggerEventDetails.
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

// One bound material image of one draw.
struct FrameDebuggerMaterialSlot {
    std::string slot;      // MaterialImageRoleLabel(), for example "Normal".
    std::string imageName; // Debug name of the bound image; empty when unknown.
    friend bool operator==(const FrameDebuggerMaterialSlot&, const FrameDebuggerMaterialSlot&) = default;
};

// One real draw call's attribution facts, in draw order. Never deduplicated.
struct FrameDebuggerDrawRecord {
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string displayName; // e.g. "terrain", or the "Entity <index>" fallback.
    std::string pipelineDebugName;
    std::vector<FrameDebuggerMaterialSlot> materialSlots; // Empty for an untextured draw.
    std::uint32_t triangleCount = 0;
};

// Blend/Z/stencil facts of the standard mesh pipeline.
FrameDebuggerStandardPipelineState DescribeStandardPipelineState();

// Same for the sky background pipeline (depth test EQUAL, depth write off).
FrameDebuggerStandardPipelineState DescribeSkyBackgroundPipelineState();

// Per-frame recorder of what the primary view drew. Passive: it accumulates what it is told while active.
class FrameDebuggerCaptureContext : public IFrameDebuggerCaptureRecorder, public FrameDebuggerEventSink {
public:
    // Records one resolved draw. Pipeline names and (slot, image) pairs are deduplicated in first-seen order.
    // `viewProjection` is the column-major matrix the whole pass used.
    void RecordDraw(const std::string& pipelineDebugName, const std::vector<FrameDebuggerMaterialSlot>& slots,
        const Mat4& viewProjection);

    // Records one draw's attribution facts in addition to RecordDraw().
    void RecordEntityDraw(std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& displayName,
        const std::string& pipelineDebugName, const std::vector<FrameDebuggerMaterialSlot>& slots,
        std::uint32_t triangleCount);

    // IFrameDebuggerCaptureRecorder overrides; bodies live in FrameDebuggerDrawRecording.cpp and
    // FrameDebuggerReplayPasses.cpp.
    void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
        const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) override;

    std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
        float aspectWidthOverHeight, std::size_t objectCount,
        const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers, const rg::FullscreenReplay& backgroundReplay,
        RenderTexture& gameTarget, rg::RenderPassToggleRegistry* toggleRegistry = nullptr) override;

    // Arms this context for the frame and clears every recorded fact. Call once at the top of every armed frame.
    void Reset();

    // Stops counting and forgets per-draw capture state. Call on every frame that is not armed.
    void Deactivate() noexcept;

    // Live per-frame event counter: one fresh index per call. Separate index space from the event tree's eventIndex.
    int NextEventIndex() noexcept { return m_nextEventIndex++; }

    // Event count of the last armed frame; Reset() snapshots it before zeroing the counter.
    int LastFrameEventCount() const noexcept { return m_lastFrameEventCount; }

    // Binds the renderer the retained snapshot is built through. Cheap and idempotent.
    void InitializeEventCapture(Renderer& renderer) noexcept;

    // Arms per-draw capture for an event index from the next captured frame. Only that image is kept.
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
    const rg::RetainedEventSnapshot& RetainedEventImage() const noexcept { return m_retainedEventImage; }

    // True if the retained image is depth-only (color is preferred when a draw wrote both).
    bool RetainedEventIsDepth() const noexcept { return m_retainedEventIsDepth; }

    // Distinct, non-empty pipeline debug names recorded since the last Reset(), first-seen order.
    const std::vector<std::string>& PipelineDebugNames() const noexcept { return m_pipelineDebugNames; }

    // Distinct material slots recorded since the last Reset(), first-seen order; slots without an image name are skipped.
    const std::vector<FrameDebuggerMaterialSlot>& MaterialSlotRows() const noexcept { return m_materialSlotRows; }

    // The last view-projection passed to RecordDraw(); Mat4::Identity() if none since the last Reset().
    const Mat4& LastViewProjection() const noexcept { return m_lastViewProjection; }

    // Per-draw attribution records since the last Reset(), in draw order.
    const std::vector<FrameDebuggerDrawRecord>& DrawRecords() const noexcept { return m_drawRecords; }

    // Set by AddReplayPasses(): one retained texture per drawn object, plus a trailing background step if any.
    // Contents are valid only after the graph executed; move them out before the next Reset().
    void SetReplayStepPreviews(std::vector<RenderTexture>&& previews) noexcept
    {
        m_replayStepPreviews = std::move(previews);
    }

    // Empty on every frame that is not a capture frame. Mutable so capture can move the textures out.
    std::vector<RenderTexture>& ReplayStepPreviews() noexcept { return m_replayStepPreviews; }

private:
    std::vector<std::string> m_pipelineDebugNames;
    std::vector<FrameDebuggerMaterialSlot> m_materialSlotRows;
    Mat4 m_lastViewProjection = Mat4::Identity();
    std::vector<FrameDebuggerDrawRecord> m_drawRecords;
    std::vector<RenderTexture> m_replayStepPreviews;

    bool m_active = false;
    int m_nextEventIndex = 0;
    int m_lastFrameEventCount = 0;

    // Per-draw event capture state.
    Renderer* m_eventCaptureRenderer = nullptr;
    int m_armedEventIndex = -1;
    int m_retainedEventIndex = -1;
    bool m_retainedEventIsDepth = false;
    std::optional<AttachmentWrite> m_pendingEventCapture;
    rg::RetainedEventSnapshot m_retainedEventImage;
};

} // namespace gte
