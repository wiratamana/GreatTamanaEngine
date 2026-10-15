#pragma once

// Name-based binding of resources to a shader's reflected descriptor set 0.
// Pure logic: access (read/write) is inferred from reflection, no GPU calls.

#include "RenderGraphBuilder.h"
#include "../Vulkan/ShaderReflection.h"

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

// providedSize 0 means no push constants were supplied.
std::optional<std::string> ValidatePushConstantSize(
    bool shaderHasPushConstants, std::uint32_t reflectedSize, std::uint32_t providedSize);

// Declares the layout, reads/writes and bindings. Returns an error (declaring nothing) if the plan
// holds an entry that cannot be expressed.
std::optional<std::string> DeclareBindings(
    RenderGraphBuilder::PassBuilder& pass, const BindingPlan& plan, DescriptorSetLayoutHandle layout);

} // namespace gte::rg
