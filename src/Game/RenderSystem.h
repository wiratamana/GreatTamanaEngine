#pragma once

#include "DrawCommand.h"
#include "RenderBatching.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Math/Mat4.h"
#include "Math/Vec3.h"
#include "Renderer/Culling/GpuDrivenBatchCache.h"
#include "Renderer/MaterialTexture.h"
#include "Renderer/Mesh.h"
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"
#include "Renderer/ResourcePool.h"
#include "Renderer/TextureHandle.h"
// RenderSystem.h now #includes SceneQuery.h, for PipelineOverrideSet's full
// definition - a std::optional<PipelineOverrideSet> function parameter below
// needs a complete type. SceneQuery.h only forward-declares RenderSystem (no
// #include of RenderSystem.h), so this is a one-directional include only -
// never let SceneQuery.h #include RenderSystem.h back (only SceneQuery.cpp,
// a translation unit, may do that), or this becomes a real header cycle.
#include "SceneQuery.h"
// editor-core-separation-2 campaign, PHASE2 - RenderSystem.h now #includes
// the gte_core-owned IFrameDebuggerCaptureRecorder interface (src/Core/
// FrameDebuggerCaptureRecorder.h) instead of forward-declaring the concrete,
// gte_editor-only FrameDebuggerCaptureContext type - see that header's own
// doc comment and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.
// Draw() below only ever needs a POINTER to the interface, calling its
// RecordFrameDebuggerDraw() virtual method instead of the old
// gte_editor-only free function gte::RecordFrameDebuggerDraws() (which is
// GONE - see this header's own updated call-site doc comment below, and
// RenderSystem.cpp). MUST be included here, at file scope (NOT from inside
// `namespace gte { ... }` below) - this header opens its own
// `namespace gte { ... }` block, and including it from inside an
// already-open `namespace gte { ... }` here would create a bogus nested
// `gte::gte` namespace instead of extending the real `gte` namespace.
#include "../Core/FrameDebuggerCaptureRecorder.h"

#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>

// Per-draw-call granularity (Phase C) - RenderSystem::Draw() only ever needs
// a POINTER to CommandBuffer, forwarded in from whichever render-graph pass
// is already recording (see Draw()'s own `cmd` parameter below), so a plain
// forward declare is enough - never #include CommandBuffer.h from a header.
namespace gte::rg {
class CommandBuffer;
}

namespace gte {

class Renderer;

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE4 - one eligible batch's own frame-local summary, returned by
// RenderSystem::CollectGpuDrivenBatches() below. Deliberately NOT stored
// inside GpuDrivenBatchCache itself (that class owns only PERSISTENT GPU
// buffers, surviving across frames, keyed by GpuDrivenBatchKey alone) - this
// struct is a fresh, frame-scoped snapshot, rebuilt on every call, carrying
// exactly what PHASE5's future render-graph pass-declaration code needs:
// which (MeshHandle, PipelineHandle) batch this is, how many instances it
// has THIS frame (see GpuDrivenBatchCache::EnsureCapacity()'s own doc
// comment for why this must never be confused with the cache's own,
// monotonically-growing buffer capacity), and every original DrawCommand it
// replaces (so a caller can build a batchedEntities exclusion set from each
// one's own `entity` field).
struct GpuDrivenBatchFrameEntry {
    MeshHandle mesh;
    PipelineHandle pipeline;
    std::size_t instanceCount = 0;
    std::vector<DrawCommand> commands;
};

// The "middleman" between the ECS world and Renderer (see AGENTS.md, Clean
// Architecture): the ONLY thing in the engine allowed to depend on both
// Registry/Transform/MeshRenderer AND Renderer/Mesh/Pipeline. The dependency
// only ever points this one direction - Renderer itself never depends on
// ECS in any way (Renderer::Submit() takes a plain Mat4, never an
// Entity/Registry), matching the same "only Application knows about SDL"
// boundary rule this engine already applies elsewhere.
//
// Owns the actual Mesh/Pipeline/MaterialTexture objects Game creates via
// Renderer::CreateMesh()/CreatePipeline()/CreateMaterialTexture2D() - still
// returned BY VALUE exactly as before (Renderer's own factory API is
// unaffected in shape) - addressed by the MeshHandle/PipelineHandle/
// TextureHandle a MeshRenderer component can safely hold instead of ever
// embedding one of these directly.
class RenderSystem {
public:
    RenderSystem() = default;

