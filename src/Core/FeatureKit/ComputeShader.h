#pragma once

#include <memory>

namespace gte::rg {
class Shader;
} // namespace gte::rg

namespace gte::fx {

class ComputeShader;

namespace detail {
rg::Shader& ShaderOf(ComputeShader& shader) noexcept;
} // namespace detail

// A compute shader loaded by name: ComputeShader{"Blur"} uses shaders/Blur.comp.spv. Built lazily, never throws.
class ComputeShader {
public:
    explicit ComputeShader(const char* baseName);
    ~ComputeShader();

    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;
    ComputeShader(ComputeShader&&) = delete;
    ComputeShader& operator=(ComputeShader&&) = delete;

private:
    friend rg::Shader& detail::ShaderOf(ComputeShader& shader) noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
