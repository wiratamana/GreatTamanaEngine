#pragma once

// Name-based binding of resources to a shader's reflected descriptor set 0.
// Pure logic: access (read/write) is inferred from reflection, no GPU calls.

#include "RenderGraphBuilder.h"
#include "../Vulkan/ShaderReflection.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte::rg {

enum class BindingResourceKind : std::uint8_t { Texture, DepthTexture, Buffer };

// One caller-supplied resource, addressed by the shader's variable name.
struct BindingArgument {
    std::string_view name;
    BindingResourceKind kind = BindingResourceKind::Texture;
    TextureHandle texture{};
    BufferObjectHandle buffer = BufferObjectHandle::Invalid;
};

// A validated binding: slot number plus the access the shader performs.
struct PlannedBinding {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    ReflectedAccess access = ReflectedAccess::ReadWrite;
    BindingResourceKind kind = BindingResourceKind::Texture;
    TextureHandle texture{};
    BufferObjectHandle buffer = BufferObjectHandle::Invalid;
};

struct BindingPlan {
    std::vector<PlannedBinding> bindings; // In reflected order.
};

struct BindingPlanResult {
    BindingPlan plan;   // Empty when an error was found.
    std::string error;  // One line per problem; empty on success.

    bool Ok() const noexcept { return error.empty(); }
};

using DescribeTextureFn = std::function<std::optional<TextureDesc>(TextureHandle)>;

// "valid names: a, b, c" over the nameable (set 0) bindings.
std::string ValidNamesText(std::span<const ReflectedDescriptorBinding> reflected);

// Matches `args` against the reflected set-0 bindings and collects every problem.
// Other sets are engine-owned and never nameable.
BindingPlanResult PlanBindings(std::span<const ReflectedDescriptorBinding> reflected,
    std::span<const BindingArgument> args, const DescribeTextureFn& describeTexture);

// A size of 0 means no push-constant block (reflected) or none supplied (provided).
std::optional<std::string> ValidatePushConstantSize(std::uint32_t reflectedSize, std::uint32_t providedSize);

// Declares the layout, reads/writes and bindings of an already validated plan.
void DeclareBindings(RenderGraphBuilder::PassBuilder& pass, const BindingPlan& plan, DescriptorSetLayoutHandle layout);

// Why a graphics shader cannot back a fullscreen pass, or nullopt. Fullscreen passes use set 0 only,
// sample combined image samplers only, and a push block at offset 0.
std::optional<std::string> ValidateFullscreenReflection(const ShaderReflectionResult& reflection);

// Why `target` (the DescOf() of the pass target) cannot be rendered to, or nullopt.
std::optional<std::string> ValidateFullscreenTarget(const std::optional<TextureDesc>& target);

// Declares a fullscreen pass: bindings, the color target, and declare-only depth reads
// (`depthAcknowledgements` are sampled by no shader; the compositor requires the view's depth to be read).
void DeclareFullscreen(RenderGraphBuilder::PassBuilder& pass, const BindingPlan& plan, DescriptorSetLayoutHandle layout,
    TextureHandle target, const std::optional<std::array<float, 4>>& clearColor,
    std::span<const TextureHandle> depthAcknowledgements);

} // namespace gte::rg
