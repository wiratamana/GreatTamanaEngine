#include "Shader.h"

#include "BoundedLruIndex.h"
#include "BoundedOnceSet.h"
#include "BoundedRetryGates.h"
#include "BuildRetryGate.h"
#include "RenderGraphBuilder.h"
#include "ShaderBindings.h"
#include "../Compute/ComputePipeline.h"
#include "../Pipeline.h"
#include "../Renderer.h"
#include "../ShaderFile.h"
#include "../Vulkan/ReflectedSetLayouts.h"
#include "../Vulkan/ShaderReflection.h"
#include "../Vulkan/ShaderStageMask.h"
#include "../../Core/Logging.h"

#include <array>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gte::rg {

namespace {

constexpr const char* kLogCategory = "Shader";

// Pipelines kept per shader. A shader is normally drawn into one or two target setups; the cap bounds growth.
constexpr std::size_t kMaxGraphicsPipelines = 8;

// An evicted pipeline may still be referenced by commands recorded this frame or by frames in flight.
constexpr std::uint64_t kRetireFrames = 3;

struct FullscreenKeyHash {
    std::size_t operator()(const FullscreenKey& key) const noexcept
    {
        std::size_t hash = 1469598103934665603ull;
        const auto mix = [&hash](std::size_t value) { hash = (hash ^ value) * 1099511628211ull; };
        for (std::size_t i = 0; i < key.colorCount; ++i) {
            mix(static_cast<std::size_t>(key.colorFormats[i]));
        }
        mix(key.colorCount);
        mix(static_cast<std::size_t>(key.depthFormat));
        mix(static_cast<std::size_t>(key.blend));
        mix(static_cast<std::size_t>(key.depthCompare));
        mix((key.depthTest ? 1u : 0u) | (key.depthWrite ? 2u : 0u));
        return hash;
    }
};

using PipelineGates = BoundedRetryGates<FullscreenKey, FullscreenKeyHash, std::equal_to<FullscreenKey>, kMaxGraphicsPipelines>;

} // namespace

struct Shader::Impl {
    struct Retired {
        std::unique_ptr<Pipeline> pipeline;
        std::uint64_t frame = 0;
    };

    // Reflection, set-0 layout and the bounded cache of fullscreen pipeline variants.
    struct Graphics {
        ShaderReflectionResult reflection;
        ReflectedSetLayouts layouts;
        ShaderStageMask pushStages = ShaderStageMask::None;
        BoundedLruIndex<FullscreenKey, kMaxGraphicsPipelines> index;
        std::array<std::unique_ptr<Pipeline>, kMaxGraphicsPipelines> pipelines;
        std::vector<Retired> retired;
        PipelineGates gates;
    };

    ShaderKind kind = ShaderKind::Compute;
    std::string label;
    std::vector<ShaderStageFile> stages;
    BuildRetryGate gate;
    BoundedOnceSet<64> onceKeys;
    Renderer* renderer = nullptr; // Set by the first successful build.
    DescriptorSetLayoutHandle layout = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputePipeline> compute;
    std::optional<Graphics> graphics;

    bool IsBuilt() const noexcept { return compute.has_value() || graphics.has_value(); }

    VkDescriptorSetLayout NativeLayout() const noexcept
    {
        if (compute.has_value()) {
            return compute->ReflectedDescriptorSetLayout(0);
        }
        return graphics.has_value() ? graphics->layouts.Native(0) : VK_NULL_HANDLE;
    }

    void ReleaseRetired(std::uint64_t frame)
    {
        if (!graphics.has_value()) {
            return;
        }
        std::erase_if(graphics->retired, [frame](const Retired& r) { return frame >= r.frame + kRetireFrames; });
    }
};

void Shader::BuildCompute(Impl& impl, Renderer& renderer)
{
    ComputePipeline built = renderer.CreateComputePipeline(impl.stages.front().spirvPath);
    ValidateBindingNames(built.ReflectedBindings(), impl.label);
    const DescriptorSetLayoutHandle layout = built.ReflectedDescriptorSetLayoutHandle(0);
    if (layout == DescriptorSetLayoutHandle::Invalid) {
        throw std::runtime_error("shader '" + impl.label + "' declares no descriptor set 0");
    }
    impl.compute.emplace(std::move(built));
    impl.layout = layout;
}

