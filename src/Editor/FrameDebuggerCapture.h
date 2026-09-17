#pragma once

#include "../Math/Mat4.h"
#include "../Renderer/RenderTexture.h" // frame-debugger-7 campaign, PHASE3 - m_replayStepPreviews below.

#include <cstdint>
#include <string>
#include <vector>

// task_manager/frame-debugger-3 campaign, PHASE1
// (PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md) - the foundation every later
// phase of this campaign builds on. Gives the engine a real, but completely
// opt-in and zero-overhead-when-disarmed, way to observe "what did
// RenderSystem::Draw()/Renderer::Submit() actually do, for the Game View,
// this exact frame".
//
// Deliberately lives under src/Editor/ (Editor-only, GTE_ENABLE_EDITOR-
// gated - see the root CMakeLists.txt's "Editor Module Structure" and
// AGENTS.md) rather than src/Renderer/, even though it is fed FROM
// RenderSystem/Renderer: this is a debugging/inspection concern Renderer
// itself must stay completely unaware of (see AGENTS.md, "Clean
// Architecture" - Renderer never depends on Editor). Renderer::Submit()'s
// own signature does NOT change at all for this feature; the only new call
// site is one layer up, in RenderSystem::Draw() (see RenderSystem.h/.cpp).
//
// IMPORTANT for every CORE, always-compiled file that touches this type
// (RenderSystem.h/.cpp today; Game.h/.cpp and RenderPasses.h/.cpp from
// PHASE3 onward) - see this phase's own Step 3.1b: such a file may only
// ever hold a bare forward-declared POINTER to FrameDebuggerCaptureContext
// in its own header (`class FrameDebuggerCaptureContext;`, no #include of
// this header), and must wrap BOTH the real #include of this header AND
// any actual dereference of that pointer inside `#if GTE_ENABLE_EDITOR` /
// `#endif` in its own .cpp file - otherwise a GTE_ENABLE_EDITOR=OFF build
// would compile fine (the forward declaration is enough) but FAIL TO LINK,
// since this type is entirely absent from that configuration.
namespace gte {

// This engine's REAL, constant (not per-material) blend/Z/stencil facts -
// see Pipeline.cpp's own hardcoded VkPipelineColorBlendAttachmentState/
// VkPipelineDepthStencilStateCreateInfo construction, which every field
// below is transcribed from directly (never guessed). There is exactly ONE
// Pipeline configuration in this whole engine today - this is a genuine,
// load-bearing simplification (see PHASE0's Step 2), not a stub standing in
// for a future per-material variant. Every field is already a display-ready
// string, matching FrameDebuggerEventDetails' own field shapes
// (src/Editor/FrameDebuggerData.h) one-for-one, so PHASE2's snapshot
// builder can copy these straight across with zero further formatting.
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

// frame-debugger-6 campaign, PHASE3 - one real, individual draw call's own
// attribution facts, in the exact order RecordEntityDraw() was called this
// frame (never deduplicated - unlike PipelineDebugNames()/
// MaterialTextureDebugNames() below, a repeated entity/mesh combination is
// still one entry per real draw, since PHASE4 needs one real, selectable
// tree leaf per real draw, not per distinct name).
struct FrameDebuggerDrawRecord {
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string displayName; // e.g. "terrain", or the synthesized
                              // "Entity <index>" fallback - see
                              // RenderSystem::Draw()'s own resolution logic.
    std::string pipelineDebugName;
    std::string materialTextureDebugName; // empty for an untextured draw.
    std::uint32_t triangleCount = 0;
};

// Returns this engine's REAL, hardcoded blend/Z/stencil facts, cross-
// checked against Pipeline.cpp's actual construction code at implementation
// time. Pure and free - no parameters, no live VkDevice needed, directly
// Tier-1-testable (see tests/Editor/FrameDebuggerCaptureTests.cpp and
// AGENTS.md, "Testability & Regression Safety").
FrameDebuggerStandardPipelineState DescribeStandardPipelineState();

// A per-frame recorder of real facts about what RenderSystem::Draw()
// resolved and submitted for the Game View this exact frame - which real
// Pipeline debug name(s) were used, which real MaterialTexture debug
// name(s) were bound, and the real view-projection matrix this frame's
// Game-View pass actually rendered with. Deliberately dumb/passive: it only
// ever accumulates what it's told via RecordDraw() - it has no idea what a
// "frame", a "pass", or "armed" even mean; that's the CALLER's job (see
// RenderSystem::Draw()'s own `capture` parameter, defaulted to nullptr
// everywhere until PHASE3 wires a real arming trigger).
//
// Zero-overhead-when-disarmed (a hard requirement - see this phase's own
// Step 2): a caller that never constructs one of these, or that always
// passes nullptr, pays nothing beyond RenderSystem::Draw()'s own single,
// already-resolved "is this pointer null" branch per draw call - no string
// formatting, no vector work, no allocation of any kind.
class FrameDebuggerCaptureContext {
public:
    // Records one real, already-resolved draw call. `pipelineDebugName`
    // should be `pipeline.DebugName()` (Pipeline.h) - empty is tolerated
    // (never added to PipelineDebugNames()) but should not normally happen
    // once every real Renderer::CreatePipeline() call site supplies a name
    // (see PHASE1's own Step 3.2). `materialTextureDebugName` is empty for
    // an untextured draw (the normal case for many draws) - never added to
    // MaterialTextureDebugNames() either way. `viewProjection` is real and
    // correct as-is (a plain, unconverted, COLUMN-major Mat4 - see Math/
    // Mat4.h) since every draw within one Game-View pass this frame uses
    // the exact same view-projection matrix, by construction (confirmed by
    // reading RenderSystem::Draw()'s own per-call-site `viewProj` argument
    // - see this phase's own Step 3.1). Both name lists are deduplicated
    // (first-seen order preserved) - recording the same name twice across
    // multiple draws never produces two entries.
    void RecordDraw(
        const std::string& pipelineDebugName, const std::string& materialTextureDebugName, const Mat4& viewProjection);

