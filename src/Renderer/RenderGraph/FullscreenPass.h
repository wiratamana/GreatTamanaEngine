#pragma once

#include "PassChain.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"
#include "../../Core/PipelineDesc.h"

#include <array>
#include <optional>
#include <vector>

namespace gte::rg {

class Shader;

// One fullscreen triangle into a single color target, textures bound by shader variable name.
// Declare it with the scope of the view it draws for: a Shared pass cannot carry per-view bindings.
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

    // The color attachment. `clear` set clears it, unset loads it.
    FullscreenPass& Target(TextureHandle target, std::optional<std::array<float, 4>> clear = std::nullopt);

    FullscreenPass& Blend(BlendMode blend);

    // Declares the pass. Returns false when nothing was declared (inert shader or invalid chain).
    bool Draw();

private:
    std::vector<TextureHandle> m_depthAcknowledgements;
    TextureHandle m_target{};
    std::optional<std::array<float, 4>> m_clear;
    BlendMode m_blend = BlendMode::Opaque;
};

} // namespace gte::rg
