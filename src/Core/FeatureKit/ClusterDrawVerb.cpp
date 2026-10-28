#include "ClusterDrawChain.h"

#include "ClusterPartPlanning.h"
#include "ComputeChain.h"
#include "VerbSupport.h"
#include "../Core.h"
#include "../ClusterClaim.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/ClusterDrawBindings.h"
#include "../../Renderer/RenderGraph/IndirectDrawPass.h"
#include "../../Renderer/RenderGraph/PassKindDefaults.h"
#include "../../Renderer/RenderGraph/RecordedPass.h"

#include <array>
#include <cassert>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gte::fx {

namespace {

// One mesh of the chain, resolved by Plan() and declared by Record().
struct PlannedPart {
    MeshToken mesh;
    std::uint32_t slot = 0;
    std::uint32_t clusterCount = 0;
    std::uint64_t workItems = 0;
    std::vector<ClusterInstanceGpu> instances;
    std::vector<Entity> entities;
    ClusterCullParams params{};
};

std::string TokenText(MeshToken token)
{
    return "mesh " + std::to_string(token.index) + "/" + std::to_string(token.generation);
}

// Bits the chain sets itself; a caller never asks for them.
constexpr std::uint32_t kChainOwnedFlags =
    kClusterCullFlagOcclusion | kClusterCullFlagPhaseFirst | kClusterCullFlagPhaseSecond | kClusterCullFlagHistoryValid;

// Push block of ClusterCullStats.comp.
struct ClusterStatsPush {
    std::uint32_t sumSecond;
};

} // namespace

struct ClusterDrawChain::Impl {
    Impl(Frame& owner, ClusterDrawState& drawState, std::uint32_t drawLane) noexcept
        : frame(&owner)
        , state(&drawState)
        , lane(drawLane)
    {
    }

    Frame* frame;
    ClusterDrawState* state;
    std::uint32_t lane;

    std::span<const ClusterInstance> instances;
    ClusterCamera camera;
    ClusterCamera cullCamera;
    bool hasCamera = false;
    bool hasCullCamera = false;
    ClusterCullFlags flags = ClusterCullFlags::Frustum;
    Texture target;
    bool depthOnly = false;
    bool idTint = false;
    bool stats = false;

    // Input of TwoPhase().
    HizPyramid* pyramid = nullptr;
    Texture sceneDepth;
    bool wantsTwoPhase = false;

    // Decisions made by Plan(): two-phase runs lane `lane` and `lane + 1`.
    bool twoPhase = false;
    HizReason hizReason = HizReason::None;
    HizPlan hizPlan;
    Texture atlas; // This frame's pyramid, set by the build between the rounds.

    // Filled by Plan().
    bool planned = false;
    ClusterPlan plan;
    std::vector<PlannedPart> parts;
    const Pipeline* pipeline = nullptr;
    rg::ClusterDrawBindings* bindings = nullptr;
    ClusterClaimContext claimContext;

    bool recorded = false;
    ClusterStats result;
};

ClusterDrawChain Frame::ClusterDraw(ClusterDrawState& state, std::uint32_t lane)
{
    return ClusterDrawChain(std::make_unique<ClusterDrawChain::Impl>(*this, state, lane));
}

bool Frame::ClusterPathSupported() const noexcept
{
    const detail::FrameContext* context = Context();
    return context != nullptr && context->core != nullptr
        && gte::ClusterPathSupported(context->core->GetRenderer().IndirectDrawCaps());
}

