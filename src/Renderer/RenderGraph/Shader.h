#pragma once

#include "../../Core/GraphicsHandles.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gte {
class ComputePipeline;
struct ReflectedDescriptorBinding;
} // namespace gte

namespace gte::rg {

class RenderGraphBuilder;

// Compute shader loaded from "shaders/<baseName>.comp.spv"; the pipeline is built lazily and a failed
// build retries on a cooldown, never throwing. The Renderer must outlive it; idle the device before destroying.
class Shader {
public:
    explicit Shader(const char* baseName);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) = delete;
    Shader& operator=(Shader&&) = delete;

private:
    friend class ComputePass;

    struct Impl;

    // True when the pipeline is ready. False (silently) when there is no renderer or a retry is cooling down.
    bool EnsureBuilt(RenderGraphBuilder& builder);

    std::span<const ReflectedDescriptorBinding> ReflectedBindings() const noexcept;
    std::uint32_t PushConstantSize() const noexcept;
    const ComputePipeline* Pipeline() const noexcept;
    DescriptorSetLayoutHandle Layout() const noexcept;

    // True the first time `key` is seen, so a message is logged once per key.
    bool NoteOnce(std::string_view key);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::rg
