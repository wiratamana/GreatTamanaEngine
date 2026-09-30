#pragma once

// editor-core-separation-27 campaign, PHASE4 - a small, reusable, real,
// headless (VK_EXT_headless_surface) GPU test fixture: a real Renderer +
// a real RenderGraph, with NO Core/Game/EditorLayer/SDL/ImGui involved at
// all. Built on top of HeadlessSurfaceProvider.h (editor-core-separation-1,
// PHASE18) - the SAME "GTEST_SKIP() if this machine's Vulkan driver/loader
// doesn't report VK_EXT_headless_surface" discipline every existing
// consumer of that fixture already follows. Unlike every existing
// consumer, this ALSO drives real RenderGraph::Execute() frames - the
// first fixture in this codebase to do so.
//
// Usage:
//   HeadlessRenderGraphFixture fixture;
//   if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
//   fixture.RunSynchronousFrame([&](rg::RenderGraphBuilder& b) -> std::vector<rg::TextureHandle> {
//       ... declare passes ...
//       return {};
//   });

#include "../../src/Renderer/Renderer.h"
#include "../../src/Renderer/RenderGraph/RenderGraph.h"
#include "HeadlessSurfaceProvider.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gte {

class HeadlessRenderGraphFixture {
public:
    HeadlessRenderGraphFixture()
    {
        try {
            m_renderer = std::make_unique<Renderer>(m_surfaceProvider);
            m_renderGraph = std::make_unique<rg::RenderGraph>(*m_renderer);
        } catch (const std::exception& e) {
            m_skipReason = std::string("HeadlessRenderGraphFixture: construction failed - this machine's "
                                        "Vulkan driver/loader most likely does not support "
                                        "VK_EXT_headless_surface (see HeadlessSurfaceProvider.h). Underlying "
                                        "error: ") + e.what();
        }
    }

    bool IsUsable() const noexcept { return m_renderer != nullptr && m_renderGraph != nullptr; }
    const std::string& SkipReason() const noexcept { return m_skipReason; }

    Renderer& GetRenderer() { return *m_renderer; }
    rg::RenderGraph& GetRenderGraph() { return *m_renderGraph; }

    // Drives exactly ONE real SynchronousImmediateReadback Execute() call -
    // mirrors Core::BuildFrame()'s own Begin/Execute/End sequence exactly
    // (Renderer::BeginOffscreenRenderGraphRecording() ->
    // RenderGraph::Execute() -> Renderer::EndOffscreenRenderGraphRecording()),
    // with no gameTarget/sceneTarget/Editor concept involved at all -
    // `build` may declare whatever passes/resources a test needs.
    void RunSynchronousFrame(
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&)>& build)
    {
        const VkCommandBuffer cmd = m_renderer->BeginOffscreenRenderGraphRecording();
        m_renderGraph->Execute(cmd, rg::ExecuteTimingMode::SynchronousImmediateReadback, build);
        m_renderer->EndOffscreenRenderGraphRecording();
    }

private:
    HeadlessSurfaceProvider m_surfaceProvider;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<rg::RenderGraph> m_renderGraph;
    std::string m_skipReason;
};

} // namespace gte
