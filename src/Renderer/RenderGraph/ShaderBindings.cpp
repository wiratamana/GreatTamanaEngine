#include "ShaderBindings.h"

#include <algorithm>
#include <cassert>

namespace gte::rg {

namespace {

constexpr std::uint32_t kNameableSet = 0;

bool IsTextureKind(BindingResourceKind kind) noexcept
{
    return kind == BindingResourceKind::Texture || kind == BindingResourceKind::DepthTexture;
}

bool IsBufferKind(BindingResourceKind kind) noexcept
{
    return kind == BindingResourceKind::Buffer || kind == BindingResourceKind::TrackedBuffer;
}

std::string Quote(std::string_view name)
{
    return "'" + std::string(name) + "'";
}

// Empty when the resource kind fits the descriptor type, otherwise the reason.
std::string KindMismatch(VkDescriptorType type, BindingResourceKind kind)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return IsTextureKind(kind) ? std::string{} : "is a sampler: supply a texture, not a buffer";
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        if (IsBufferKind(kind)) {
            return "is a storage image: supply a texture, not a buffer";
        }
        return kind == BindingResourceKind::DepthTexture ? "is a storage image: a depth view is only valid on a sampler"
                                                         : std::string{};
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return IsBufferKind(kind) ? std::string{} : "is a storage buffer: supply a buffer, not a texture";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        if (kind == BindingResourceKind::TrackedBuffer) {
            return "is a uniform buffer: a graph buffer binds storage buffers only";
        }
        return kind == BindingResourceKind::Buffer ? std::string{} : "is a uniform buffer: supply a buffer, not a texture";
    default:
        return "has a descriptor type that name binding does not support";
    }
}

} // namespace

std::string ValidNamesText(std::span<const ReflectedDescriptorBinding> reflected)
{
    std::string text = "valid names: ";
    bool first = true;
    for (const ReflectedDescriptorBinding& binding : reflected) {
        if (binding.set != kNameableSet || binding.name.empty()) {
            continue;
        }
        if (!first) {
            text += ", ";
        }
        text += binding.name;
        first = false;
    }
    return first ? text + "(none)" : text;
}

BindingPlanResult PlanBindings(std::span<const ReflectedDescriptorBinding> reflected,
    std::span<const BindingArgument> args, const DescribeTextureFn& describeTexture, const DescribeBufferFn& describeBuffer)
{
    std::vector<std::string> problems;
    std::vector<const ReflectedDescriptorBinding*> slots; // Set-0 bindings, reflected order.
    for (const ReflectedDescriptorBinding& binding : reflected) {
        if (binding.set == kNameableSet) {
            slots.push_back(&binding);
        }
    }

    std::vector<std::uint8_t> supplied(slots.size(), 0);
    std::vector<const BindingArgument*> matched(slots.size(), nullptr);

    for (const ReflectedDescriptorBinding* slot : slots) {
        if (slot->name.empty()) {
            problems.push_back("set 0 binding " + std::to_string(slot->binding)
                + " has no name in the shader (anonymous block): give it an instance name");
        } else if (slot->count > 1) {
            problems.push_back(Quote(slot->name) + " is a descriptor array: not supported by name binding");
        }
    }

    for (const BindingArgument& arg : args) {
        const auto found = std::find_if(slots.begin(), slots.end(),
            [&arg](const ReflectedDescriptorBinding* slot) { return !slot->name.empty() && slot->name == arg.name; });
        if (found == slots.end()) {
            problems.push_back("unknown name " + Quote(arg.name));
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(found - slots.begin());
        const ReflectedDescriptorBinding& slot = **found;
        if (supplied[index] != 0) {
            problems.push_back(Quote(arg.name) + " was bound twice");
            continue;
        }
        supplied[index] = 1;

        if (const std::string mismatch = KindMismatch(slot.type, arg.kind); !mismatch.empty()) {
            problems.push_back(Quote(arg.name) + " " + mismatch);
            continue;
        }
        const bool invalid = IsTextureKind(arg.kind)
            ? !arg.texture.IsValid()
            : (arg.kind == BindingResourceKind::TrackedBuffer ? !arg.trackedBuffer.IsValid()
                                                              : arg.buffer == BufferObjectHandle::Invalid);
        if (invalid) {
            problems.push_back(Quote(arg.name) + " was given an invalid resource handle");
            continue;
        }
        if (arg.kind == BindingResourceKind::TrackedBuffer && describeBuffer) {
            const std::optional<BufferDesc> bufferDesc = describeBuffer(arg.trackedBuffer);
            if (!bufferDesc.has_value()) {
                problems.push_back(Quote(arg.name) + " was given a buffer the graph does not know");
                continue;
            }
            // BufferUsage::None means the usage is not declared (every import): nothing to check.
            if (bufferDesc->usage != BufferUsage::None && !HasUsage(bufferDesc->usage, BufferUsage::StorageBuffer)) {
                problems.push_back(Quote(arg.name) + " was given a buffer without storage usage");
                continue;
            }
        }
        const std::optional<TextureDesc> desc =
            IsTextureKind(arg.kind) && describeTexture ? describeTexture(arg.texture) : std::nullopt;
        if (arg.kind == BindingResourceKind::DepthTexture && (!desc.has_value() || !desc->hasDepth)) {
            problems.push_back(Quote(arg.name) + " was given a depth view of a texture without a depth aspect");
            continue;
        }
        if (arg.kind == BindingResourceKind::Texture && desc.has_value() && !desc->hasColor) {
            problems.push_back(Quote(arg.name)
                + (slot.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                        ? " is a depth-only texture: use rg::Depth() to sample it"
                        : " is a depth-only texture: it cannot be bound to a storage image"));
            continue;
        }
        matched[index] = &arg;
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (supplied[i] == 0 && !slots[i]->name.empty() && slots[i]->count <= 1) {
            problems.push_back(Quote(slots[i]->name) + " was never bound");
        }
    }

    BindingPlanResult result;
    if (!problems.empty()) {
        const std::string suffix = "; " + ValidNamesText(reflected);
        for (const std::string& problem : problems) {
            if (!result.error.empty()) {
                result.error += '\n';
            }
            result.error += problem + suffix;
        }
        return result;
    }

    result.plan.bindings.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const BindingArgument& arg = *matched[i];
        PlannedBinding planned;
        planned.binding = slots[i]->binding;
        planned.type = slots[i]->type;
        planned.access = slots[i]->access;
        planned.kind = arg.kind;
        planned.texture = arg.texture;
        planned.buffer = arg.buffer;
        planned.trackedBuffer = arg.trackedBuffer;
        result.plan.bindings.push_back(planned);
    }
    return result;
}

