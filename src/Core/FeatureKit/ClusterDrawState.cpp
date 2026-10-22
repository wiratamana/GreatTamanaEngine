#include "ClusterDrawState.h"

#include "ClusterNamePool.h"
#include "ClusterPartPlanning.h"
#include "FrameContext.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/ClusterDrawBindings.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>

namespace gte::fx {

namespace {

constexpr const char* kLogCategory = "ClusterDraw";

constexpr std::uint32_t kMaxLanes = std::max(kMainViewLanes, kDepthOnlyLanes);
constexpr std::uint32_t kMaxSlots = std::max(kMainViewPartSlots, kDepthOnlyPartSlots);

constexpr std::array<const char*, kClusterPassKindCount> kPassKindNames{ "Reset", "Cull", "Draw", "Stats" };

std::uint64_t NextOwnerId() noexcept
{
    static std::atomic<std::uint64_t> counter{ 1 };
    return counter.fetch_add(1, std::memory_order_relaxed);
}

std::string ScopeTag(std::size_t scope)
{
    return ".S" + std::to_string(scope);
}

} // namespace

std::string ClusterPassNameText(std::string_view base, std::uint32_t lane, std::uint32_t slot, ClusterPassKind kind)
{
    std::string name(base);
    name += ".L" + std::to_string(lane) + ".P" + std::to_string(slot) + ".";
    name += kPassKindNames[static_cast<std::size_t>(kind)];
    return name;
}

bool ClusterShapeIsValid(const ClusterDrawShape& shape) noexcept
{
    return shape.lanes >= 1 && shape.lanes <= kMaxLanes && shape.partSlots >= 1 && shape.partSlots <= kMaxSlots;
}

ClusterDrawLedger& ProcessClusterLedger() noexcept
{
    static ClusterDrawLedger ledger;
    return ledger;
}

ClusterDrawState::ClusterDrawState(std::string_view baseName, ClusterDrawShape shape)
    : m_base(baseName)
    , m_shape(shape)
    , m_owner(NextOwnerId())
{
    m_lastChainFrame.fill(kNever);
    if (m_base.empty()) {
        m_error = "ClusterDrawState needs a base name";
        return;
    }
    if (!ClusterShapeIsValid(m_shape)) {
        m_error = "ClusterDrawState '" + m_base + "': lanes or part slots are out of range";
        return;
    }

    const std::size_t chains = static_cast<std::size_t>(m_shape.lanes) * m_shape.partSlots;
    m_passNames.reserve(chains * kClusterPassKindCount);
    for (std::uint32_t lane = 0; lane < m_shape.lanes; ++lane) {
        for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
            for (std::size_t kind = 0; kind < kClusterPassKindCount; ++kind) {
                const char* name =
                    InternClusterName(ClusterPassNameText(m_base, lane, slot, static_cast<ClusterPassKind>(kind)));
                if (name == nullptr) {
                    m_error = "ClusterDrawState '" + m_base + "': the pass name table is full";
                    m_passNames.clear();
                    return;
                }
                m_passNames.push_back(name);
            }
        }
    }
    m_paramsNames.reserve(chains * rg::kViewScopeCount);
    m_tableNames.reserve(chains * rg::kViewScopeCount);
    for (std::size_t scope = 0; scope < rg::kViewScopeCount; ++scope) {
        for (std::uint32_t lane = 0; lane < m_shape.lanes; ++lane) {
            for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
                const std::string tail = ScopeTag(scope) + ".L" + std::to_string(lane) + ".P" + std::to_string(slot);
                const char* params = InternClusterName(m_base + ".Params" + tail);
                const char* table = InternClusterName(m_base + ".Clusters" + tail);
                if (params == nullptr || table == nullptr) {
                    m_error = "ClusterDrawState '" + m_base + "': the pass name table is full";
                    m_passNames.clear();
                    m_paramsNames.clear();
                    m_tableNames.clear();
                    return;
                }
                m_paramsNames.push_back(params);
                m_tableNames.push_back(table);
            }
        }
    }
    m_cullShader = std::make_unique<ComputeShader>("ClusterCull");
    m_statsShader = std::make_unique<ComputeShader>("ClusterCullStats");
    m_statsClearNames.reserve(chains);
    m_statsCopyNames.reserve(chains);
    for (std::uint32_t lane = 0; lane < m_shape.lanes; ++lane) {
        for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
            const std::string stats = ClusterPassNameText(m_base, lane, slot, ClusterPassKind::Stats);
            const char* clear = InternClusterName(stats + ".Clear");
            const char* copy = InternClusterName(stats + ".Copy");
            if (clear == nullptr || copy == nullptr) {
                m_error = "ClusterDrawState '" + m_base + "': the pass name table is full";
                m_passNames.clear();
                m_paramsNames.clear();
                m_tableNames.clear();
                m_statsClearNames.clear();
                m_statsCopyNames.clear();
                return;
            }
            m_statsClearNames.push_back(clear);
            m_statsCopyNames.push_back(copy);
        }
    }
    m_instanceUploadSerial.assign(rg::kViewScopeCount * m_shape.partSlots, 0);

    constexpr BufferUse kInstanceUse = BufferUse::Storage | BufferUse::HostWrite;
    constexpr BufferUse kVisibleUse = BufferUse::Storage;
    constexpr BufferUse kCommandUse = BufferUse::Storage | BufferUse::Indirect;
    constexpr BufferUse kCountUse = BufferUse::Storage | BufferUse::Indirect | BufferUse::TransferDst;

    for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
        const std::string suffix = ".P" + std::to_string(slot);
        m_instances.push_back(std::make_unique<PersistentBuffer>(m_base + ".Instances" + suffix, kInstanceUse));
        m_stats.push_back(std::make_unique<GpuReadback<std::uint32_t>>(m_base + ".Stats" + suffix));
    }
    for (std::uint32_t lane = 0; lane < m_shape.lanes; ++lane) {
        for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
            const std::string suffix = ".L" + std::to_string(lane) + ".P" + std::to_string(slot);
            m_visible.push_back(std::make_unique<PersistentBuffer>(m_base + ".Visible" + suffix, kVisibleUse));
            m_commands.push_back(std::make_unique<PersistentBuffer>(m_base + ".Commands" + suffix, kCommandUse));
            m_count.push_back(std::make_unique<PersistentBuffer>(m_base + ".Count" + suffix, kCountUse));
        }
    }
    for (std::size_t i = 0; i < chains * rg::kViewScopeCount; ++i) {
        m_params.push_back(std::make_unique<GpuParams<ClusterCullParams>>());
    }
    m_chainBytes.assign(chains * rg::kViewScopeCount, 0);
}