void Shader::BuildGraphics(Impl& impl, Renderer& renderer)
{
    ShaderReflectionResult reflection = ReflectShader(impl.stages);
    ValidateBindingNames(reflection.descriptorBindings, impl.label);
    if (const auto error = ValidateFullscreenReflection(reflection)) {
        throw std::runtime_error("shader '" + impl.label + "': " + *error);
    }
    ReflectedSetLayouts layouts = renderer.CreateReflectedSetLayouts(reflection.descriptorBindings, impl.label);
    const DescriptorSetLayoutHandle layout = layouts.Handle(0);
    if (layout == DescriptorSetLayoutHandle::Invalid) {
        throw std::runtime_error("shader '" + impl.label + "' declares no descriptor set 0");
    }

    Impl::Graphics graphics;
    graphics.pushStages = ToShaderStageMask(reflection.pushConstantStages);
    graphics.reflection = std::move(reflection);
    graphics.layouts = std::move(layouts);
    impl.graphics.emplace(std::move(graphics));
    impl.layout = layout;
}

Pipeline Shader::BuildFullscreenPipeline(const Impl& impl, Renderer& renderer, const FullscreenKey& key)
{
    const Impl::Graphics& graphics = *impl.graphics;

    PipelineDesc desc{};
    desc.blendMode = key.blend;
    desc.vertexLayout = VertexLayout::None;
    desc.cullMode = CullMode::None;
    desc.depthTest = key.depthTest;
    desc.depthWrite = key.depthWrite;
    desc.depthCompare = key.depthCompare;
    desc.colorAttachmentCount = key.colorCount;
    for (std::size_t i = 0; i < key.colorCount; ++i) {
        desc.colorAttachmentFormats[i] = key.colorFormats[i];
    }
    desc.depthAttachmentFormat = key.depthFormat;
    desc.debugName = impl.label.c_str();

    const DescriptorSetLayoutHandle setLayouts[] = { impl.layout };
    desc.layout.extraSetLayouts = std::span<const DescriptorSetLayoutHandle>(setLayouts);
    if (graphics.reflection.hasPushConstantRange) {
        desc.layout.pushConstantByteSize = graphics.reflection.pushConstantRange.size;
        desc.layout.pushConstantStages = graphics.pushStages;
    }

    const std::vector<std::byte> vertex = ReadShaderFileBytes(impl.stages[0].spirvPath);
    const std::vector<std::byte> fragment = ReadShaderFileBytes(impl.stages[1].spirvPath);
    return renderer.CreatePipeline(desc, vertex, fragment);
}

Shader::Shader(const char* baseName, ShaderKind kind)
    : m_impl(std::make_unique<Impl>())
{
    const std::string base = std::string("shaders/") + (baseName != nullptr ? baseName : "");
    Impl& impl = *m_impl;
    impl.kind = kind;
    if (kind == ShaderKind::Compute) {
        impl.label = base + ".comp.spv";
        impl.stages.push_back(ShaderStageFile{ VK_SHADER_STAGE_COMPUTE_BIT, impl.label });
    } else {
        impl.label = base;
        impl.stages.push_back(ShaderStageFile{ VK_SHADER_STAGE_VERTEX_BIT, base + ".vert.spv" });
        impl.stages.push_back(ShaderStageFile{ VK_SHADER_STAGE_FRAGMENT_BIT, base + ".frag.spv" });
    }
}

Shader::~Shader()
{
    const Impl& impl = *m_impl;
    if (impl.renderer != nullptr && impl.IsBuilt()) {
        impl.renderer->QueueBindingRelease(impl.NativeLayout());
    }
}