    // frame-debugger-6 campaign, PHASE3 - records one real, individual draw
    // call's own attribution facts, IN ADDITION to (never instead of) the
    // existing deduplicated PipelineDebugNames()/MaterialTextureDebugNames()/
    // DrawCallCount() bookkeeping RecordDraw() above already performs - call
    // this alongside RecordDraw() at RenderSystem::Draw()'s own call site.
    // `triangleCount` is computed the exact same way
    // DrawStats.h::AccumulateDrawStats() already computes a draw's own
    // contribution (HasIndexBuffer() ? IndexCount()/3 : VertexCount()/3) -
    // never a separately-invented formula.
    void RecordEntityDraw(std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& displayName,
        const std::string& pipelineDebugName, const std::string& materialTextureDebugName,
        std::uint32_t triangleCount);

    // Clears every recorded fact back to the empty/default state - call
    // once at the top of every armed frame (mirrors FrameRecorder::
    // BeginFrame()'s own per-frame-clear convention). A freshly-constructed
    // FrameDebuggerCaptureContext is already in this same empty state, so
    // this never strictly needs to be called before the very first frame.
    void Reset();

    // Every DISTINCT, non-empty real Pipeline debug name recorded via
    // RecordDraw() since the last Reset(), in first-seen order.
    const std::vector<std::string>& PipelineDebugNames() const noexcept { return m_pipelineDebugNames; }

    // Every DISTINCT, non-empty real MaterialTexture debug name recorded
    // via RecordDraw() since the last Reset(), in first-seen order.
    const std::vector<std::string>& MaterialTextureDebugNames() const noexcept { return m_materialTextureDebugNames; }

    // How many times RecordDraw() has been called since the last Reset() -
    // this frame's real Game-View draw-call count.
    int DrawCallCount() const noexcept { return m_drawCallCount; }

    // The LAST view-projection matrix RecordDraw() was called with since
    // the last Reset() - real and correct (see RecordDraw()'s own comment
    // above for why "last" is exactly as good as "the" for this campaign's
    // Game-View-only scope). Mat4::Identity() (NOT Mat4's own all-zero
    // default - see Math/Mat4.h) if RecordDraw() has never been called
    // since the last Reset().
    const Mat4& LastViewProjection() const noexcept { return m_lastViewProjection; }

    // frame-debugger-6 campaign, PHASE3 - every real per-draw attribution
    // record captured since the last Reset(), in real draw order (index 0 ==
    // first draw issued this frame's Game-View pass). Never deduplicated -
    // see FrameDebuggerDrawRecord's own doc comment above.
    const std::vector<FrameDebuggerDrawRecord>& DrawRecords() const noexcept { return m_drawRecords; }

    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.4) - called ONCE by AddFrameDebuggerReplayPasses()
    // (src/Application/RenderPasses.cpp), at Render-Graph-pass-declaration
    // time (a live Renderer& already exists there), on an explicit
    // capture-trigger frame only. Hands over N real, retained
    // RenderTexture objects - one per real object drawn this frame's
    // "GameView" pass, in the SAME order as DrawRecords() (see that
    // function's own doc comment for the one documented case this
    // ordering assumption could theoretically diverge in) - each one
    // holding the real, accumulated Game View image exactly as it looked
    // after objects [0..i] were redrawn from scratch. Move-only
    // (RenderTexture itself is move-only), mirroring
    // FrameDebuggerComputePassPreview's own vector-of-move-only-struct
    // precedent (FrameDebuggerHistory.h). Filled with FRESH (garbage/
    // uninitialized) content at the time this is called - each replay
    // pass's own `execute` lambda only actually renders real pixels into
    // its destination later this SAME Execute() call - so a caller must
    // never read these textures back before this whole Execute() call has
    // returned.
    //
    // IMPORTANT ordering requirement for TriggerCapture()/CaptureFrame()
    // (Phase 4's own job to act on) - this vector must be read/moved OUT
    // of this FrameDebuggerCaptureContext into permanent storage strictly
    // BEFORE the next armed frame's Reset() call below wipes it, i.e.
    // inside TriggerCapture() itself, which the Step 3.0 two-bool
    // handshake already guarantees runs later this SAME frame.
    void SetReplayStepPreviews(std::vector<RenderTexture>&& previews) noexcept
    {
        m_replayStepPreviews = std::move(previews);
    }

    // Empty on every frame that isn't itself a capture-trigger frame (see
    // SetReplayStepPreviews()'s own doc comment above) - Reset() (below)
    // clears this every armed frame, exactly like every other field on
    // this class.
    const std::vector<RenderTexture>& ReplayStepPreviews() const noexcept { return m_replayStepPreviews; }

private:
    std::vector<std::string> m_pipelineDebugNames;
    std::vector<std::string> m_materialTextureDebugNames;
    int m_drawCallCount = 0;
    Mat4 m_lastViewProjection = Mat4::Identity();
    std::vector<FrameDebuggerDrawRecord> m_drawRecords;

    // task_manager/frame-debugger-7 campaign, PHASE3 - see
    // SetReplayStepPreviews()/ReplayStepPreviews() above.
    std::vector<RenderTexture> m_replayStepPreviews;
};

} // namespace gte