ClusterDrawState::~ClusterDrawState()
{
    ProcessClusterLedger().ReleaseOwner(m_owner);
}

std::vector<std::string> ClusterDrawState::AllPassNames() const
{
    std::vector<std::string> names;
    if (!Valid()) {
        return names;
    }
    names.reserve(m_passNames.size());
    for (const char* name : m_passNames) {
        names.emplace_back(name);
    }
    return names;
}

const char* ClusterDrawState::PassName(std::uint32_t lane, std::uint32_t slot, ClusterPassKind kind) const noexcept
{
    if (!InRange(lane, slot) || static_cast<std::size_t>(kind) >= kClusterPassKindCount) {
        return nullptr;
    }
    return m_passNames[(static_cast<std::size_t>(lane) * m_shape.partSlots + slot) * kClusterPassKindCount
        + static_cast<std::size_t>(kind)];
}

PersistentBuffer* ClusterDrawState::Instances(std::uint32_t slot) noexcept
{
    return InRange(0, slot) ? m_instances[slot].get() : nullptr;
}

PersistentBuffer* ClusterDrawState::Visible(std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_visible[ChainIndex(0, lane, slot)].get() : nullptr;
}

PersistentBuffer* ClusterDrawState::Commands(std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_commands[ChainIndex(0, lane, slot)].get() : nullptr;
}

PersistentBuffer* ClusterDrawState::Count(std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_count[ChainIndex(0, lane, slot)].get() : nullptr;
}

GpuReadback<std::uint32_t>* ClusterDrawState::Stats(std::uint32_t slot) noexcept
{
    return InRange(0, slot) ? m_stats[slot].get() : nullptr;
}

GpuParams<ClusterCullParams>* ClusterDrawState::Params(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_params[ChainIndex(rg::ViewScopeIndex(scope), lane, slot)].get() : nullptr;
}

