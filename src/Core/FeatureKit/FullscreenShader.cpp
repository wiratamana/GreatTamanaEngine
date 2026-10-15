#include "FullscreenShader.h"

#include "../../Renderer/RenderGraph/Shader.h"

namespace gte::fx {

struct FullscreenShader::Impl {
    Impl(const char* baseName, FullscreenBlend blendMode)
        : shader(baseName, rg::ShaderKind::Graphics)
        , blend(blendMode)
    {
    }

    rg::Shader shader;
    FullscreenBlend blend;
};

FullscreenShader::FullscreenShader(const char* baseName, FullscreenBlend blend)
    : m_impl(std::make_unique<Impl>(baseName, blend))
{
}

FullscreenShader::~FullscreenShader() = default;

namespace detail {

rg::Shader& ShaderOf(FullscreenShader& shader) noexcept
{
    return shader.m_impl->shader;
}

FullscreenBlend BlendOf(const FullscreenShader& shader) noexcept
{
    return shader.m_impl->blend;
}

} // namespace detail

} // namespace gte::fx
