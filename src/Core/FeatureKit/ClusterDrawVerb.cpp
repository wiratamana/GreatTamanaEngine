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

    // Hi-Z input of Occlusion().
    Texture hiz;
    Mat4 prevViewProjection = Mat4::Identity();
    std::uint32_t hizBaseWidth = 0;
    std::uint32_t hizBaseHeight = 0;
    bool hasOcclusion = false;

    // Hi-Z decisions made by Plan().
    HizLayout hizLayout;
    Texture hizBinding; // The pyramid, or a 1x1 stand-in while occlusion is off.
    bool occlusionActive = false;

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

ClusterDrawChain& ClusterDrawChain::Occlusion(
    Texture hizAtlas, const Mat4& prevViewProjection, std::uint32_t baseWidth, std::uint32_t baseHeight)
{
    m_impl->hiz = hizAtlas;
    m_impl->prevViewProjection = prevViewProjection;
    m_impl->hizBaseWidth = baseWidth;
    m_impl->hizBaseHeight = baseHeight;
    m_impl->hasOcclusion = true;
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
    ClusterCullFlags flags = EffectiveClusterCullFlags(s.flags, s.depthOnly);
    if (!ClusterCullParamsAreFinite(MakeClusterCullParams(
            cullCamera.viewProjection, cullCamera.eyeOrDirection, cullCamera.orthographic, 1, 1, flags))) {
        frame.ReportOnce(std::string("Pass '") + anchorName.value + "': the cull camera is not finite");
        return;
    }

    // The Occlusion test needs a pyramid of this view; without a usable one only that test is dropped.
    if (HasClusterCullFlag(flags, ClusterCullFlags::Occlusion)) {
        s.hizLayout = HizLayoutFor(s.hizBaseWidth * 2u, s.hizBaseHeight * 2u);
        const bool usable = !s.depthOnly && s.hasOcclusion && s.hizLayout.IsValid() && s.hiz.IsValid()
            && CameraIsFinite(s.prevViewProjection, Vec3(0.0f, 0.0f, 0.0f));
        if (!usable) {
            flags = static_cast<ClusterCullFlags>(
                static_cast<std::uint32_t>(flags) & ~static_cast<std::uint32_t>(ClusterCullFlags::Occlusion));
        } else if (!detail::CheckOwned(frame, s.hiz, anchorName, "the occlusion pyramid")) {
            return;
        } else {
            s.hizBinding = s.hiz;
            s.occlusionActive = true;
        }
    }
    if (!s.hizBinding.IsValid()) {
        s.hizBinding = state.HizDummy(frame, s.lane);
        if (!s.hizBinding.IsValid()) {
            return;
        }
    }

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
        frame.ReportOnce(std::string("Pass '") + anchorName.value + "': more meshes than part slots; the rest use the CPU draw");
    }

    std::uint32_t slot = 0;
    std::uint32_t toggledOffParts = 0;
    for (const std::uint32_t index : assignment.assigned) {
        const std::uint32_t thisSlot = slot++;
        const ClusterMeshGroup& group = eligible[index];
        const std::uint64_t instanceCount = group.instances.size();
        const std::uint64_t workItems = instanceCount * clusterCounts[index];

        // All-or-none: a toggled-off pass of the chain means nothing of this part is touched.
        if (!frame.PassEnabled(PassNameOf(state, s.lane, thisSlot, ClusterPassKind::Reset))
            || !frame.PassEnabled(PassNameOf(state, s.lane, thisSlot, ClusterPassKind::Cull))
            || !frame.PassEnabled(PassNameOf(state, s.lane, thisSlot, ClusterPassKind::Draw))) {
            ++toggledOffParts;
            continue;
        }
        if (state.TryReserveChainBytes(context->scope, s.lane, thisSlot, instanceCount, workItems)
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
        if (s.occlusionActive) {
            SetClusterCullHiz(part.params, s.prevViewProjection, s.hizLayout);
        }
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

// Declares the passes of one part. Returns how far the chain got.
ClusterChainProgress DeclarePart(ClusterDrawChain::Impl& s, const PlannedPart& part)
{
    Frame& frame = *s.frame;
    ClusterDrawState& state = *s.state;
    detail::FrameContext& context = *frame.Context();
    Core& core = *context.core;
    const std::uint32_t lane = s.lane;
    const std::uint32_t slot = part.slot;

    const Mesh* mesh = core.ResolveClusterMesh(part.mesh);
    if (mesh == nullptr || mesh->ClusterTable() == nullptr) {
        assert(false && "a planned cluster mesh vanished within one frame");
        frame.ReportOnce("ClusterDraw(): a planned mesh is no longer available; the CPU draw is used");
        return ClusterChainProgress::None;
    }

    // Buffers. Acquire imports each one once per frame; nothing is declared yet.
    PersistentBuffer* instanceBuffer = state.Instances(slot);
    const std::uint64_t instanceBytes = part.instances.size() * sizeof(ClusterInstanceGpu);
    const GpuBufferRef instances = instanceBuffer->Acquire(frame, instanceBytes);
    if (!instances.IsValid()) {
        return ClusterChainProgress::None;
    }
    if (state.NeedsInstanceUpload(frame, slot)) {
        if (!instanceBuffer->Upload(frame, part.instances.data(), instanceBytes)) {
            return ClusterChainProgress::None;
        }
        state.NoteInstanceUploaded(frame, slot);
    }
    const GpuBufferRef visible = state.Visible(lane, slot)->Acquire(frame, part.workItems * kClusterVisibleBytes);
    const GpuBufferRef commands = state.Commands(lane, slot)->Acquire(frame, part.workItems * kClusterCommandBytes);
    const GpuBufferRef count = state.Count(lane, slot)->Acquire(frame, sizeof(std::uint32_t));
    if (!visible.IsValid() || !commands.IsValid() || !count.IsValid()) {
        return ClusterChainProgress::None;
    }
    const rg::BufferHandle tableHandle = context.builder->Import(*mesh->ClusterTable(), state.TableName(context.scope, lane, slot));
    const GpuBufferRef clusters = frame.StampBuffer(
        tableHandle, std::uint64_t{ part.clusterCount } * sizeof(MeshCluster), BufferUse::Storage);

    struct Declarer {
        ClusterDrawChain::Impl& s;
        const PlannedPart& part;
        detail::FrameContext& context;
        GpuBufferRef instances, visible, commands, count, clusters;
        rg::StaticName resetName, cullName, drawName;
        GpuReadback<std::uint32_t>* readback = nullptr;

        bool Reset() { return s.frame->FillBuffer(resetName, count, 0); }

        bool Cull()
        {
            ClusterDrawState& state = *s.state;
            return s.frame->Compute(state.CullShader(), cullName)
                .Params(*state.Params(context.scope, s.lane, part.slot), part.params)
                .BindBuffer("instances", instances)
                .BindBuffer("clusters", clusters)
                .BindBuffer("visible", visible)
                .BindBuffer("commands", commands)
                .BindBuffer("count", count)
                .Bind("hizAtlas", s.hizBinding)
                .Dispatch(Extent2D{ static_cast<std::uint32_t>(part.workItems), 1 });
        }

        bool Draw()
        {
            if (!detail::PassEnabled(context, drawName)) {
                return false;
            }
            rg::IndirectDrawRecord record;
            record.target = s.target.handle;
            record.indirect = commands.handle;
            record.count = count.handle;
            record.instances = instances.handle;
            record.visible = visible.handle;
            record.maxDrawCount = static_cast<std::uint32_t>(part.workItems);
            record.useCount = true;
            record.depthOnly = s.depthOnly;

            Core* core = context.core;
            const MeshToken token = part.mesh;
            const Pipeline* pipeline = s.pipeline;
            rg::ClusterDrawBindings* bindings = s.bindings;
            const std::size_t bindingIndex = s.state->BindingIndex(context.scope, s.lane, part.slot);
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

        // Stats are optional and declared as one unit: clear, dispatch, host copy.
        bool StatsClear()
        {
            ClusterDrawState& state = *s.state;
            const std::uint32_t lane = s.lane;
            readback = state.Stats(part.slot);
            if (readback == nullptr || !s.frame->ShaderReady(state.StatsShader())
                || !s.frame->PassEnabled(PassNameOf(state, lane, part.slot, ClusterPassKind::Stats))
                || !s.frame->PassEnabled(rg::StaticName::Interned(state.StatsClearName(lane, part.slot)))
                || !s.frame->PassEnabled(rg::StaticName::Interned(state.StatsCopyName(lane, part.slot)))) {
                return false;
            }
            return s.frame->ClearReadback(rg::StaticName::Interned(state.StatsClearName(lane, part.slot)), *readback, 0u);
        }

        bool StatsCompute()
        {
            ClusterDrawState& state = *s.state;
            const bool dispatched =
                s.frame->Compute(state.StatsShader(), PassNameOf(state, s.lane, part.slot, ClusterPassKind::Stats))
                    .BindBuffer("count", count)
                    .BindOutput("outCount", *readback)
                    .Dispatch(Extent2D{ 1, 1 });
            if (!dispatched) {
                s.frame->ReportOnce("ClusterDraw(): the stats dispatch was refused; the visible count is not updated");
            }
            return dispatched;
        }

        bool StatsCopy()
        {
            ClusterDrawState& state = *s.state;
            return s.frame->CopyReadbackToHost(rg::StaticName::Interned(state.StatsCopyName(s.lane, part.slot)), *readback);
        }
    };

    Declarer declarer{ s, part, context, instances, visible, commands, count, clusters,
        PassNameOf(state, lane, slot, ClusterPassKind::Reset), PassNameOf(state, lane, slot, ClusterPassKind::Cull),
        PassNameOf(state, lane, slot, ClusterPassKind::Draw) };
    const ClusterChainProgress progress = RecordClusterPart(declarer, ClusterStatsWanted(s.stats, s.depthOnly, lane));
    if (progress != ClusterChainProgress::Draw) {
        frame.ReportOnce(std::string("Pass '") + declarer.drawName.value
            + "': the chain stopped before the draw; the CPU draw is used for this mesh");
    }
    return progress;
}

void RecordChain(ClusterDrawChain::Impl& s)
{
    Core& core = *s.frame->Context()->core;
    std::vector<ClusterPartPlan> plans;
    plans.reserve(s.parts.size());
    for (const PlannedPart& part : s.parts) {
        plans.push_back(ClusterPartPlan{ part.entities, true, ClusterChainToggles{} });
    }
    const ClusterRecordOutcome outcome = RecordClusterParts(
        plans, s.claimContext, [&](std::size_t index) { return DeclarePart(s, s.parts[index]); });

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
        s.frame->ReportOnce(std::string("ClusterDraw() could not plan: ") + e.what());
    } catch (...) {
        s.parts.clear();
        s.plan = ClusterPlan{};
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

    // Idle bookkeeping of the frame's scope; release only runs in the synchronous regime.
    const detail::FrameContext* context = s.frame->Context();
    if (context != nullptr && context->scope != rg::ViewScope::Shared && s.state->Valid()) {
        s.state->NoteFrame(*s.frame, s.result.declared);
        s.state->ReleaseIdleScope(*s.frame);
    }
    return s.result;
}

} // namespace gte::fx
