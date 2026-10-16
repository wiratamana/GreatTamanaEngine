#pragma once

#include "PassChain.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"
#include "../../Core/PipelineDesc.h"

#include <array>
#include <initializer_list>
#include <optional>
#include <vector>

namespace gte::rg {

class Shader;
struct FullscreenReplay;

// One fullscreen triangle into one or more color targets (MRT), textures bound by shader variable name.
// An optional depth test reads the depth of a texture. Declare it with the scope of the view it draws for:
// a Shared pass cannot carry per-view bindings. Attachment formats come from the declared textures.
class FullscreenPass : public PassChain<FullscreenPass> {
public:
    FullscreenPass(RenderGraphBuilder& builder, Shader& shader, StaticName name, ViewScope scope,
        RenderPassEvent event = RenderPassEvent::AfterEverything);
    ~FullscreenPass();

    FullscreenPass(const FullscreenPass&) = delete;
    FullscreenPass& operator=(const FullscreenPass&) = delete;
    FullscreenPass(FullscreenPass&&) = delete;
    FullscreenPass& operator=(FullscreenPass&&) = delete;

    // Declares a depth read the shader does not perform (the compositor wants the view's depth acknowledged).
    FullscreenPass& Acknowledge(TextureHandle depth);

    // The one color attachment. `clear` set clears it, unset loads it.
    FullscreenPass& Target(TextureHandle target, std::optional<std::array<float, 4>> clear = std::nullopt);

    // Several color attachments, in the order of the shader's `layout(location = N) out` slots.
    FullscreenPass& Targets(std::initializer_list<TextureHandle> targets,
        std::optional<std::array<float, 4>> clear = std::nullopt);

    // Tests (and, with `write`, updates) the depth of `depth`. It may be the same texture as a color target
    // (a view target carries both). The depth is loaded, never cleared.
    FullscreenPass& DepthTest(TextureHandle depth, CompareOp op, bool write);

    FullscreenPass& Blend(BlendMode blend);

    // Declares the pass. Returns false when nothing was declared (inert shader or invalid chain).
    // When `replay` is set and the pass was declared, it receives a way to record the same draw again elsewhere.
    bool Draw(FullscreenReplay* replay = nullptr);

private:
    std::vector<TextureHandle> m_depthAcknowledgements;
    std::vector<TextureHandle> m_targets;
    std::optional<std::array<float, 4>> m_clear;
    std::optional<TextureHandle> m_depth;
    CompareOp m_depthCompare = CompareOp::Always;
    bool m_depthWrite = false;
    BlendMode m_blend = BlendMode::Opaque;
};

} // namespace gte::rg
