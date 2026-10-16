#include "RenderGraph.h"

#include "../GraphicsHandleRegistry.h"
#include "../Renderer.h"

#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace gte::rg {

namespace {

bool NeedsAttachments(const PassRecord& pass)
{
    if (!pass.colorAttachments.empty() || pass.arrayLayerAttachment.has_value()) {
        return true;
    }
    for (const ResourceUsage& usage : pass.writes) {
        if (usage.kind == ResourceKind::Texture && TargetsDepthState(usage.access)) {
            return true;
        }
    }
    return false;
}

// Descriptor sets of one run: freed after the GPU is done with them.
class SetLeases {
public:
    explicit SetLeases(std::function<void(VkDescriptorSet)> freeSet)
        : m_freeSet(std::move(freeSet))
    {
    }
    ~SetLeases()
    {
        for (const Lease& lease : m_leases) {
            UnregisterDescriptorSet(lease.handle);
            m_freeSet(lease.set);
        }
    }
    SetLeases(const SetLeases&) = delete;
    SetLeases& operator=(const SetLeases&) = delete;

    void Add(DescriptorSetHandle handle, VkDescriptorSet set) { m_leases.push_back(Lease{ handle, set }); }

private:
    struct Lease {
        DescriptorSetHandle handle;
        VkDescriptorSet set;
    };
    std::function<void(VkDescriptorSet)> m_freeSet;
    std::vector<Lease> m_leases;
};

} // namespace

bool RenderGraph::RunOneShot(CompiledGraphInput input, std::span<const TextureHandle> outputs,
    std::span<const OneShotFinal> finals, std::vector<std::optional<ResourceState>>& states)
{
    const CompiledGraph compiled = Compile(input, outputs);
    std::vector<PhysicalTexture> physicalTextures(input.textures.size());
    std::vector<PhysicalBuffer> physicalBuffers(input.buffers.size());
    SetLeases leases([this](VkDescriptorSet set) { m_renderer->FreeDescriptorSet(set); });

    try {
        m_renderer->ImmediateSubmit([&](CommandBuffer& commandBuffer) {
            const VkCommandBuffer cmd = RecordingAccess::Native(commandBuffer);
            for (const PassHandle& passHandle : compiled.executionOrder) {
                PassRecord& pass = input.passes[passHandle.index];
                for (const ResourceUsage& usage : pass.reads) {
                    ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers);
                }
                for (const ResourceUsage& usage : pass.writes) {
                    ApplyUsageBarrierIfNeeded(cmd, usage, input, physicalTextures, physicalBuffers);
                }

                const std::string passName = pass.name != nullptr ? pass.name : "<unnamed>";
                if (NeedsAttachments(pass) || (!pass.execute && pass.kind == PassKind::Blit)) {
                    m_onceLog.Error("OneShot", "pass '" + passName + "' needs attachments or a blit: unsupported, skipped");
                    continue;
                }

                DrawStats drawStats;
                PassContext ctx = BuildPassContext(cmd, physicalTextures, physicalBuffers, drawStats);
                if (!pass.bindings.empty()) {
                    std::vector<DescriptorWrite> writes;
                    const VkDescriptorSetLayout layout = Native(pass.computeBindingLayout);
                    if (layout == VK_NULL_HANDLE || !BuildDescriptorWrites(pass, physicalTextures, physicalBuffers, writes)) {
                        m_onceLog.Error("OneShot", "pass '" + passName + "' has unresolved bindings, skipped");
                        continue;
                    }
                    DescriptorSet set = m_renderer->AllocateDescriptorSet(layout);
                    const VkDescriptorSet raw = set.Native();
                    set.Rewrite(writes);
                    ctx.computeDescriptorSet = RegisterDescriptorSet(raw);
                    leases.Add(ctx.computeDescriptorSet, raw);
                }
                if (pass.execute && !pass.skipExecute) {
                    pass.execute(ctx);
                }
            }

            for (const OneShotFinal& final : finals) {
                if (!final.texture.IsValid() || final.texture.index >= physicalTextures.size()
                    || !physicalTextures[final.texture.index].resolved || !final.state.has_value()
                    || final.state->layout == VK_IMAGE_LAYOUT_UNDEFINED) {
                    continue;
                }
                PhysicalTexture& tex = physicalTextures[final.texture.index];
                if (RequiresBarrier(tex.colorState, *final.state)) {
                    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    EmitImageBarrier(cmd, tex.target.image, range, tex.colorState, *final.state);
                }
                tex.colorState = *final.state;
            }
        });
    } catch (const std::exception& e) {
        m_onceLog.Error("OneShot", std::string("run failed, texture states unchanged: ") + e.what());
        return false;
    }

    states.assign(physicalTextures.size(), std::nullopt);
    for (std::size_t i = 0; i < physicalTextures.size(); ++i) {
        if (physicalTextures[i].resolved) {
            states[i] = physicalTextures[i].colorState;
        }
    }
    return true;
}

} // namespace gte::rg
