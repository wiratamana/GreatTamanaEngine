#include "ComputeShader.h"

#include "../../Renderer/RenderGraph/Shader.h"

namespace gte::fx {

struct ComputeShader::Impl {
    explicit Impl(const char* baseName)
        : shader(baseName, rg::ShaderKind::Compute)
    {
    }

    rg::Shader shader;
};

ComputeShader::ComputeShader(const char* baseName)
    : m_impl(std::make_unique<Impl>(baseName))
{
}

ComputeShader::~ComputeShader() = default;

namespace detail {

rg::Shader& ShaderOf(ComputeShader& shader) noexcept
{
    return shader.m_impl->shader;
}

} // namespace detail

} // namespace gte::fx
