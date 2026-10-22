#pragma once

#include "ClusterDrawTypes.h"
#include "ComputeShader.h"
#include "GpuParams.h"
#include "GpuReadback.h"
#include "PersistentBuffer.h"
#include "PersistentTarget.h"
#include "../../Renderer/Culling/ClusterCullMath.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gte {
class Pipeline;
} // namespace gte

namespace gte::rg {
class ClusterDrawBindings;
} // namespace gte::rg

namespace gte::fx {

namespace detail {
struct FrameContext;
} // namespace detail

struct ClusterDrawShape {
    std::uint32_t lanes = 1;     // Independent draw sequences per frame (for example one per depth-only target).
    std::uint32_t partSlots = 1; // Distinct meshes drawn per lane.
};

enum class ClusterPassKind : std::uint8_t { Reset, Cull, Draw, Stats };
inline constexpr std::size_t kClusterPassKindCount = 4;

enum class ClusterReserveResult : std::uint8_t { Ok, OverStateBudget, OverProcessBudget, BadIndex };

// Flat index of (scope, lane, slot) in the per-chain tables.
constexpr std::size_t ClusterChainIndex(
    std::size_t scope, std::uint32_t lane, std::uint32_t slot, const ClusterDrawShape& shape) noexcept
{
    return (scope * shape.lanes + lane) * shape.partSlots + slot;
}

// "<base>.L<lane>.P<slot>.Reset|Cull|Draw|Stats"
std::string ClusterPassNameText(std::string_view base, std::uint32_t lane, std::uint32_t slot, ClusterPassKind kind);

// True when the shape is inside the named lane and slot maxima.
bool ClusterShapeIsValid(const ClusterDrawShape& shape) noexcept;

// Capacity bytes held by every state in every view scope. Main thread only.
ClusterDrawLedger& ProcessClusterLedger() noexcept;

// Everything with a lifetime that a cluster draw chain needs, per view scope, lane and part slot.
// Create it once in the feature object. Destroy it only while the GPU is idle.
class ClusterDrawState {
public:
    ClusterDrawState(std::string_view baseName, ClusterDrawShape shape);
    ~ClusterDrawState();

    ClusterDrawState(const ClusterDrawState&) = delete;
    ClusterDrawState& operator=(const ClusterDrawState&) = delete;
    ClusterDrawState(ClusterDrawState&&) = delete;
    ClusterDrawState& operator=(ClusterDrawState&&) = delete;

    // False when the shape is out of range or the name table is full; Error() says why.
    bool Valid() const noexcept { return m_error.empty(); }
    const std::string& Error() const noexcept { return m_error; }

    ClusterDrawShape Shape() const noexcept { return m_shape; }
    std::uint64_t OwnerId() const noexcept { return m_owner; }

    // Every pass name of the state, for a gate and for the stats registry.
    std::vector<std::string> AllPassNames() const;

    // Interned pass name; null when out of range or the state is not valid.
    const char* PassName(std::uint32_t lane, std::uint32_t slot, ClusterPassKind kind) const noexcept;

    // Per-slot instance records, shared by the lanes of one frame.
    PersistentBuffer* Instances(std::uint32_t slot) noexcept;
    // Per-lane, per-slot working buffers.
    PersistentBuffer* Visible(std::uint32_t lane, std::uint32_t slot) noexcept;
    PersistentBuffer* Commands(std::uint32_t lane, std::uint32_t slot) noexcept;
    PersistentBuffer* Count(std::uint32_t lane, std::uint32_t slot) noexcept;
    // Optional visible-count readback of a part slot.
    GpuReadback<std::uint32_t>* Stats(std::uint32_t slot) noexcept;

    // One parameter block per (view scope, lane, slot): no two chains ever share one.
    GpuParams<ClusterCullParams>* Params(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) noexcept;
    // Static-storage debug name of that block.
    const char* ParamsName(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) const noexcept;
    // Static-storage graph name of the mesh cluster table import of that chain.
    const char* TableName(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) const noexcept;

    // The cull compute shader shared by every chain of this state.
    ComputeShader& CullShader() noexcept { return *m_cullShader; }
    // Copies the survivor count into the readback of a part slot.
    ComputeShader& StatsShader() noexcept { return *m_statsShader; }

    // Interned names of the clear and copy passes around the stats dispatch of a chain.
    const char* StatsClearName(std::uint32_t lane, std::uint32_t slot) const noexcept;
    const char* StatsCopyName(std::uint32_t lane, std::uint32_t slot) const noexcept;

