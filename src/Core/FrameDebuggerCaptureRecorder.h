#pragma once

// New header, editor-core-separation-2 campaign, PHASE2 - closes the two
// real gte_core -> gte_editor-only-symbol undefined-reference hazards
// editor-core-separation-1 left open (RecordFrameDebuggerDraws(),
// AddFrameDebuggerReplayPasses() - see that campaign's own
// CAMPAIGN_COMPLETION_REPORT.md, "What remains genuinely open"). A pure,
// gte_core-owned abstract interface - gte_core-tier code (RenderSystem.cpp,
// Core.cpp) calls through a pointer to THIS type instead of calling a
// gte_editor-only free function by name, so no link-time symbol from
// gte_editor.a is ever required inside gte_core.a itself; only a vtable
// read at runtime, satisfied by whichever concrete object (always
// FrameDebuggerCaptureContext, gte_editor-owned) the pointer actually
// points at. Mirrors IEditorLayer's own already-proven opaque-interface
// pattern, one boundary layer deeper (this interface is reached FROM
// inside Core-tier code that has no access to IEditorLayer* at all -
// RenderSystem.cpp is three call-levels away from Core::BuildFrame(), via
// Core.cpp's own inline "RenderOpaque" provider -> Game.cpp -> RenderSystem.cpp
// - see this phase's own Step 1 for the confirmed, real call chain).
//
// A nullptr of this type means exactly what a nullptr
// FrameDebuggerCaptureContext* used to mean: "the Frame Debugger is not
// currently armed for this frame" (the overwhelmingly common case) - every
// existing null-check call site (RenderSystem::Draw(), Core::BuildFrame())
// is completely unchanged by this interface's introduction.

#include "../Math/Mat4.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h" // rg::TextureHandle, rg::BufferHandle
#include "../ECS/Entity.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace gte {

class Registry;
class Renderer;
class Mesh;
class Pipeline;
// MaterialTexture (src/Renderer/MaterialTexture.h) is a real `struct`, NOT a
// `class` - forward-declare it with the SAME tag its own definition uses
// (`struct MaterialTexture;`, not `class MaterialTexture;`) to avoid a real,
// if harmless-by-default, struct/class forward-declaration tag mismatch
// (MSVC's own C4099 warning, or -Wmismatched-tags on Clang) - confirmed
// against the real header (src/Renderer/MaterialTexture.h line 26) before
// writing this line.
struct MaterialTexture;
class Game;
class RenderTexture;

namespace rg {
class RenderGraphBuilder;
class RenderPassToggleRegistry;
} // namespace rg

class IFrameDebuggerCaptureRecorder {
public:
    virtual ~IFrameDebuggerCaptureRecorder() = default;

    // Replaces the old free function gte::RecordFrameDebuggerDraws() -
    // same parameters, same semantics, same call site
    // (RenderSystem::Draw()), just a virtual method instead of a
    // gte_editor-only free function called by name.
    virtual void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
        const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) = 0;

    // Replaces the old free function gte::AddFrameDebuggerReplayPasses() -
    // same parameters (minus the trailing FrameDebuggerCaptureContext&,
    // which becomes the implicit `this`), same semantics, same call site
    // (Core::BuildFrame()).
    //
    // editor-core-separation-21 campaign, PHASE4 (fixing PHASE3's
    // confirmed-lie finding #24) - `toggleRegistry` (default nullptr) lets
    // this method honestly consult RenderPassToggleRegistry::
    // NoteDeclaredAndCheckEnabled("FrameDebuggerReplay") ONCE, as a single
    // whole-mechanism switch (mirroring AddGpuSkinningPasses()'s own
    // "GpuSkinning" whole-stage precedent) rather than per dynamically-
    // named "FrameDebuggerReplayStepN" pass - these are ephemeral, one-
    // capture-lifetime debug tooling passes, not real content, so one
    // umbrella toggle is the correct granularity.
    virtual std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game,
        Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
        const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
        const std::function<void(VkCommandBuffer)>& recordBackgroundStep, RenderTexture& gameTarget,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr) = 0;
};

} // namespace gte
