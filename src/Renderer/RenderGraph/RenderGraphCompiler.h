#pragma once

// Graph compilation: orders passes by their declared dependencies, culls passes that reach no
// output, and computes every resource's lifetime. Pure algorithm; no device is touched.

#include "RenderGraphBuilder.h"
#include "RenderGraphTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte::rg {

// A resource's lifetime as indices into CompiledGraph::executionOrder (not declaration indices).
// -1 means never used by a surviving pass. Covers reads and writes: a write without a later read
// still keeps the resource alive through that pass.
struct ResourceLifetime {
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;

    friend bool operator==(const ResourceLifetime&, const ResourceLifetime&) noexcept = default;
};

// The result of Compile(). `executionOrder` is empty when no pass is reachable; that is not an error.
struct CompiledGraph {
    // Topologically sorted, culled passes excluded. PassHandle::index is the declaration index into
    // CompiledGraphInput::passes; the generation is always 1.
    std::vector<PassHandle> executionOrder;

    // Parallel to CompiledGraphInput::textures/buffers, one entry per declared resource whether or not it
    // survived culling. `textureLifetimes` covers every texture dimension.
    std::vector<ResourceLifetime> textureLifetimes;
    std::vector<ResourceLifetime> bufferLifetimes;
};

enum class RenderPassEventContradictionKind : std::uint8_t {
    // A reader has no earlier writer, but a later pass in `processingOrder` writes the same resource.
    // The dependency scan cannot link them, so the writer can be culled away.
    OrphanReadWithLaterWriter,
    // A real dependency exists but the writer's RenderPassEvent is later than the reader's.
    // The dependency is honored; the event tag of one of the two passes is probably wrong.
    DeclaredEventOrderDisagreesWithRealDependency,
};

struct RenderPassEventContradiction {
    RenderPassEventContradictionKind kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
    std::int32_t readerPassIndex = -1;
    std::int32_t writerPassIndex = -1;
    ResourceKind resourceKind = ResourceKind::Texture;
    std::uint32_t resourceIndex = 0;
};

// Orders passes by data dependency (ties by RenderPassEvent, then declaration order) and culls passes
// with no path to a root: `finalOutputs`, the Keep*Output() sets and persistent textures. Event
// contradictions go to stderr (assert in debug). Writes PassRecord::isCulled; throws on a cycle.
CompiledGraph Compile(CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs);

} // namespace gte::rg
