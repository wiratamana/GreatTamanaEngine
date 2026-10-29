#include "Frame.h"

#include "ComputeShader.h"
#include "VerbSupport.h"
#include "../Core.h"
#include "../Plugins/RenderFeatureCompositor.h"
#include "../../Renderer/RenderGraph/Shader.h"
#include "../Logging.h"

#include <string>

namespace gte::fx {

namespace {

constexpr const char* kLogCategory = "FeatureKit";

rg::BlitFilter ToRgFilter(BlitFilter filter) noexcept
{
    return filter == BlitFilter::Nearest ? rg::BlitFilter::Nearest : rg::BlitFilter::Linear;
}

} // namespace

void Frame::ReportImpl(std::string_view text, bool warning) const
{
    if (m_context != nullptr && m_context->builder != nullptr) {
        if (warning) {
            m_context->builder->ReportWarningOnce(kLogCategory, text);
        } else {
            m_context->builder->ReportOnce(kLogCategory, text);
        }
        return;
    }
    if (!m_noContextLogged) {
        m_noContextLogged = true;
        if (warning) {
            GTE_LOG_WARNING(kLogCategory, std::string(text));
        } else {
            GTE_LOG_ERROR(kLogCategory, std::string(text));
        }
    }
}

void Frame::ReportOnce(std::string_view text) const
{
    ReportImpl(text, false);
}

void Frame::ReportWarningOnce(std::string_view text) const
{
    ReportImpl(text, true);
}

float Frame::DeltaSeconds() const noexcept
{
    return m_context != nullptr ? m_context->deltaSeconds : 0.0f;
}

std::span<const ClusterInstance> Frame::ClusterCandidates() const noexcept
{
    if (m_context == nullptr || m_context->core == nullptr || m_view.scope == rg::ViewScope::Shared) {
        return {};
    }
    return m_context->core->ClusterCandidates();
}

std::span<const AABB> Frame::ClusterCandidateBounds() const
{
    if (m_context == nullptr || m_context->core == nullptr || m_view.scope == rg::ViewScope::Shared) {
        return {};
    }
    return m_context->core->ClusterCandidateBounds();
}

ClusterRejectCounts Frame::ClusterRejections() const noexcept
{
    if (m_context == nullptr || m_context->core == nullptr || m_view.scope == rg::ViewScope::Shared) {
        return {};
    }
    return m_context->core->ClusterRejections();
}

bool Frame::CullSceneDraw(const Mat4& viewProjection, float paddingFraction)
{
    if (m_context == nullptr || m_context->core == nullptr) {
        return false;
    }
    return m_context->core->SetSceneDrawCull(m_view.scope, viewProjection, paddingFraction);
}

SceneCullCounts Frame::LastSceneDrawCullCounts() const noexcept
{
    if (m_context == nullptr || m_context->core == nullptr) {
        return {};
    }
    const gte::SceneCullCounts counts = m_context->core->LastSceneDrawCullCounts(m_view.scope, FrameIndex());
    return SceneCullCounts{ counts.tested, counts.culled, counts.unbounded };
}

std::optional<fx::View> Frame::PrimaryCamera(float fallbackAspect) const
{
    if (m_context == nullptr || m_context->core == nullptr) {
        return std::nullopt;
    }
    return m_context->core->PrimaryCameraView(fallbackAspect);
}

bool Frame::ViewCaptureArmed() const noexcept
{
    if (m_context == nullptr || m_context->core == nullptr) {
        return false;
    }
    return m_view.scope == rg::ViewScope::Primary && m_context->core->CaptureArmedThisFrame();
}

bool Frame::ShaderReady(ComputeShader& shader)
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        return false;
    }
    return detail::ShaderOf(shader).IsReady(*m_context->builder);
}

bool Frame::PassEnabled(rg::StaticName passName) const
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        return false;
    }
    return detail::PassEnabled(*m_context, passName);
}

bool Frame::ChannelHasEmptyViewReaders(std::string_view channelName) const
{
    if (m_context == nullptr || m_context->core == nullptr) {
        return true;
    }
    const RenderFeatureCompositor* compositor = m_context->core->GetRenderFeatureCompositor();
    if (compositor == nullptr) {
        return true;
    }
    return compositor->HasChannelConsumer(std::string(m_context->featureName) + "." + std::string(channelName), m_view.scope);
}

void Frame::Keep(Texture texture)
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("Keep() needs a frame that belongs to a render callback");
        return;
    }
    if (!Owns(texture)) {
        ReportOnce("Keep(): the texture is invalid or was made by another frame callback");
        return;
    }
    m_context->builder->KeepTextureOutput(texture.handle);
}

bool Frame::Blit(rg::StaticName passName, Texture src, Texture dst, BlitFilter filter)
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("Blit() needs a frame that belongs to a render callback");
        return false;
    }
    if (!detail::PassEnabled(*m_context, passName)) {
        return false;
    }
    if (!detail::CheckOwned(*this, src, passName, "the blit source") || !detail::CheckOwned(*this, dst, passName, "the blit target")) {
        return false;
    }
    rg::BlitSpec spec;
    spec.src = src.handle;
    spec.dst = dst.handle;
    spec.filter = ToRgFilter(filter);
    m_context->builder->AddBlitPass(passName.value, spec, m_context->event, m_context->scope, rg::RenderPassCategory::General);
    return true;
}

} // namespace gte::fx