    // Takes ownership of a Mesh/Pipeline/MaterialTexture Game already
    // created via Renderer::CreateMesh()/CreatePipeline()/
    // CreateMaterialTexture2D(), returning the handle a MeshRenderer
    // component should store.
    MeshHandle RegisterMesh(Mesh&& mesh) { return m_meshes.Insert(std::move(mesh)); }
    PipelineHandle RegisterPipeline(Pipeline&& pipeline) { return m_pipelines.Insert(std::move(pipeline)); }
    TextureHandle RegisterTexture(MaterialTexture&& texture) { return m_textures.Insert(std::move(texture)); }

    // Frees a previously-registered Pipeline's slot. Only safe to call for a
    // handle guaranteed to be unreferenced by any entity's MeshRenderer -
    // this pool tracks no back-references, so removing a handle still in
    // use elsewhere just makes those entities silently stop drawing
    // (TryGet() returns nullptr, Draw() skips them like any stale handle).
    // Exists for same-call rollback of a just-inserted, not-yet-exposed
    // pipeline (see ShadowFeature::EnsureDepthPipelinesBuilt()) - not a
    // general-purpose live-unload API.
    bool UnregisterPipeline(PipelineHandle handle) { return m_pipelines.Remove(handle); }

    // Direct, mutable access to an already-registered Mesh by handle -
    // needed by Game::UpdateSkeletalAnimators() (src/Game/Game.cpp) to call
    // Mesh::UpdateVertexData() on a rigged model's parts every frame as its
    // pose animates. Returns nullptr for an invalid/stale/never-registered
    // handle (best-effort, same "never assert on a bad handle" convention
    // as Draw() below) - the caller should simply skip that part for this
    // frame rather than crash.
    Mesh* TryGetMesh(MeshHandle handle) { return m_meshes.TryGet(handle); }

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE5 (task_manager/render-pass-5/
    // PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md) - the
    // Pipeline sibling of TryGetMesh() above, needed by Application's new
    // "GpuDrivenBatches" provider to resolve a batch's own ORIGINAL
    // Pipeline& (e.g. to build/resolve its VertexLayout::
    // PositionNormalInstanced sibling) - see const Pipeline* TryGetMesh's
    // own identical doc comment for the "never assert on a bad handle"
    // convention this mirrors.
    const Pipeline* TryGetPipeline(PipelineHandle handle) const { return m_pipelines.TryGet(handle); }

    // Pure data-collection step: every entity with a MeshRenderer becomes
    // one DrawCommand, using ECS/TransformHierarchy.h's ComputeWorldMatrix()
    // (its Transform's LocalToWorldMatrix() composed all the way up its
    // parent chain, if any - Mat4::Identity() if it has no Transform at
    // all). Touches nothing but `registry` - no Renderer, no live GPU
    // resources - so this alone is Tier-1-testable (see
    // tests/Game/RenderSystemTests.cpp) even though RenderSystem as a whole
    // is not (it owns real Mesh/Pipeline objects).
    static std::vector<DrawCommand> CollectRenderables(Registry& registry);

    // Render Pass campaign (task_manager/render-pass-1), PHASE2 - the
    // transparency-equivalent of CollectRenderables() above. Always returns
    // an EMPTY vector today - there is no isTransparent/renderQueue concept
    // anywhere on MeshRenderer yet (see PHASE0_MASTER_STRATEGY.md's own Step
    // 2, point 6) - this exists purely as the real, structural drop-in point
    // a FUTURE transparency feature extends, mirroring CollectRenderables()'s
    // own exact shape so that future work is a pure additive change to
    // MeshRenderer + a real filter added HERE, never a new parallel
    // mechanism. `registry`'s parameter name is deliberately kept (even
    // though unused today) so the signature stays stable for that future
    // change. See src/Application/RenderPasses.cpp's AddRenderTransparentPass()
    // for this method's one production call site.
    static std::vector<DrawCommand> CollectTransparentRenderables(Registry& registry);

    // Pure camera-resolution step, the Camera equivalent of
    // CollectRenderables() above: finds the first entity (in
    // ComponentStorage<Camera> order) with Camera::active == true and
    // combines its Camera::ProjectionMatrix(aspectWidthOverHeight) with
    // Camera::ViewMatrix() of its Transform (an identity Transform - origin,
    // no rotation - if that entity happens not to have one) into a single
    // view-projection matrix. Returns Mat4::Identity() if the Registry has
    // no active Camera at all, which is exactly what preserves this
    // engine's original "vertices are already authored directly in clip
    // space, no camera involved" triangle-demo behavior for a scene that
    // hasn't added a Camera yet. Touches nothing but `registry` - no
    // Renderer, no live GPU resources - so this alone is Tier-1-testable
    // (see tests/Game/RenderSystemTests.cpp) exactly like
    // CollectRenderables() above.
    static Mat4 ResolveActiveCameraViewProjection(Registry& registry, float aspectWidthOverHeight);

    // Resolves the eye world-space position for the FIRST active ECS Camera
    // entity - the Vec3-only sibling of ResolveActiveCameraViewProjection()
    // above (same "first active Camera, in ComponentStorage<Camera> order"
    // resolution), falling back to Vec3::Zero() when the Registry has no
    // active Camera at all.
    static Vec3 ResolveActiveCameraWorldPosition(Registry& registry) noexcept;

    // Resolves each DrawCommand's handles against this RenderSystem's own
    // Mesh/Pipeline/MaterialTexture pools and submits it to `renderer` - the
    // one step that actually needs a live Renderer, called once per frame
    // PER VISIBLE render target from Game::Render() (a Game view and a
    // Scene view, each with their own RenderTexture/aspect ratio, both
    // showing the identical scene through whatever the active Camera
    // currently is - see Application::Run()). `aspectWidthOverHeight` is the
    // aspect ratio of whichever render target this call's draws will land
    // in - see ResolveActiveCameraViewProjection() above. A DrawCommand
    // whose mesh/pipeline handle no longer resolves (e.g. a future unloaded
    // mesh) is silently skipped rather than asserting - draws are
    // inherently best-effort against whatever is currently loaded. A
    // DrawCommand whose texture handle doesn't resolve (either
    // kInvalidTextureHandle - the normal untextured case - or a stale
    // handle) simply draws with no material texture bound (VK_NULL_HANDLE -
    // see Renderer::Submit()'s own `materialDescriptorSet` parameter).
    //
    // `capture` (optional, default nullptr - see IFrameDebuggerCaptureRecorder,
    // src/Core/FrameDebuggerCaptureRecorder.h, task_manager/frame-debugger-3,
    // PHASE1, upgraded to an abstract interface pointer by
    // editor-core-separation-2's own PHASE2) is only ever a real,
    // non-null, ARMED pointer for the Game-View-driving call site, and only
    // from PHASE3 onward - every existing call site (including this whole
    // campaign's own PHASE1) keeps compiling completely unchanged against
    // this new parameter's default. When armed, records real per-draw facts
    // (Pipeline debug name/MaterialTexture debug name/view-projection
    // matrix) for every resolved draw this call issues - see
    // IFrameDebuggerCaptureRecorder::RecordFrameDebuggerDraw().
    //
    // `maxDrawCount` (task_manager/frame-debugger-7 campaign, PHASE3,
    // PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.2) - optional, defaulted LAST parameter, purely additive (every
    // existing call site keeps compiling unchanged against its
    // std::nullopt default). When set, this call stops iterating once
    // `maxDrawCount` COMMANDS (i.e. DrawCommand entries yielded by
    // CollectRenderables(), the loop's own iteration count - NOT a count of
    // only the ones that successfully resolve a mesh+pipeline - see
    // Game::CountGameViewDrawCommandsThisFrame()'s own documented
    // assumption, Game.h) have been considered - `break`s out of the `for`
    // loop early, after the `maxDrawCount`-th iteration. Used exclusively
    // by AddFrameDebuggerReplayPasses() (src/Application/RenderPasses.cpp),
    // each of its N replay passes requesting a different cutoff (`i + 1`)
    // so pass `i` redraws exactly objects `[0..i]`.
    //
    // `batchedEntities` (GPU-Driven Frustum Culling + Indirect Draw
    // campaign, render-pass-5, PHASE5 - task_manager/render-pass-5/
    // PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md, Section
    // 3.3) - optional, defaulted, TRAILING (after `capture`/`maxDrawCount`)
    // parameter, empty by default - every existing call site keeps
    // compiling/behaving completely unmodified. A DrawCommand whose
    // `entity` is in this set is skipped for drawing (and, therefore, for
    // RecordFrameDebuggerDraws()'s own RecordDraw()/RecordEntityDraw() calls)
    // but still counts toward `consideredCount` for `maxDrawCount`
    // purposes, matching that parameter's own "iteration count, not
    // resolved-draw count" contract. Passed a real, non-empty value at
    // EXACTLY ONE production call site - the Game-View branch of the
    // "RenderOpaque" provider (Application.cpp) - never Scene View, never
    // AddFrameDebuggerReplayPasses()'s own per-object replay steps, never
    // AddPresentPass()'s direct-render-to-swapchain fallback (Locked
    // Design Decision 11, PHASE0_MASTER_STRATEGY.md).
    //
    // `sceneServicesSet` (Block 4, task_manager/better-render-pass-6,
    // PHASE6_DRAW_CALL_THREADING_SCENEQUERY_RENDERSYSTEM_GAME.md) - optional,
    // trailing, defaulted (VK_NULL_HANDLE) parameter, purely additive - every
    // existing call site keeps compiling/behaving completely unchanged.
    // Forwarded, unchanged, straight into the Mat4& overload below (which
    // owns the real per-command renderer.Submit()/CommandBuffer::Draw()
    // loop) - see that overload's own doc comment for the full contract.
    // `cmd` - see the Mat4& overload below's own doc comment; forwarded
    // straight through, unchanged.
    void Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight,
        IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt,
        const std::unordered_set<Entity>& batchedEntities = {}, VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE,
        rg::CommandBuffer* cmd = nullptr);

