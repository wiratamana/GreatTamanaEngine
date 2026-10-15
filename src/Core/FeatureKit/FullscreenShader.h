#pragma once

#include <cstdint>
#include <memory>

namespace gte::rg {
class Shader;
} // namespace gte::rg

namespace gte::fx {

// How a fullscreen draw combines with what is already in its target.
enum class FullscreenBlend : std::uint8_t { Opaque, AlphaBlend, Additive };

class FullscreenShader;

namespace detail {
rg::Shader& ShaderOf(FullscreenShader& shader) noexcept;
FullscreenBlend BlendOf(const FullscreenShader& shader) noexcept;
} // namespace detail

// A vertex + fragment shader pair loaded by name: FullscreenShader{"Tint"} uses shaders/Tint.vert.spv and
// shaders/Tint.frag.spv. Sampler layout and push size come from the SPIR-V. Built lazily, never throws.
class FullscreenShader {
public:
    explicit FullscreenShader(const char* baseName, FullscreenBlend blend = FullscreenBlend::Opaque);
    ~FullscreenShader();

    FullscreenShader(const FullscreenShader&) = delete;
    FullscreenShader& operator=(const FullscreenShader&) = delete;
    FullscreenShader(FullscreenShader&&) = delete;
    FullscreenShader& operator=(FullscreenShader&&) = delete;

private:
    friend rg::Shader& detail::ShaderOf(FullscreenShader& shader) noexcept;
    friend FullscreenBlend detail::BlendOf(const FullscreenShader& shader) noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
