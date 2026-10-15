#include "MeshPass.h"

#include "PassKindDefaults.h"
#include "RenderGraph.h"
#include "../Renderer.h"

#include <cassert>
#include <utility>

namespace gte::rg {

SceneRecording::SceneRecording(PassContext& ctx)
    : m_renderer(ctx.renderer)
{
    assert(m_renderer != nullptr && "SceneRecording needs a pass context that carries a renderer");
    if (m_renderer != nullptr) {
        m_renderer->BeginGraphPassRecording(ctx.RawCommandBuffer(), ctx.recordDraw);
    }
}

SceneRecording::~SceneRecording()
{
    if (m_renderer != nullptr) {
        m_renderer->EndGraphPassRecording();
    }
}

std::optional<std::string> ValidateMeshPassRecord(const MeshPassRecord& record)
{
    if (!record.color.IsValid() && !record.depth.IsValid()) {
        return std::string("a mesh pass needs a color or a depth attachment");
    }
    return std::nullopt;
}

void DeclareMeshPass(RenderGraphBuilder::PassBuilder& pass, const MeshPassRecord& record)
{
    if (record.color.IsValid()) {
        pass.WriteColorAttachment(record.color, record.clearColor);
    }
    if (record.depth.IsValid()) {
        pass.WriteDepthStencilAttachment(record.depth, record.clearDepth);
    }
}

MeshPass::MeshPass(RenderGraphBuilder& builder, StaticName name, ViewScope scope, RenderPassEvent event)
    : m_builder(builder)
    , m_name(name)
    , m_scope(scope)
    , m_event(event)
{
}

MeshPass::~MeshPass()
{
    if (!m_drawn) {
        assert(false && "MeshPass destroyed without Draw()");
        Report("chain ended without Draw(): nothing was declared");
    }
}

void MeshPass::Report(const std::string& text)
{
    m_builder.ReportOnce("MeshPass", std::string("Pass '") + m_name.value + "': " + text);
}

MeshPass& MeshPass::Color(TextureHandle target, std::optional<std::array<float, 4>> clear)
{
    m_record.color = target;
    m_record.clearColor = clear;
    return *this;
}

MeshPass& MeshPass::Depth(TextureHandle target, std::optional<float> clear)
{
    m_record.depth = target;
    m_record.clearDepth = clear;
    return *this;
}

bool MeshPass::Draw(std::function<void()> draw)
{
    if (m_drawn) {
        Report("Draw() called twice on one chain");
        return false;
    }
    m_drawn = true;
    if (const auto error = ValidateMeshPassRecord(m_record)) {
        Report(*error);
        return false;
    }
    if (!draw) {
        Report("no draw function was given");
        return false;
    }

    m_builder.AddRenderPass(
        m_name.value, DefaultPassKind(DeclarativePassType::Mesh), m_scope, RenderPassCategory::General,
        [record = m_record](RenderGraphBuilder::PassBuilder& pass) { DeclareMeshPass(pass, record); },
        [draw = std::move(draw)](PassContext& ctx) {
            const SceneRecording recording(ctx);
            draw();
        },
        DefaultDrawKind(DeclarativePassType::Mesh), m_event);
    return true;
}

} // namespace gte::rg