    // Explicit-view-projection overload of Draw() above, for a caller that
    // already has its own view-projection matrix to render with instead of
    // resolving one from the ECS Camera component - namely
    // ImGuiEditorLayer's Scene view, which renders through its own
    // independently-orbitable EditorCamera (see
    // src/Editor/EditorCamera.h) rather than whatever ECS entity has the
    // active Camera component (that's still what the float-aspect overload
    // above, used by the Game view, resolves via
    // ResolveActiveCameraViewProjection()). The float-aspect overload above
    // is implemented purely in terms of this one. `capture` - see the
    // float-aspect overload above's own comment; Scene View's own call site
    // (unaffected by this campaign - see PHASE0's Locked Design Decision
    // #7) stays at its default nullptr forever, since Scene View is out of
    // scope for the whole Frame Debugger feature. `maxDrawCount` - see the
    // actually owns the real loop/cutoff logic. `batchedEntities` - see the
    // float-aspect overload above's own comment; this overload owns the
    // real per-command skip check - EVERY call site to THIS overload
    // (Scene View, AddFrameDebuggerReplayPasses(), AddPresentPass()'s
    // fallback) keeps passing the default (empty) forever, per Locked
    // Design Decision 11 - only the float-aspect overload above's own
    // Game-View caller ever supplies a real, non-empty value, and it does
    // so by forwarding straight into this same overload.
    //
    // `pipelineOverrideSet` - optional, trailing, defaulted (std::nullopt)
    // parameter, purely additive. When set, resolves per entity via
    // ResolveOverridePipeline() (SceneQuery.h) against the entity's own
    // pipeline's VertexLayoutKind() - lets one call draw primitives,
    // untextured meshes, and textured meshes each through their own matching
    // override pipeline. An entity whose layout has no entry is skipped
    // entirely (warned once per distinct VertexLayout); a stale per-layout
    // handle is skipped too (warned once per distinct handle) - never a
    // crash, never a silent fall-back to the entity's own original pipeline.
    // `batchedEntities` composes with this exactly as it already composes
    // with everything else Draw() does - a batched-and-excluded entity is
    // skipped by its own existing check first.
    //
    // `sceneServicesSet` (Block 4, task_manager/better-render-pass-6,
    // PHASE6_DRAW_CALL_THREADING_SCENEQUERY_RENDERSYSTEM_GAME.md) - optional,
    // trailing, defaulted (VK_NULL_HANDLE) parameter, purely additive - every
    // existing call site (including the float-aspect overload above, which
    // forwards it straight through) keeps compiling/behaving completely
    // unchanged. This overload owns the real per-command loop, so it is the
    // ONE place that actually forwards this single, call-scoped value into
    // every renderer.Submit() call this method makes - never resolved
    // per-entity/per-MeshRenderer (see SceneQuery.h's SceneDrawRequest::
    // sceneServicesSet doc comment for the full "one value per view per
    // frame" contract).
    //
    // `cmd` (Frame Debugger Unity-Style Rehaul, per-draw-call granularity) -
    // optional, trailing, defaulted (nullptr) pointer to the render-graph
    // CommandBuffer already recording this pass. When non-null, every
    // resolved draw goes through cmd->Draw() (which also feeds
    // FrameDebuggerEventSink::NoteCommandResult()) instead of
    // renderer.Submit() directly - both paths issue the identical Vulkan
    // work, see CommandBuffer::Draw()'s own doc comment. nullptr (the
    // default) preserves the renderer.Submit() path unchanged for every
    // call site not yet migrated (Scene View, replay passes, DrawScene()).
    void Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection,
        IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt,
        const std::unordered_set<Entity>& batchedEntities = {},
        std::optional<PipelineOverrideSet> pipelineOverrideSet = std::nullopt,
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE, rg::CommandBuffer* cmd = nullptr);

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE4 (task_manager/render-pass-5/
    // PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md) - groups this
    // frame's DrawCommands by (MeshHandle, PipelineHandle)
    // (GroupDrawCommandsByMeshAndPipeline(), RenderBatching.h), resolves
    // each group's real Mesh&/Pipeline& via this RenderSystem's own pools
    // (exactly like Draw() above already does), applies
    // IsGpuDrivenEligible() (Locked Design Decision 7, PHASE0), and for
    // every eligible group repacks this frame's live Transform/Mesh-bounds
    // data into `cache`'s persistent per-batch GPU buffers
    // (GpuDrivenBatchCache::EnsureCapacity()/PackThisFrame()).
    //
    // `gpuSkinnedOutputBuffersThisFrame` - Locked Design Decision 7(d)'s
    // real cross-reference mechanism (confirmed via `ask_questions` during
    // this phase - see PHASE4_COMPLETION_REPORT.md): the exact
    // AnimationSystem::GpuSkinningDispatchRequest::outputBuffer VkBuffer
    // identity of every Mesh currently receiving a real GPU-skinning
    // dispatch THIS frame (see Game::CollectGpuSkinningDispatchRequests()) -
    // that request struct carries NO MeshHandle at all, so this set is built
    // by the CALLER (a Game/Application-level concern - RenderSystem itself
    // must never depend on AnimationSystem/Game, see AGENTS.md's Clean
    // Architecture rule) and compared HERE against each candidate group's
    // own resolved Mesh::VertexBuffer() (which returns the exact same
    // VkBuffer type/identity - a GPU-skinned model's Mesh IS built directly
    // from that same output buffer, see GpuSkinningRigCache.h). Left at its
    // default (empty) by every call site that never deals with GPU-skinned
    // models at all - always safe, since an empty set can never match any
    // real Mesh's VertexBuffer().
    //
    // Builds NO render-graph pass declarations and issues NO
    // dispatch/indirect draw of any kind (PHASE5's job). Does NOT modify
    // Draw()'s own existing per-entity loop behavior in any way - every
    // DrawCommand, including ones a caller later excludes via a future
    // batchedEntities-style parameter (PHASE5), is still drawn by Draw()
    // exactly as before until PHASE5 actually wires that exclusion in.
    std::vector<GpuDrivenBatchFrameEntry> CollectGpuDrivenBatches(Registry& registry, Renderer& renderer,
        GpuDrivenBatchCache& cache, const std::unordered_set<VkBuffer>& gpuSkinnedOutputBuffersThisFrame = {},
        std::size_t minInstancesForGpuDrivenBatch = kMinInstancesForGpuDrivenBatch);

private:
    ResourcePool<Mesh, MeshHandle> m_meshes;
    ResourcePool<Pipeline, PipelineHandle> m_pipelines;
    ResourcePool<MaterialTexture, TextureHandle> m_textures;

    // Draw()'s pipelineOverrideSet warn-once state. Single-threaded only -
    // Draw() has no concurrent call sites today. Never cleared: growth is
    // bounded by the number of distinct layouts/bad handles ever seen, not
    // by frame count.
    //
    // Warn once per distinct VertexLayout that has no override entry - same
    // per-key granularity as the stale-handle set below, not a single
    // fires-once-ever flag, so a missing PositionColor entry and a missing
    // PositionNormalUv entry are each reported.
    std::unordered_set<VertexLayout> m_warnedMissingOverrideLayouts;

    // Warn once per distinct stale per-layout handle.
    std::unordered_set<PipelineHandle> m_warnedStaleOverrideSetHandles;
};

} // namespace gte
