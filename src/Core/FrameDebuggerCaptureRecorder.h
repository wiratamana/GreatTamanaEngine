#pragma once

// Engine-side interface to the Editor's Frame Debugger. Core code calls through a pointer to this type, so
// gte_core never needs a symbol from gte_editor. A nullptr means "not armed for this frame".

#include "../Math/Mat4.h"
#include "../Renderer/RenderGraph/FullscreenReplay.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h" // rg::TextureHandle, rg::BufferHandle
#include "../ECS/Entity.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace gte {

class Registry;
class Renderer;
class Mesh;
class Pipeline;
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

    // Records one scene draw for the armed frame.
    virtual void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
        const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) = 0;

    // Declares the replay passes: one per scene object, plus one for the view background when `backgroundReplay`
    // is set. `toggleRegistry` gates the whole mechanism via "FrameDebuggerReplay".
    virtual std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game,
        Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
        const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers, const rg::FullscreenReplay& backgroundReplay,
        RenderTexture& gameTarget, rg::RenderPassToggleRegistry* toggleRegistry = nullptr) = 0;
};

} // namespace gte