bool Shader::EnsureBuilt(RenderGraphBuilder& builder)
{
    Impl& impl = *m_impl;
    if (impl.IsBuilt()) {
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

    bool failed = false;
    std::string failure;
    try {
        if (impl.kind == ShaderKind::Compute) {
            BuildCompute(impl, *renderer);
        } else {
            BuildGraphics(impl, *renderer);
        }
    } catch (const std::exception& e) {
        failed = true;
        failure = e.what();
    } catch (...) {
        failed = true;
        failure = "unknown error while building '" + impl.label + "'";
    }

    if (failed) {
        impl.compute.reset();
        impl.graphics.reset();
        impl.layout = DescriptorSetLayoutHandle::Invalid;
        if (impl.gate.OnFailure(frame) == BuildRetryGate::FailureKind::First) {
            GTE_LOG_WARNING(kLogCategory, "Build failed, retrying on a timer: " + failure);
        }
        return false;
    }

    impl.renderer = renderer;
    if (impl.gate.OnSuccess()) {
        GTE_LOG_INFO(kLogCategory, "Recovered: " + impl.label);
    }
    return true;
}

ShaderKind Shader::Kind() const noexcept
{
    return m_impl->kind;
}

std::span<const ReflectedDescriptorBinding> Shader::ReflectedBindings() const noexcept
{
    const Impl& impl = *m_impl;
    if (impl.compute.has_value()) {
        return impl.compute->ReflectedBindings();
    }
    return impl.graphics.has_value() ? std::span<const ReflectedDescriptorBinding>(impl.graphics->reflection.descriptorBindings)
                                     : std::span<const ReflectedDescriptorBinding>{};
}

std::uint32_t Shader::PushConstantSize() const noexcept
{
    const Impl& impl = *m_impl;
    if (impl.compute.has_value()) {
        return impl.compute->PushConstantSize();
    }
    return impl.graphics.has_value() && impl.graphics->reflection.hasPushConstantRange
        ? impl.graphics->reflection.pushConstantRange.size
        : 0;
}

ShaderStageMask Shader::PushStages() const noexcept
{
    const Impl& impl = *m_impl;
    if (impl.graphics.has_value()) {
        return impl.graphics->pushStages;
    }
    return impl.compute.has_value() ? ShaderStageMask::Compute : ShaderStageMask::None;
}

DescriptorSetLayoutHandle Shader::Layout() const noexcept
{
    return m_impl->layout;
}

const ComputePipeline* Shader::Compute() const noexcept
{
    return m_impl->compute.has_value() ? &*m_impl->compute : nullptr;
}

const Pipeline* Shader::GraphicsPipeline(RenderGraphBuilder& builder, const FullscreenKey& key)
{
    Impl& impl = *m_impl;
    if (!impl.graphics.has_value() || impl.renderer == nullptr) {
        return nullptr;
    }
    Impl::Graphics& graphics = *impl.graphics;
    const std::uint64_t frame = builder.FrameIndex();
    impl.ReleaseRetired(frame);

    if (!graphics.gates.ShouldAttempt(key, frame)) {
        return nullptr;
    }
    const auto touched = graphics.index.Touch(key, frame);
    if (touched.outcome == BoundedLruIndex<FullscreenKey, kMaxGraphicsPipelines>::Outcome::Hit) {
        return graphics.pipelines[touched.slot].get();
    }

    bool failed = false;
    std::string failure;
    try {
        if (graphics.pipelines[touched.slot] != nullptr) {
            graphics.retired.push_back(Impl::Retired{ std::move(graphics.pipelines[touched.slot]), frame });
        }
        graphics.pipelines[touched.slot] =
            std::make_unique<Pipeline>(BuildFullscreenPipeline(impl, *impl.renderer, key));
    } catch (const std::exception& e) {
        failed = true;
        failure = e.what();
    } catch (...) {
        failed = true;
        failure = "unknown error while building a pipeline for '" + impl.label + "'";
    }

    if (failed) {
        graphics.pipelines[touched.slot].reset();
        graphics.index.Release(touched.slot);
        if (graphics.gates.OnFailure(key, frame) == BuildRetryGate::FailureKind::First) {
            GTE_LOG_WARNING(kLogCategory, "Pipeline build failed, retrying on a timer: " + failure);
        }
        return nullptr;
    }
    if (graphics.gates.OnSuccess(key)) {
        GTE_LOG_INFO(kLogCategory, "Pipeline recovered: " + impl.label);
    }
    return graphics.pipelines[touched.slot].get();
}

std::size_t Shader::RetiredPipelineCount() const noexcept
{
    return m_impl->graphics.has_value() ? m_impl->graphics->retired.size() : 0;
}

OnceLog Shader::NoteOnce(std::string_view key)
{
    const auto result = m_impl->onceKeys.Note(key);
    if (!BoundedOnceSet<64>::ShouldLog(result)) {
        return OnceLog::Skip;
    }
    return result == BoundedOnceSet<64>::Result::First ? OnceLog::Message : OnceLog::SuppressedNotice;
}

std::string_view Shader::Label() const noexcept
{
    return m_impl->label;
}

} // namespace gte::rg