ClusterDrawChain::ClusterDrawChain(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

ClusterDrawChain::~ClusterDrawChain() = default;
ClusterDrawChain::ClusterDrawChain(ClusterDrawChain&&) noexcept = default;
ClusterDrawChain& ClusterDrawChain::operator=(ClusterDrawChain&&) noexcept = default;

ClusterDrawChain& ClusterDrawChain::Instances(std::span<const ClusterInstance> instances)
{
    m_impl->instances = instances;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::Camera(const ClusterCamera& camera)
{
    m_impl->camera = camera;
    m_impl->hasCamera = true;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::CullCamera(const ClusterCamera& camera)
{
    m_impl->cullCamera = camera;
    m_impl->hasCullCamera = true;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::Culling(ClusterCullFlags flags)
{
    m_impl->flags = flags;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::TwoPhase(HizPyramid& pyramid, Texture sceneDepth)
{
    m_impl->pyramid = &pyramid;
    m_impl->sceneDepth = sceneDepth;
    m_impl->wantsTwoPhase = true;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::Target(Texture sceneTarget)
{
    m_impl->target = sceneTarget;
    m_impl->depthOnly = false;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::DepthOnlyTarget(Texture depth)
{
    m_impl->target = depth;
    m_impl->depthOnly = true;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::IdTint(bool enabled)
{
    m_impl->idTint = enabled;
    return *this;
}

ClusterDrawChain& ClusterDrawChain::Stats(bool enabled)
{
    m_impl->stats = enabled;
    return *this;
}

namespace {

rg::StaticName PassNameOf(const ClusterDrawState& state, std::uint32_t lane, std::uint32_t slot, ClusterPassKind kind)
{
    return rg::StaticName::Interned(state.PassName(lane, slot, kind));
}

// Decides whether the chain runs in two phases. Declares nothing; None means it does.
HizReason PlanTwoPhase(ClusterDrawChain::Impl& s, rg::StaticName anchorName)
{
    Frame& frame = *s.frame;
    detail::FrameContext& context = *frame.Context();
    if (s.pyramid == nullptr || s.lane != 0 || s.state->Shape().lanes < 2) {
        frame.ReportOnce(std::string("Pass '") + anchorName.value
            + "': a two-phase chain needs lane 0 of a state with two lanes");
        return HizReason::Unavailable;
    }
    const ClusterCamera& cullCamera = s.hasCullCamera ? s.cullCamera : s.camera;
    if (!CameraPoseEqual(cullCamera.viewProjection, cullCamera.eyeOrDirection, s.camera.viewProjection,
            s.camera.eyeOrDirection)) {
        return HizReason::Unavailable; // The depth belongs to the draw camera only.
    }
    if (!s.sceneDepth.IsValid() || !detail::CheckOwned(frame, s.sceneDepth, anchorName, "the scene depth")
        || !context.builder->HasSampleableDepth(s.sceneDepth.handle)) {
        return HizReason::Unavailable;
    }
    s.hizPlan = s.pyramid->Plan(frame, s.sceneDepth);
    return s.hizPlan.reason;
}

// Resolves everything the chain needs and fills `s.parts`. Declares nothing.
void PlanChain(ClusterDrawChain::Impl& s)
{
    Frame& frame = *s.frame;
    ClusterDrawState& state = *s.state;
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->core == nullptr) {
        frame.ReportOnce("ClusterDraw() needs a frame that belongs to a render callback");
        return;
    }
    if (context->scope == rg::ViewScope::Shared) {
        s.plan.inertReason = ClusterInertReason::SharedScope;
        frame.ReportOnce("ClusterDraw() needs a view frame; a shared frame has no camera and no target");
        return;
    }
    Core& core = *context->core;
    s.claimContext = ClusterClaimContext{ context->scope, core.CaptureArmedThisFrame() };
    if (!ClusterClaimAllowed(s.claimContext)) {
        s.plan.inertReason = ClusterInertReason::CaptureArmed;
        return; // A capture is armed: every entity stays on the CPU draw by design.
    }
    if (!state.Valid()) {
        s.plan.inertReason = ClusterInertReason::InvalidState;
        frame.ReportOnce("ClusterDraw(): the draw state is invalid: " + state.Error());
        return;
    }
    if (state.PassName(s.lane, 0, ClusterPassKind::Draw) == nullptr) {
        frame.ReportOnce("ClusterDraw(): lane " + std::to_string(s.lane) + " is outside the shape of the draw state");
        return;
    }
    const rg::StaticName anchorName = PassNameOf(state, s.lane, 0, ClusterPassKind::Draw);
    if (!state.AcceptCandidateSpan(frame, s.lane, s.instances.data(), s.instances.size())) {
        frame.ReportOnce(std::string("Pass '") + anchorName.value + "': lanes of one frame must pass the same instances");
        return;
    }

    const IndirectDrawCapsView caps = core.GetRenderer().IndirectDrawCaps();
    if (!ClusterPathSupported(caps)) {
        s.plan.inertReason = ClusterInertReason::DeviceCaps;
        frame.ReportOnce("ClusterDraw(): the device lacks multi draw indirect, indirect count or first instance; "
                         "the CPU draw is used");
        return;
    }
    if (s.instances.empty()) {
        return;
    }
    if (!s.hasCamera || !s.camera.valid || (s.hasCullCamera && !s.cullCamera.valid)) {
        frame.ReportOnce(std::string("Pass '") + anchorName.value + "': the camera is missing or not finite");
        return;
    }
    if (!s.target.IsValid() || !detail::CheckOwned(frame, s.target, anchorName, "the target")) {
        return;
    }

    const ClusterCamera& cullCamera = s.hasCullCamera ? s.cullCamera : s.camera;
    const ClusterCullFlags flags = static_cast<ClusterCullFlags>(
        static_cast<std::uint32_t>(EffectiveClusterCullFlags(s.flags, s.depthOnly)) & ~kChainOwnedFlags);
    if (!ClusterCullParamsAreFinite(MakeClusterCullParams(
            cullCamera.viewProjection, cullCamera.eyeOrDirection, cullCamera.orthographic, 1, 1, flags))) {
        frame.ReportOnce(std::string("Pass '") + anchorName.value + "': the cull camera is not finite");
        return;
    }

    // Without a usable pyramid the chain is the plain single-phase chain.
    if (s.wantsTwoPhase && !s.depthOnly) {
        s.hizReason = PlanTwoPhase(s, anchorName);
        s.twoPhase = s.hizReason == HizReason::None;
    }
    const std::uint32_t rounds = s.twoPhase ? 2 : 1;

    s.pipeline = state.ResolvePipeline(*context, ClusterPipelineKeyFor(flags, s.depthOnly, s.idTint));
    if (s.pipeline == nullptr || !frame.ShaderReady(state.CullShader())) {
        return;
    }
    s.bindings = state.Bindings(*context);
    if (s.bindings == nullptr) {
        frame.ReportOnce(std::string("Pass '") + anchorName.value + "': the descriptor sets could not be allocated");
        return;
    }

    // Meshes that can run a chain, with their sizes.
    const ClusterLimits limits = ClusterLimitsFor(caps);
    std::vector<ClusterMeshGroup> groups = GroupInstancesByMesh(s.instances);
    std::vector<ClusterMeshGroup> eligible;
    std::vector<ClusterPartSize> sizes;
    std::vector<std::uint32_t> clusterCounts;
    for (ClusterMeshGroup& group : groups) {
        // A mesh without a table gets a one-cluster table here, once; a failure backs off inside Core.
        if (!core.EnsureClusterTable(group.mesh, frame.FrameIndex())) {
            continue;
        }
        const Mesh* mesh = core.ResolveClusterMesh(group.mesh);
        if (mesh == nullptr || !mesh->HasClusters() || !mesh->HasIndexBuffer() || mesh->ClusterTable() == nullptr) {
            continue;
        }
        const std::uint64_t instanceCount = group.instances.size();
        const ClusterPartVerdict verdict = EvaluateClusterPart(instanceCount, mesh->ClusterCount(), limits);
        if (verdict == ClusterPartVerdict::NotWorthIt) {
            continue;
        }
        if (verdict != ClusterPartVerdict::Ok) {
            frame.ReportOnce(std::string("Pass '") + anchorName.value + "': " + TokenText(group.mesh)
                + " is over a cluster draw limit; the CPU draw is used");
            continue;
        }
        sizes.push_back(ClusterPartSize{ group.mesh, instanceCount, mesh->ClusterCount() });
        clusterCounts.push_back(mesh->ClusterCount());
        eligible.push_back(std::move(group));
    }

    const ClusterSlotAssignment assignment = AssignClusterPartSlots(sizes, state.Shape().partSlots);
    if (!assignment.dropped.empty()) {
        frame.ReportWarningOnce(
            std::string("Pass '") + anchorName.value + "': more meshes than part slots; the rest use the CPU draw");
    }

    std::uint32_t slot = 0;
    std::uint32_t toggledOffParts = 0;
    for (const std::uint32_t index : assignment.assigned) {
        const std::uint32_t thisSlot = slot++;
        const ClusterMeshGroup& group = eligible[index];
        const std::uint64_t instanceCount = group.instances.size();
        const std::uint64_t workItems = instanceCount * clusterCounts[index];

        // All-or-none: a toggled-off pass of the chain means nothing of this part is touched.
        bool toggledOff = false;
        for (std::uint32_t round = 0; round < rounds; ++round) {
            toggledOff = toggledOff || !frame.PassEnabled(PassNameOf(state, s.lane + round, thisSlot, ClusterPassKind::Reset))
                || !frame.PassEnabled(PassNameOf(state, s.lane + round, thisSlot, ClusterPassKind::Cull))
                || !frame.PassEnabled(PassNameOf(state, s.lane + round, thisSlot, ClusterPassKind::Draw));
        }
        if (toggledOff) {
            ++toggledOffParts;
            continue;
        }
        if (state.TryReserveChainBytes(context->scope, s.lane, thisSlot, instanceCount, workItems, s.twoPhase)
            != ClusterReserveResult::Ok) {
            frame.ReportOnce(std::string("Pass '") + anchorName.value + "': " + TokenText(group.mesh)
                + " is over the cluster draw memory budget; the CPU draw is used");
            continue;
        }

        PlannedPart part;
        part.mesh = group.mesh;
        part.slot = thisSlot;
        part.clusterCount = clusterCounts[index];
        part.workItems = workItems;
        part.instances.reserve(group.instances.size());
        part.entities.reserve(group.instances.size());
        for (const std::uint32_t candidate : group.instances) {
            const ClusterInstance& instance = s.instances[candidate];
            part.instances.push_back(MakeClusterInstanceGpu(instance.world, instance.flags, instance.maxScale));
            part.entities.push_back(instance.entity);
        }
        part.params = MakeClusterCullParams(cullCamera.viewProjection, cullCamera.eyeOrDirection,
            cullCamera.orthographic, static_cast<std::uint32_t>(instanceCount), part.clusterCount, flags);
        s.parts.push_back(std::move(part));
    }
    if (s.parts.empty()) {
        if (toggledOffParts > 0) {
            s.plan.inertReason = ClusterInertReason::ToggledOff;
        }
        return;
    }

    // A depth-only user skips the taken-over entities in its own CPU draw; the set is filled by Record().
    if (s.depthOnly) {
        s.plan.exclusion = core.ReserveExclusion();
        if (s.plan.exclusion.handle == nullptr) {
            frame.ReportOnce(std::string("Pass '") + anchorName.value + "': no exclusion set is left this frame");
            s.parts.clear();
            return;
        }
    }
    s.plan.parts = static_cast<std::uint32_t>(s.parts.size());
    s.plan.ready = true;
}

// Buffers and bindings of one part, acquired once per frame.
struct PartRuntime {
    GpuBufferRef instances;
    std::array<GpuBufferRef, 2> clusters, visible, commands, count; // [round]
    std::array<Texture, 2> hizDummy;                                // [round]; round 1 of a two-phase chain binds the pyramid
    GpuBufferRef history;                                           // The slot's history, or a stand-in.
    bool historyValid = false;
    std::uint64_t historyKey = 0;
    GpuReadback<std::uint32_t>* readback = nullptr;
};

// Declares the passes of a chain, one round of one part at a time. See RecordClusterRounds().
class ChainDeclarer {
public:
    ChainDeclarer(ClusterDrawChain::Impl& chain)
        : s(chain)
        , runtime(chain.parts.size())
    {
    }

    ClusterDrawChain::Impl& s;
    std::vector<PartRuntime> runtime;
    std::size_t statsPart = 0;

    std::uint32_t LaneOf(std::uint32_t round) const noexcept { return s.lane + round; }

    // Acquires the buffers of a part. False leaves the part on the CPU draw.
    bool Begin(std::size_t index)
    {
        Frame& frame = *s.frame;
        ClusterDrawState& state = *s.state;
        detail::FrameContext& context = *frame.Context();
        const PlannedPart& part = s.parts[index];
        PartRuntime& run = runtime[index];

        const Mesh* mesh = context.core->ResolveClusterMesh(part.mesh);
        if (mesh == nullptr || mesh->ClusterTable() == nullptr) {
            assert(false && "a planned cluster mesh vanished within one frame");
            frame.ReportOnce("ClusterDraw(): a planned mesh is no longer available; the CPU draw is used");
            return false;
        }

        // Acquire imports each buffer once per frame; nothing is declared yet.
        PersistentBuffer* instanceBuffer = state.Instances(part.slot);
        const std::uint64_t instanceBytes = part.instances.size() * sizeof(ClusterInstanceGpu);
        run.instances = instanceBuffer->Acquire(frame, instanceBytes);
        if (!run.instances.IsValid()) {
            return false;
        }
        if (state.NeedsInstanceUpload(frame, part.slot)) {
            if (!instanceBuffer->Upload(frame, part.instances.data(), instanceBytes)) {
                return false;
            }
            state.NoteInstanceUploaded(frame, part.slot);
        }

        const std::uint32_t rounds = s.twoPhase ? 2 : 1;
        for (std::uint32_t round = 0; round < rounds; ++round) {
            const std::uint32_t lane = LaneOf(round);
            run.visible[round] = state.Visible(lane, part.slot)->Acquire(frame, part.workItems * kClusterVisibleBytes);
            run.commands[round] = state.Commands(lane, part.slot)->Acquire(frame, part.workItems * kClusterCommandBytes);
            run.count[round] = state.Count(lane, part.slot)->Acquire(frame, sizeof(std::uint32_t));
            if (!run.visible[round].IsValid() || !run.commands[round].IsValid() || !run.count[round].IsValid()) {
                return false;
            }
            const rg::BufferHandle table =
                context.builder->Import(*mesh->ClusterTable(), state.TableName(context.scope, lane, part.slot));
            run.clusters[round] = frame.StampBuffer(
                table, std::uint64_t{ part.clusterCount } * sizeof(MeshCluster), BufferUse::Storage);
            run.hizDummy[round] = round == 0 ? state.HizDummy(frame, lane) : Texture{};
        }
        if (!run.hizDummy[0].IsValid()) {
            return false;
        }

        if (!s.twoPhase) {
            run.history = state.HistoryDummy(frame, s.lane);
            return run.history.IsValid();
        }
        PersistentBuffer* historyBuffer = state.History(part.slot);
        run.history = historyBuffer->Acquire(frame, part.workItems * kClusterHistoryBytes);
        if (!run.history.IsValid()) {
            return false;
        }
        std::vector<std::uint32_t> entityWords;
        entityWords.reserve(part.entities.size() * 2);
        for (const Entity& entity : part.entities) {
            entityWords.push_back(entity.index);
            entityWords.push_back(entity.generation);
        }
        const std::uint64_t meshToken = (std::uint64_t{ part.mesh.index } << 32) | part.mesh.generation;
        run.historyKey = ClusterHistoryKey(meshToken, part.clusterCount, entityWords, historyBuffer->Generation(frame));
        run.historyValid = ClusterHistoryUsable(state.HistoryRecord(frame, part.slot), run.historyKey, frame.FrameIndex());
        // Valid again only once both rounds of this frame were declared.
        state.DropHistory(frame, part.slot);
        return true;
    }

    bool Reset(std::size_t index, std::uint32_t round)
    {
        const PlannedPart& part = s.parts[index];
        return s.frame->FillBuffer(PassNameOf(*s.state, LaneOf(round), part.slot, ClusterPassKind::Reset),
            runtime[index].count[round], 0);
    }

    bool Cull(std::size_t index, std::uint32_t round)
    {
        ClusterDrawState& state = *s.state;
        const PlannedPart& part = s.parts[index];
        const PartRuntime& run = runtime[index];
        const std::uint32_t lane = LaneOf(round);

        ClusterCullParams params = part.params;
        if (round == 1) {
            SetClusterCullHiz(params, s.hizPlan.layout);
            params.counts[3] |= kClusterCullFlagPhaseSecond;
        } else if (s.twoPhase) {
            params.counts[3] |= kClusterCullFlagPhaseFirst | (run.historyValid ? kClusterCullFlagHistoryValid : 0u);
        }
        const Texture pyramid = round == 1 ? s.atlas : run.hizDummy[0];
        return s.frame->Compute(state.CullShader(), PassNameOf(state, lane, part.slot, ClusterPassKind::Cull))
            .Params(*state.Params(s.frame->Context()->scope, lane, part.slot), params)
            .BindBuffer("instances", run.instances)
            .BindBuffer("clusters", run.clusters[round])
            .BindBuffer("visible", run.visible[round])
            .BindBuffer("commands", run.commands[round])
            .BindBuffer("count", run.count[round])
            .BindBuffer("history", run.history)
            .Bind("hizAtlas", pyramid)
            .Dispatch(Extent2D{ static_cast<std::uint32_t>(part.workItems), 1 });
    }

    bool Draw(std::size_t index, std::uint32_t round)
    {
        detail::FrameContext& context = *s.frame->Context();
        const PlannedPart& part = s.parts[index];
        const PartRuntime& run = runtime[index];
        const std::uint32_t lane = LaneOf(round);
        const rg::StaticName drawName = PassNameOf(*s.state, lane, part.slot, ClusterPassKind::Draw);
        if (!detail::PassEnabled(context, drawName)) {
            return false;
        }
        rg::IndirectDrawRecord record;
        record.target = s.target.handle;
        record.indirect = run.commands[round].handle;
        record.count = run.count[round].handle;
        record.instances = run.instances.handle;
        record.visible = run.visible[round].handle;
        record.maxDrawCount = static_cast<std::uint32_t>(part.workItems);
        record.depthOnly = s.depthOnly;

        Core* core = context.core;
        const MeshToken token = part.mesh;
        const Pipeline* pipeline = s.pipeline;
        rg::ClusterDrawBindings* bindings = s.bindings;
        const std::size_t bindingIndex = s.state->BindingIndex(context.scope, lane, part.slot);
        const rg::RenderViewId view = context.view;
        const bool depthOnly = s.depthOnly;
        const Mat4 viewProjection = s.camera.viewProjection;
        // The mesh is looked up again at execute time; a missing one draws nothing.
        auto resolveInputs = [core, token, pipeline, bindings, bindingIndex, view, depthOnly, viewProjection]() {
            rg::IndirectDrawInputs inputs;
            inputs.mesh = core->ResolveClusterMesh(token);
            inputs.pipeline = pipeline;
            inputs.viewProjection = viewProjection;
            inputs.clusterBindings = bindings;
            inputs.clusterBindingIndex = bindingIndex;
            if (!depthOnly) {
                inputs.sceneSet = core->GetSceneServicesDescriptorSet().DescriptorSetFor(view);
            }
            return inputs;
        };

        std::optional<rg::PassBody> body =
            rg::MakeIndirectDrawBody(*context.builder, drawName.value, record, std::move(resolveInputs));
        if (!body) {
            return false;
        }
        context.builder->AddRenderPass(drawName.value, rg::DefaultPassKind(rg::DeclarativePassType::IndirectDraw),
            context.scope, rg::RenderPassCategory::General, std::move(body->setup), std::move(body->execute),
            rg::DefaultDrawKind(rg::DeclarativePassType::IndirectDraw), context.event);
        return true;
    }

    // Declares the pyramid from the depth the first round drew.
    bool Build()
    {
        s.atlas = s.pyramid->Build(*s.frame, s.hizPlan, s.sceneDepth);
        if (!s.atlas.IsValid()) {
            s.frame->ReportOnce("ClusterDraw(): the occlusion pyramid was refused; the CPU draw is used");
            return false;
        }
        return true;
    }

    // Stats are optional and declared as one unit: clear, dispatch, host copy. They run on the last lane of the chain.
    std::uint32_t StatsLane() const noexcept { return LaneOf(s.twoPhase ? 1 : 0); }

    bool StatsClear()
    {
        ClusterDrawState& state = *s.state;
        const std::uint32_t slot = s.parts[statsPart].slot;
        const std::uint32_t lane = StatsLane();
        PartRuntime& run = runtime[statsPart];
        run.readback = state.Stats(slot);
        if (run.readback == nullptr || !s.frame->ShaderReady(state.StatsShader())
            || !s.frame->PassEnabled(PassNameOf(state, lane, slot, ClusterPassKind::Stats))
            || !s.frame->PassEnabled(rg::StaticName::Interned(state.StatsClearName(lane, slot)))
            || !s.frame->PassEnabled(rg::StaticName::Interned(state.StatsCopyName(lane, slot)))) {
            return false;
        }
        return s.frame->ClearReadback(rg::StaticName::Interned(state.StatsClearName(lane, slot)), *run.readback, 0u);
    }

    bool StatsCompute()
    {
        ClusterDrawState& state = *s.state;
        const PartRuntime& run = runtime[statsPart];
        const std::uint32_t round = s.twoPhase ? 1 : 0;
        const bool dispatched =
            s.frame->Compute(state.StatsShader(), PassNameOf(state, StatsLane(), s.parts[statsPart].slot, ClusterPassKind::Stats))
                .BindBuffer("count", run.count[0])
                .BindBuffer("secondCount", run.count[round])
                .BindOutput("outCount", *run.readback)
                .Push(ClusterStatsPush{ s.twoPhase ? 1u : 0u })
                .Dispatch(Extent2D{ 1, 1 });
        if (!dispatched) {
            s.frame->ReportOnce("ClusterDraw(): the stats dispatch was refused; the visible count is not updated");
        }
        return dispatched;
    }

    bool StatsCopy()
    {
        ClusterDrawState& state = *s.state;
        return s.frame->CopyReadbackToHost(
            rg::StaticName::Interned(state.StatsCopyName(StatsLane(), s.parts[statsPart].slot)), *runtime[statsPart].readback);
    }
};

void RecordChain(ClusterDrawChain::Impl& s)
{
    Frame& frame = *s.frame;
    Core& core = *frame.Context()->core;
    ChainDeclarer declarer(s);
    const std::vector<ClusterStep> steps = RecordClusterRounds(declarer, s.parts.size(), s.twoPhase);

    const bool statsWanted = ClusterStatsWanted(s.stats, s.depthOnly, s.lane);
    std::vector<std::span<const Entity>> partEntities;
    partEntities.reserve(s.parts.size());
    for (std::size_t i = 0; i < s.parts.size(); ++i) {
        const PlannedPart& part = s.parts[i];
        partEntities.push_back(part.entities);
        if (ClusterProgressOf(steps[i], s.twoPhase) != ClusterChainProgress::Draw) {
            frame.ReportOnce(std::string("Pass '") + PassNameOf(*s.state, s.lane, part.slot, ClusterPassKind::Draw).value
                + "': the chain stopped before the draw; the CPU draw is used for this mesh");
            continue;
        }
        if (s.twoPhase) {
            s.state->NoteHistoryWritten(frame, part.slot, declarer.runtime[i].historyKey);
        }
        // A refused stats pass never changes the progress, so it cannot revoke a claim.
        if (statsWanted) {
            declarer.statsPart = i;
            RecordClusterStats(declarer);
        }
    }
    const ClusterRecordOutcome outcome = ClusterOutcomeOf(partEntities, s.claimContext, steps, s.twoPhase);

    // The takeover is the last step, after every pass of a part was declared.
    if (!outcome.claimed.empty()) {
        if (s.depthOnly) {
            core.FillExclusion(s.plan.exclusion, outcome.claimed);
        } else {
            core.ClaimEntities(s.claimContext.scope, outcome.claimed);
        }
    }
    for (std::size_t i = 0; i < s.parts.size(); ++i) {
        if (outcome.progress[i] == ClusterChainProgress::Draw) {
            s.result.declared = true;
            s.result.instances += static_cast<std::uint32_t>(s.parts[i].instances.size());
            ++s.result.parts;
            s.result.workItems += s.parts[i].workItems;
        }
    }
}

} // namespace

ClusterPlan ClusterDrawChain::Plan()
{
    Impl& s = *m_impl;
    if (s.planned) {
        return s.plan;
    }
    s.planned = true;
    try {
        PlanChain(s);
    } catch (const std::exception& e) {
        s.parts.clear();
        s.plan = ClusterPlan{};
        s.twoPhase = false;
        s.frame->ReportOnce(std::string("ClusterDraw() could not plan: ") + e.what());
    } catch (...) {
        s.parts.clear();
        s.plan = ClusterPlan{};
        s.twoPhase = false;
        s.frame->ReportOnce("ClusterDraw() could not plan: unknown error");
    }
    return s.plan;
}

ClusterStats ClusterDrawChain::Record()
{
    Impl& s = *m_impl;
    if (s.recorded) {
        return s.result;
    }
    s.recorded = true;
    Plan();

    if (s.plan.ready && !s.parts.empty()) {
        try {
            RecordChain(s);
        } catch (const std::exception& e) {
            s.result = ClusterStats{};
            s.frame->ReportOnce(std::string("ClusterDraw() could not record: ") + e.what());
        } catch (...) {
            s.result = ClusterStats{};
            s.frame->ReportOnce("ClusterDraw() could not record: unknown error");
        }
    }
    s.result.inertReason = s.plan.inertReason;
    s.result.hizReason = s.wantsTwoPhase && !s.depthOnly ? s.hizReason : HizReason::None;
    s.result.occlusionApplied = s.result.declared && s.twoPhase;

    // Idle bookkeeping of the frame's scope; release only runs in the synchronous regime.
    s.state->TickIdle(*s.frame, s.result.declared);
    return s.result;
}

} // namespace gte::fx
