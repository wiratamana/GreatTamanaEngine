#pragma once

#include "RenderGraphHandles.h"
#include "../../Core/GraphicsHandles.h"

#include <cstddef>
#include <vector>

namespace gte {
class Renderer;
} // namespace gte

namespace gte::rg {

struct PassContext;

// Owns the descriptor sets of cluster draws (binding 0 = visible list, binding 1 = instances).
// One set per draw per frame. Create and destroy it only while the GPU is idle.
class ClusterDrawBindings {
public:
    explicit ClusterDrawBindings(Renderer& renderer) noexcept;
    ~ClusterDrawBindings();

    ClusterDrawBindings(const ClusterDrawBindings&) = delete;
    ClusterDrawBindings& operator=(const ClusterDrawBindings&) = delete;
    ClusterDrawBindings(ClusterDrawBindings&& other) noexcept;
    ClusterDrawBindings& operator=(ClusterDrawBindings&& other) noexcept;

    // Grows to at least `setCount` sets. False when the pool refuses; sets that already existed are kept.
    bool Prepare(std::size_t setCount);

    std::size_t SetCount() const noexcept { return m_sets.size(); }

    // Points set `index` at the resolved graph buffers. Call while a pass executes.
    // False when `index` is out of range or a buffer did not resolve.
    bool Rewrite(PassContext& ctx, std::size_t index, BufferHandle visible, BufferHandle instances);

    DescriptorSetHandle Set(std::size_t index) const noexcept;

private:
    void ReleaseAll() noexcept;

    Renderer* m_renderer = nullptr;
    std::vector<DescriptorSetHandle> m_sets;
};

} // namespace gte::rg
