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

// The pass kinds plus the stats clear and copy passes are exactly what the render graph reserves timing slots for.
static_assert(kClusterPassKindCount + 2 == kMainViewPassNamesPerPart);
static_assert(kClusterPassKindCount - 1 == kDepthOnlyPassNamesPerPart);

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
    m_tableNames.reserve(chains * rg::kViewScopeCount);
    for (std::size_t scope = 0; scope < rg::kViewScopeCount; ++scope) {
        for (std::uint32_t lane = 0; lane < m_shape.lanes; ++lane) {
            for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
                const std::string tail = ScopeTag(scope) + ".L" + std::to_string(lane) + ".P" + std::to_string(slot);
                const char* table = InternClusterName(m_base + ".Clusters" + tail);
                if (table == nullptr) {
                    m_error = "ClusterDrawState '" + m_base + "': the pass name table is full";
                    m_passNames.clear();
                    m_tableNames.clear();
                    return;
                }
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
    m_instanceContent.assign(rg::kViewScopeCount * m_shape.partSlots, InstanceContent{});

    constexpr BufferUse kInstanceUse = BufferUse::Storage | BufferUse::HostWrite;
    constexpr BufferUse kVisibleUse = BufferUse::Storage;
    constexpr BufferUse kCommandUse = BufferUse::Storage | BufferUse::Indirect;
    constexpr BufferUse kCountUse = BufferUse::Storage | BufferUse::Indirect | BufferUse::TransferDst;
    constexpr BufferUse kHistoryUse = BufferUse::Storage;

    for (std::uint32_t slot = 0; slot < m_shape.partSlots; ++slot) {
        const std::string suffix = ".P" + std::to_string(slot);
        m_instances.push_back(std::make_unique<PersistentBuffer>(m_base + ".Instances" + suffix, kInstanceUse));
        m_history.push_back(std::make_unique<PersistentBuffer>(m_base + ".History" + suffix, kHistoryUse));
        m_stats.push_back(std::make_unique<GpuReadback<ClusterStatsWords>>(m_base + ".Stats" + suffix));
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
    for (std::uint32_t lane = 0; lane < m_shape.lanes; ++lane) {
        const std::string suffix = ".L" + std::to_string(lane);
        m_hizDummy.push_back(std::make_unique<PersistentTarget>(m_base + ".HizDummy" + suffix));
        m_historyDummy.push_back(std::make_unique<PersistentBuffer>(m_base + ".HistoryDummy" + suffix, kHistoryUse));
    }
    m_hizDummyOnce.resize(rg::kViewScopeCount * m_shape.lanes);
    m_historyDummyOnce.resize(rg::kViewScopeCount * m_shape.lanes);
    m_historyRecords.assign(rg::kViewScopeCount * m_shape.partSlots, ClusterHistoryRecord{});
    m_chainBytes.assign(chains * rg::kViewScopeCount, 0);
    m_instanceBytes.assign(rg::kViewScopeCount * m_shape.partSlots, 0);
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
    return InRange(lane, slot) ? m_visible[ChainIndex(0, BufferLane(lane), slot)].get() : nullptr;
}

PersistentBuffer* ClusterDrawState::Commands(std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_commands[ChainIndex(0, BufferLane(lane), slot)].get() : nullptr;
}

PersistentBuffer* ClusterDrawState::Count(std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_count[ChainIndex(0, BufferLane(lane), slot)].get() : nullptr;
}

GpuReadback<ClusterStatsWords>* ClusterDrawState::Stats(std::uint32_t slot) noexcept
{
    return InRange(0, slot) ? m_stats[slot].get() : nullptr;
}

GpuParams<ClusterCullParams>* ClusterDrawState::Params(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot) noexcept
{
    return InRange(lane, slot) ? m_params[ChainIndex(rg::ViewScopeIndex(scope), lane, slot)].get() : nullptr;
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

Texture ClusterDrawState::HizDummy(Frame& frame, std::uint32_t lane)
{
    if (!InRange(lane, 0)) {
        return Texture{};
    }
    detail::FrameContext* context = frame.Context();
    const std::size_t index = rg::ViewScopeIndex(frame.Scope()) * m_shape.lanes + lane;
    return m_hizDummyOnce[index].Get(frame.Serial(), context != nullptr ? context->builder : nullptr,
        [&] { return m_hizDummy[lane]->Acquire(frame, Extent2D{ 1, 1 }, PixelFormat::R32_Float); });
}

PersistentBuffer* ClusterDrawState::History(std::uint32_t slot) noexcept
{
    return InRange(0, slot) ? m_history[slot].get() : nullptr;
}

GpuBufferRef ClusterDrawState::HistoryDummy(Frame& frame, std::uint32_t lane)
{
    if (!InRange(lane, 0)) {
        return GpuBufferRef{};
    }
    detail::FrameContext* context = frame.Context();
    const std::size_t index = rg::ViewScopeIndex(frame.Scope()) * m_shape.lanes + lane;
    return m_historyDummyOnce[index].Get(frame.Serial(), context != nullptr ? context->builder : nullptr,
        [&] { return m_historyDummy[lane]->Acquire(frame, kClusterHistoryBytes); });
}

const ClusterHistoryRecord& ClusterDrawState::HistoryRecord(const Frame& frame, std::uint32_t slot) const noexcept
{
    static const ClusterHistoryRecord kNone;
    if (!InRange(0, slot) || frame.Scope() == rg::ViewScope::Shared) {
        return kNone;
    }
    return m_historyRecords[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot];
}

void ClusterDrawState::NoteHistoryWritten(const Frame& frame, std::uint32_t slot, std::uint64_t key) noexcept
{
    if (InRange(0, slot) && frame.Scope() != rg::ViewScope::Shared) {
        m_historyRecords[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot] =
            ClusterHistoryRecord{ key, frame.FrameIndex(), true };
    }
}

void ClusterDrawState::DropHistory(const Frame& frame, std::uint32_t slot) noexcept
{
    if (InRange(0, slot) && frame.Scope() != rg::ViewScope::Shared) {
        m_historyRecords[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot] = ClusterHistoryRecord{};
    }
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
            samples.push_back(ClusterCountSample{ true, latest->value.visible, latest->frame });
        }
    }
    return SumClusterCounts(samples);
}

ClusterReasonCounts ClusterDrawState::ReasonCounts(const Frame& frame) const
{
    if (!Valid()) {
        return ClusterReasonCounts{};
    }
    std::vector<ClusterReasonSample> samples;
    samples.reserve(m_stats.size());
    for (const auto& readback : m_stats) {
        if (const auto latest = readback->Latest(frame)) {
            samples.push_back(ClusterReasonSample{ true, latest->value, latest->frame });
        }
    }
    return SumClusterReasons(samples);
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

bool ClusterDrawState::InstanceContentIs(const Frame& frame, std::uint32_t slot, const InstanceContent& want) const noexcept
{
    return InRange(0, slot) && want.group.epoch != 0
        && m_instanceContent[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot] == want;
}

void ClusterDrawState::NoteInstanceContent(const Frame& frame, std::uint32_t slot, const InstanceContent& content) noexcept
{
    if (InRange(0, slot)) {
        m_instanceContent[rg::ViewScopeIndex(frame.Scope()) * m_shape.partSlots + slot] = content;
    }
}

namespace {

std::optional<std::uint64_t> FindKey(const std::vector<ClusterDrawState::KeyMemo>& memos, const ClusterDrawState::KeyQuery& query)
{
    if (query.group.epoch == 0) {
        return std::nullopt;
    }
    for (const ClusterDrawState::KeyMemo& memo : memos) {
        if (memo.query == query) {
            return memo.key;
        }
    }
    return std::nullopt;
}

void StoreKey(std::vector<ClusterDrawState::KeyMemo>& memos, const ClusterDrawState::KeyQuery& query, std::uint64_t key)
{
    if (query.group.epoch == 0) {
        return;
    }
    // Memos of an older candidate list are dead; the cap bounds a scene with many parts.
    if (!memos.empty() && (memos.front().query.group.epoch != query.group.epoch || memos.size() >= ClusterDrawState::kMaxKeyMemos)) {
        memos.clear();
    }
    memos.push_back(ClusterDrawState::KeyMemo{ query, key });
}

} // namespace

std::optional<std::uint64_t> ClusterDrawState::FindSetKey(const KeyQuery& query) const noexcept
{
    return FindKey(m_setKeys, query);
}

void ClusterDrawState::StoreSetKey(const KeyQuery& query, std::uint64_t key)
{
    StoreKey(m_setKeys, query, key);
}

std::optional<std::uint64_t> ClusterDrawState::FindHistoryKey(const KeyQuery& query) const noexcept
{
    return FindKey(m_historyKeys, query);
}

void ClusterDrawState::StoreHistoryKey(const KeyQuery& query, std::uint64_t key)
{
    StoreKey(m_historyKeys, query, key);
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
    const std::size_t firstSlot = rg::ViewScopeIndex(scope) * m_shape.partSlots;
    for (std::size_t i = 0; i < m_shape.partSlots; ++i) {
        total += m_instanceBytes[firstSlot + i];
    }
    return total;
}

ClusterReserveResult ClusterDrawState::CheckChainBytes(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot,
    std::uint64_t instanceCount, std::uint64_t workItems, bool twoPhase, std::optional<std::uint64_t> historyWords,
    ChainWant& want) const
{
    const bool twoPhaseOnSharedLanes = twoPhase && m_shape.shareLaneBuffers;
    if (!InRange(lane, slot) || scope == rg::ViewScope::Shared || twoPhaseOnSharedLanes
        || (twoPhase && lane + 1 >= m_shape.lanes)) {
        return ClusterReserveResult::BadIndex;
    }
    const std::size_t scopeIndex = rg::ViewScopeIndex(scope);
    const std::uint64_t recordedLane = m_chainBytes[ChainIndex(scopeIndex, BufferLane(lane), slot)];
    const std::uint64_t recordedInstances = m_instanceBytes[scopeIndex * m_shape.partSlots + slot];
    want.lane = std::max(recordedLane,
        ClusterChainBytes(instanceCount, workItems, ClusterChainShape{ twoPhase, true, historyWords }));
    want.instances = std::max(recordedInstances,
        ClusterBufferCapacity(instanceCount * std::uint64_t{ sizeof(ClusterInstanceGpu) }));
    const std::uint64_t others = HeldBytes(scope) - recordedLane - recordedInstances;
    const std::uint64_t total = want.lane + want.instances;
    if (!FitsClusterBudget(others, total, kClusterDrawBudgetBytes)) {
        return ClusterReserveResult::OverStateBudget;
    }
    if (!ProcessClusterLedger().CanReserve(
            m_owner, static_cast<std::uint32_t>(scopeIndex), others + total, kClusterDrawProcessBudgetBytes)) {
        return ClusterReserveResult::OverProcessBudget;
    }
    return ClusterReserveResult::Ok;
}

ClusterReserveResult ClusterDrawState::CanReserveChainBytes(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot,
    std::uint64_t instanceCount, std::uint64_t workItems, bool twoPhase, std::optional<std::uint64_t> historyWords) const
{
    ChainWant want;
    return CheckChainBytes(scope, lane, slot, instanceCount, workItems, twoPhase, historyWords, want);
}

ClusterReserveResult ClusterDrawState::TryReserveChainBytes(rg::ViewScope scope, std::uint32_t lane, std::uint32_t slot,
    std::uint64_t instanceCount, std::uint64_t workItems, bool twoPhase, std::optional<std::uint64_t> historyWords)
{
    ChainWant want;
    const ClusterReserveResult result =
        CheckChainBytes(scope, lane, slot, instanceCount, workItems, twoPhase, historyWords, want);
    if (result != ClusterReserveResult::Ok) {
        return result;
    }
    const std::size_t scopeIndex = rg::ViewScopeIndex(scope);
    std::uint64_t& recordedLane = m_chainBytes[ChainIndex(scopeIndex, BufferLane(lane), slot)];
    std::uint64_t& recordedInstances = m_instanceBytes[scopeIndex * m_shape.partSlots + slot];
    const std::uint64_t others = HeldBytes(scope) - recordedLane - recordedInstances;
    // The check above passed, so the ledger raise cannot fail; its entry now holds this scope's new total.
    if (!ProcessClusterLedger().TryReserve(m_owner, static_cast<std::uint32_t>(scopeIndex),
            others + want.lane + want.instances, kClusterDrawProcessBudgetBytes)) {
        return ClusterReserveResult::OverProcessBudget;
    }
    recordedLane = want.lane;
    recordedInstances = want.instances;
    return ClusterReserveResult::Ok;
}

void ClusterDrawState::NoteFrame(const Frame& frame, bool declaredChain) noexcept
{
    std::uint64_t& last = m_lastChainFrame[rg::ViewScopeIndex(frame.Scope())];
    if (declaredChain || last == kNever) {
        last = frame.FrameIndex();
    }
}

void ClusterDrawState::TickIdle(Frame& frame, bool declaredChain)
{
    if (!Valid() || frame.Scope() == rg::ViewScope::Shared) {
        return;
    }
    NoteFrame(frame, declaredChain);
    ReleaseIdleScope(frame);
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
    for (const auto& buffer : m_history) {
        const bool ok = buffer->Release(frame);
        all = all && ok;
    }
    for (const auto& buffer : m_historyDummy) {
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
    std::fill_n(m_instanceBytes.begin() + static_cast<std::ptrdiff_t>(scopeIndex * m_shape.partSlots), m_shape.partSlots,
        std::uint64_t{ 0 });
    std::fill_n(m_historyRecords.begin() + static_cast<std::ptrdiff_t>(scopeIndex * m_shape.partSlots),
        m_shape.partSlots, ClusterHistoryRecord{});
    ProcessClusterLedger().Release(m_owner, static_cast<std::uint32_t>(scopeIndex));
    m_lastChainFrame[scopeIndex] = kNever;
    return true;
}

} // namespace gte::fx