const char* ClusterDrawState::ParamsName(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) const noexcept
{
    return InRange(lane, slot) ? m_paramsNames[ChainIndex(rg::ViewScopeIndex(scope), lane, slot)] : nullptr;
}

const char* ClusterDrawState::TableName(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) const noexcept
{
    return InRange(lane, slot) ? m_tableNames[ChainIndex(rg::ViewScopeIndex(scope), lane, slot)] : nullptr;
}

const char* ClusterDrawState::StatsClearName(std::uint32_t lane, std::uint32_t slot) const noexcept
{
    return InRange(lane, slot) ? m_statsClearNames[ChainIndex(0, lane, slot)] : nullptr;
}

const char* ClusterDrawState::StatsCopyName(std::uint32_t lane, std::uint32_t slot) const noexcept
{
    return InRange(lane, slot) ? m_statsCopyNames[ChainIndex(0, lane, slot)] : nullptr;
}

ClusterVisibleCount ClusterDrawState::VisibleClusters(const Frame& frame) const
{
    if (!Valid()) {
        return ClusterVisibleCount{};
    }
    std::vector<ClusterCountSample> samples;
    samples.reserve(m_stats.size());
    for (const auto& readback : m_stats) {
        if (const auto latest = readback->Latest(frame)) {
            samples.push_back(ClusterCountSample{ true, latest->value, latest->frame });
        }
    }
    return SumClusterCounts(samples);
}

bool ClusterDrawState::AcceptCandidateSpan(
    const Frame& frame, std::uint32_t lane, const void* data, std::size_t count) noexcept
{
    SpanKey& key = m_candidateSpans[rg::ViewScopeIndex(frame.Scope())];
    if (lane == 0 || key.serial != frame.Serial()) {
        key = SpanKey{ frame.Serial(), data, count };
        return true;
    }
    return key.data == data && key.count == count;
}

bool ClusterDrawState::NeedsInstanceUpload(const Frame& frame, std::uint32_t slot) const noexcept
{
    return InRange(0, slot)
        && m_instanceUploadSerial[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot] != frame.Serial();
}

void ClusterDrawState::NoteInstanceUploaded(const Frame& frame, std::uint32_t slot) noexcept
{
    if (InRange(0, slot)) {
        m_instanceUploadSerial[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot] = frame.Serial();
    }
}

const Pipeline* ClusterDrawState::ResolvePipeline(detail::FrameContext& context, const ClusterPipelineKey& key)
{
    const std::size_t index = ClusterPipelineCacheIndex(key);
    if (m_pipelines[index] != nullptr) {
        return m_pipelines[index];
    }
    if (context.core == nullptr || context.builder == nullptr) {
        return nullptr;
    }
    rg::BuildRetryGate& gate = m_pipelineGates[index];
    const std::uint64_t frame = context.builder->FrameIndex();
    if (!gate.ShouldAttempt(frame)) {
        return nullptr;
    }

    const Pipeline* pipeline = nullptr;
    std::string failure = "the shaders are missing or the pipeline build failed";
    try {
        Core& core = *context.core;
        pipeline = core.GetRenderer().CreateClusterPipeline(key, core.GetSceneServicesDescriptorSet().LayoutHandle());
    } catch (const std::exception& e) {
        failure = e.what();
    } catch (...) {
        failure = "unknown error";
    }

    if (pipeline == nullptr) {
        if (gate.OnFailure(frame) == rg::BuildRetryGate::FailureKind::First) {
            GTE_LOG_WARNING(kLogCategory, "Cluster pipeline of '" + m_base + "' is not available: " + failure);
        }
        return nullptr;
    }
    if (gate.OnSuccess()) {
        GTE_LOG_INFO(kLogCategory, "Cluster pipeline of '" + m_base + "' built after an earlier failure.");
    }
    m_pipelines[index] = pipeline;
    return pipeline;
}

rg::ClusterDrawBindings* ClusterDrawState::Bindings(detail::FrameContext& context)
{
    if (!Valid() || context.core == nullptr) {
        return nullptr;
    }
    if (m_bindings == nullptr) {
        m_bindings = std::make_unique<rg::ClusterDrawBindings>(context.core->GetRenderer());
    }
    // One set per (view scope, lane, slot) of the two real scopes.
    const std::size_t needed = 2 * static_cast<std::size_t>(m_shape.lanes) * m_shape.partSlots;
    if (m_bindings->SetCount() < needed && !m_bindings->Prepare(needed)) {
        return nullptr;
    }
    return m_bindings.get();
}