    // A 1x1 texture of the lane for the pyramid binding while occlusion culling is off. Once per frame and lane.
    Texture HizDummy(Frame& frame, std::uint32_t lane);

    // Survivor count of the frame's view, summed over part slots. Two frames old.
    ClusterVisibleCount VisibleClusters(const Frame& frame) const;

    // The first lane of a frame fixes the candidate span of its scope; a later lane with another span is refused.
    bool AcceptCandidateSpan(const Frame& frame, std::uint32_t lane, const void* data, std::size_t count) noexcept;

    // The first lane of a frame uploads the instance records of a slot; the other lanes reuse them.
    bool NeedsInstanceUpload(const Frame& frame, std::uint32_t slot) const noexcept;
    void NoteInstanceUploaded(const Frame& frame, std::uint32_t slot) noexcept;

    // The cached pipeline of `key`. Null while it is missing or backing off after a failed build.
    const Pipeline* ResolvePipeline(detail::FrameContext& context, const ClusterPipelineKey& key);

    // Binding sets of the chains, created on first use. Null when the renderer refuses.
    // Index with BindingIndex(). Only the two view scopes have sets.
    rg::ClusterDrawBindings* Bindings(detail::FrameContext& context);
    std::size_t BindingIndex(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) const noexcept;

    // Checks the buffer budget of one chain (per-state and process-wide) and records its capacity.
    // Nothing is recorded unless the result is Ok.
    ClusterReserveResult TryReserveChainBytes(
        rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot, std::uint64_t instanceCount, std::uint64_t workItems);

    // Capacity bytes recorded for one scope (all lanes and slots).
    std::uint64_t HeldBytes(rg::ViewScope scope) const noexcept;

    // Call once per view callback. `declaredChain` restarts the idle count of the frame's scope.
    void NoteFrame(const Frame& frame, bool declaredChain) noexcept;

    // Frees every buffer of the frame's scope when it has been idle long enough, then returns its bytes
    // to the ledger. True when memory was released. A refused buffer keeps the accounting for a retry.
    bool ReleaseIdleScope(Frame& frame);

private:
    static constexpr std::uint64_t kNever = ~std::uint64_t{ 0 };

    std::size_t ChainIndex(std::size_t scope, std::uint32_t lane, std::uint32_t slot) const noexcept
    {
        return ClusterChainIndex(scope, lane, slot, m_shape);
    }
    bool InRange(std::uint32_t lane, std::uint32_t slot) const noexcept
    {
        return Valid() && lane < m_shape.lanes && slot < m_shape.partSlots;
    }

    std::string m_base;
    ClusterDrawShape m_shape;
    std::string m_error;
    std::uint64_t m_owner = 0;

    std::vector<const char*> m_passNames;   // [(lane * slots + slot) * kClusterPassKindCount + kind]
    std::vector<const char*> m_paramsNames; // [scope][lane][slot]
    std::vector<const char*> m_tableNames;  // [scope][lane][slot]
    std::unique_ptr<ComputeShader> m_cullShader;
    std::unique_ptr<ComputeShader> m_statsShader;
    std::vector<const char*> m_statsClearNames; // [lane][slot]
    std::vector<const char*> m_statsCopyNames;  // [lane][slot]
    struct SpanKey {
        std::uint64_t serial = 0;
        const void* data = nullptr;
        std::size_t count = 0;
    };
    std::array<SpanKey, rg::kViewScopeCount> m_candidateSpans;
    std::vector<std::uint64_t> m_instanceUploadSerial; // [scope][slot]
    std::vector<std::unique_ptr<PersistentBuffer>> m_instances;
    std::vector<std::unique_ptr<PersistentBuffer>> m_visible;
    std::vector<std::unique_ptr<PersistentBuffer>> m_commands;
    std::vector<std::unique_ptr<PersistentBuffer>> m_count;
    std::vector<std::unique_ptr<GpuReadback<std::uint32_t>>> m_stats;
    std::vector<std::unique_ptr<GpuParams<ClusterCullParams>>> m_params;
    std::vector<std::unique_ptr<PersistentTarget>> m_hizDummy; // [lane]

    std::array<const Pipeline*, kClusterPipelineKeyCount> m_pipelines{};
    std::array<rg::BuildRetryGate, kClusterPipelineKeyCount> m_pipelineGates;
    std::unique_ptr<rg::ClusterDrawBindings> m_bindings;

    std::vector<std::uint64_t> m_chainBytes; // [scope][lane][slot] recorded capacity
    std::array<std::uint64_t, rg::kViewScopeCount> m_lastChainFrame;
};

} // namespace gte::fx