std::optional<std::string> ValidatePushConstantSize(std::uint32_t reflectedSize, std::uint32_t providedSize)
{
    if (reflectedSize != 0 && providedSize == 0) {
        return "shader declares a " + std::to_string(reflectedSize) + "-byte push-constant block but none was supplied";
    }
    if (reflectedSize == 0 && providedSize != 0) {
        return "shader has no push-constant block but " + std::to_string(providedSize) + " bytes were supplied";
    }
    if (providedSize != reflectedSize) {
        return "push-constant size mismatch: shader declares " + std::to_string(reflectedSize) + " bytes, got "
            + std::to_string(providedSize);
    }
    return std::nullopt;
}

void DeclareBindings(RenderGraphBuilder::PassBuilder& pass, const BindingPlan& plan, DescriptorSetLayoutHandle layout)
{
    assert(layout != DescriptorSetLayoutHandle::Invalid && "a built shader always has a layout");
    pass.BindDescriptorSetLayout(layout);
    for (const PlannedBinding& binding : plan.bindings) {
        switch (binding.type) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            if (binding.kind == BindingResourceKind::DepthTexture) {
                pass.SampleDepth(binding.binding, binding.texture);
            } else {
                pass.SampleTexture(binding.binding, binding.texture);
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            if (binding.access == ReflectedAccess::WriteOnly) {
                pass.WriteStorageImage(binding.binding, binding.texture);
            } else if (binding.access == ReflectedAccess::ReadOnly) {
                pass.ReadTexture(binding.texture, ResourceAccess::ComputeShaderRead, /*isDepthResource=*/false);
                pass.BindStorageImage(binding.binding, binding.texture);
            } else {
                pass.ReadTexture(binding.texture, ResourceAccess::ComputeShaderRead, /*isDepthResource=*/false);
                pass.WriteStorageImage(binding.binding, binding.texture);
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (binding.kind == BindingResourceKind::TrackedBuffer) {
                switch (binding.access) {
                case ReflectedAccess::ReadOnly: pass.ReadStorageBuffer(binding.binding, binding.trackedBuffer); break;
                case ReflectedAccess::WriteOnly: pass.WriteStorageBuffer(binding.binding, binding.trackedBuffer); break;
                case ReflectedAccess::ReadWrite: pass.ReadWriteStorageBuffer(binding.binding, binding.trackedBuffer); break;
                }
            } else {
                pass.ReadStorageBuffer(binding.binding, binding.buffer); // External buffers are not graph-tracked.
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            pass.BindUniformBuffer(binding.binding, binding.buffer);
            break;
        default:
            assert(false && "PlanBindings rejects unsupported descriptor types");
            break;
        }
    }
}

std::optional<std::string> ValidateFullscreenReflection(const ShaderReflectionResult& reflection)
{
    bool hasSetZero = false;
    for (const ReflectedDescriptorBinding& binding : reflection.descriptorBindings) {
        if (binding.set != kNameableSet) {
            return "binding " + std::to_string(binding.binding) + " is in set " + std::to_string(binding.set)
                + ": fullscreen passes use set 0 only";
        }
        if (binding.type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && binding.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            return "binding " + std::to_string(binding.binding) + " is not a combined image sampler or uniform buffer";
        }
        hasSetZero = true;
    }
    if (!hasSetZero) {
        return std::string("the shader declares no descriptor set 0");
    }
    if (reflection.hasPushConstantRange && reflection.pushConstantRange.offset != 0) {
        return std::string("the push-constant block must start at offset 0");
    }
    return std::nullopt;
}

std::optional<std::string> ValidateFullscreenTarget(const std::optional<TextureDesc>& target)
{
    if (!target.has_value()) {
        return std::string("the target is missing or not a valid texture");
    }
    if (target->width == 0 || target->height == 0) {
        return std::string("the target has a zero extent");
    }
    if (target->format == PixelFormat::Undefined) {
        return std::string("the target format is Undefined");
    }
    if (!target->hasColor) {
        return std::string("the target is a depth-only texture: a fullscreen pass writes color targets");
    }
    return std::nullopt;
}

FullscreenAttachmentPlan PlanFullscreenAttachments(std::span<const std::optional<TextureDesc>> targets,
    const FullscreenDepthRequest& depth, PixelFormat rendererDepthFormat, BlendMode blend)
{
    FullscreenAttachmentPlan result;
    if (targets.empty()) {
        result.error = "no color target was set";
        return result;
    }
    if (targets.size() > FullscreenKey::kMaxTargets) {
        result.error = "more than " + std::to_string(FullscreenKey::kMaxTargets) + " color targets";
        return result;
    }

    FullscreenKey key;
    key.blend = blend;
    key.colorCount = static_cast<std::uint8_t>(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const std::optional<TextureDesc>& color = targets[i];
        if (const auto error = ValidateFullscreenTarget(color)) {
            result.error = "target " + std::to_string(i) + ": " + *error;
            return result;
        }
        if (color->width != targets[0]->width || color->height != targets[0]->height) {
            result.error = "target " + std::to_string(i) + " has a different extent than target 0";
            return result;
        }
        key.colorFormats[i] = color->format;
    }

    if (depth.enabled) {
        if (!depth.desc.has_value() || !depth.desc->hasDepth) {
            result.error = "the depth test texture is missing or has no depth aspect";
            return result;
        }
        if (depth.desc->width != targets[0]->width || depth.desc->height != targets[0]->height) {
            result.error = "the depth test texture has a different extent than the color targets";
            return result;
        }
        key.depthFormat = depth.desc->depthFormat != PixelFormat::Undefined ? depth.desc->depthFormat : rendererDepthFormat;
        key.depthTest = true;
        key.depthWrite = depth.write;
        key.depthCompare = depth.compare;
    }
    result.key = key;
    return result;
}

void DeclareFullscreen(RenderGraphBuilder::PassBuilder& pass, const BindingPlan& plan, DescriptorSetLayoutHandle layout,
    std::span<const TextureHandle> targets, const std::optional<std::array<float, 4>>& clearColor,
    std::span<const TextureHandle> depthAcknowledgements, const std::optional<TextureHandle>& depthTest)
{
    DeclareBindings(pass, plan, layout);
    for (const TextureHandle target : targets) {
        pass.WriteColorAttachment(target, clearColor);
    }
    if (depthTest.has_value()) {
        pass.WriteDepthStencilAttachment(*depthTest);
    }
    for (const TextureHandle depth : depthAcknowledgements) {
        pass.ReadTexture(depth, ResourceAccess::ShaderRead, /*isDepthResource=*/true);
    }
}

} // namespace gte::rg
