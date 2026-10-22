#include "Frame.h"

#include "ComputeShader.h"
#include "VerbSupport.h"
#include "../Core.h"
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

void Frame::ReportOnce(std::string_view text) const
{
    if (m_context != nullptr && m_context->builder != nullptr) {
        m_context->builder->ReportOnce(kLogCategory, text);
        return;
    }
    if (!m_noContextLogged) {
        m_noContextLogged = true;
        GTE_LOG_ERROR(kLogCategory, std::string(text));
    }
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