std::size_t ClusterDrawState::BindingIndex(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) const noexcept
{
    if (!InRange(lane, slot) || scope == rg::ViewScope::Shared) {
        return std::numeric_limits<std::size_t>::max();
    }
    return ClusterChainIndex(rg::ViewScopeIndex(scope) - 1, lane, slot, m_shape);
}

std::uint64_t ClusterDrawState::HeldBytes(rg::ViewScope scope) const noexcept
{
    if (!Valid()) {
        return 0;
    }
    const std::size_t perScope = static_cast<std::size_t>(m_shape.lanes) * m_shape.partSlots;
    const std::size_t first = rg::ViewScopeIndex(scope) * perScope;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < perScope; ++i) {
        total += m_chainBytes[first + i];
    }
    return total;
}

ClusterReserveResult ClusterDrawState::TryReserveChainBytes(
    rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot, std::uint64_t instanceCount, std::uint64_t workItems)
{
    if (!InRange(lane, slot) || scope == rg::ViewScope::Shared) {
        return ClusterReserveResult::BadIndex;
    }
    const std::size_t scopeIndex = rg::ViewScopeIndex(scope);
    std::uint64_t& recorded = m_chainBytes[ChainIndex(scopeIndex, lane, slot)];
    const std::uint64_t wanted = std::max(recorded, ClusterChainBytes(instanceCount, workItems));
    const std::uint64_t others = HeldBytes(scope) - recorded;
    if (!FitsClusterBudget(others, wanted, kClusterDrawBudgetBytes)) {
        return ClusterReserveResult::OverStateBudget;
    }
    if (!ProcessClusterLedger().TryReserve(m_owner, static_cast<std::uint32_t>(scopeIndex), others + wanted,
            kClusterDrawProcessBudgetBytes)) {
        return ClusterReserveResult::OverProcessBudget;
    }
    recorded = wanted;
    return ClusterReserveResult::Ok;
}

void ClusterDrawState::NoteFrame(const Frame& frame, bool declaredChain) noexcept
{
    std::uint64_t& last = m_lastChainFrame[rg::ViewScopeIndex(frame.Scope())];
    if (declaredChain || last == kNever) {
        last = frame.FrameIndex();
    }
}

bool ClusterDrawState::ReleaseIdleScope(Frame& frame)
{
    if (!Valid()) {
        return false;
    }
    const rg::ViewScope scope = frame.Scope();
    const std::size_t scopeIndex = rg::ViewScopeIndex(scope);
    const std::uint64_t last = m_lastChainFrame[scopeIndex];
    if (last == kNever || HeldBytes(scope) == 0) {
        return false;
    }
    const std::uint64_t now = frame.FrameIndex();
    const std::uint64_t idle = now > last ? now - last : 0;
    if (!ShouldReleaseIdleScope(static_cast<std::uint32_t>(std::min<std::uint64_t>(idle, 0xFFFFFFFFu)))) {
        return false;
    }

    bool all = true;
    for (const auto& buffer : m_instances) {
        const bool ok = buffer->Release(frame);
        all = all && ok;
    }
    for (std::size_t i = 0; i < m_visible.size(); ++i) {
        const bool visibleOk = m_visible[i]->Release(frame);
        const bool commandsOk = m_commands[i]->Release(frame);
        const bool countOk = m_count[i]->Release(frame);
        all = all && visibleOk && commandsOk && countOk;
    }
    if (!all) {
        return false;
    }

    const std::size_t perScope = static_cast<std::size_t>(m_shape.lanes) * m_shape.partSlots;
    std::fill_n(m_chainBytes.begin() + static_cast<std::ptrdiff_t>(scopeIndex * perScope), perScope, std::uint64_t{ 0 });
    ProcessClusterLedger().Release(m_owner, static_cast<std::uint32_t>(scopeIndex));
    m_lastChainFrame[scopeIndex] = kNever;
    return true;
}

} // namespace gte::fx
