#include "Shader.h"

#include "BoundedOnceSet.h"
#include "BuildRetryGate.h"
#include "RenderGraphBuilder.h"
#include "../Compute/ComputePipeline.h"
#include "../Renderer.h"
#include "../Vulkan/ShaderReflection.h"
#include "../../Core/Logging.h"

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace gte::rg {

namespace {
constexpr const char* kLogCategory = "Shader";
} // namespace

struct Shader::Impl {
    std::string path;
    BuildRetryGate gate;
    std::optional<ComputePipeline> pipeline;
    DescriptorSetLayoutHandle layout = DescriptorSetLayoutHandle::Invalid;
    Renderer* renderer = nullptr; // Set by the first successful build.
    BoundedOnceSet<64> onceKeys;
};

Shader::Shader(const char* baseName)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->path = std::string("shaders/") + (baseName != nullptr ? baseName : "") + ".comp.spv";
}

Shader::~Shader()
{
    const Impl& impl = *m_impl;
    if (impl.renderer != nullptr && impl.pipeline.has_value()) {
        impl.renderer->QueueComputeBindingRelease(impl.pipeline->ReflectedDescriptorSetLayout(0));
    }
}

bool Shader::EnsureBuilt(RenderGraphBuilder& builder)
{
    Impl& impl = *m_impl;
    if (impl.pipeline.has_value()) {
        return true;
    }
    Renderer* renderer = builder.RendererOrNull();
    if (renderer == nullptr) {
        return false;
    }
    const std::uint64_t frame = builder.FrameIndex();
    if (!impl.gate.ShouldAttempt(frame)) {
        return false;
    }

    std::string failure;
    try {
        ComputePipeline built = renderer->CreateComputePipeline(impl.path);
        ValidateBindingNames(built.ReflectedBindings(), impl.path);
        impl.pipeline.emplace(std::move(built));
        impl.layout = impl.pipeline->ReflectedDescriptorSetLayoutHandle(0);
        if (impl.layout == DescriptorSetLayoutHandle::Invalid) {
            impl.pipeline.reset();
            failure = "shader '" + impl.path + "' declares no descriptor set 0";
        }
    } catch (const std::exception& e) {
        impl.pipeline.reset();
        failure = e.what();
    } catch (...) {
        impl.pipeline.reset();
        failure = "unknown error while building '" + impl.path + "'";
    }

    if (!impl.pipeline.has_value()) {
        impl.layout = DescriptorSetLayoutHandle::Invalid;
        if (impl.gate.OnFailure(frame) == BuildRetryGate::FailureKind::First) {
            GTE_LOG_WARNING(kLogCategory, "Build failed, retrying on a timer: " + failure);
        }
        return false;
    }

    impl.renderer = renderer;
    if (impl.gate.OnSuccess()) {
        GTE_LOG_INFO(kLogCategory, "Recovered: " + impl.path);
    }
    return true;
}

std::span<const ReflectedDescriptorBinding> Shader::ReflectedBindings() const noexcept
{
    return m_impl->pipeline.has_value() ? m_impl->pipeline->ReflectedBindings()
                                        : std::span<const ReflectedDescriptorBinding>{};
}

std::uint32_t Shader::PushConstantSize() const noexcept
{
    return m_impl->pipeline.has_value() ? m_impl->pipeline->PushConstantSize() : 0;
}

const ComputePipeline* Shader::Pipeline() const noexcept
{
    return m_impl->pipeline.has_value() ? &*m_impl->pipeline : nullptr;
}

DescriptorSetLayoutHandle Shader::Layout() const noexcept
{
    return m_impl->layout;
}

bool Shader::NoteOnce(std::string_view key)
{
    return m_impl->onceKeys.Note(key) == BoundedOnceSet<64>::Result::First;
}

} // namespace gte::rg
