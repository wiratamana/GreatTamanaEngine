#pragma once

// ============================================================================
// GPU-Driven Frustum Culling + Indirect Draw (render-pass-5) - PHASE6:
// Editor Tooling + Live Validation
// ============================================================================
// See task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md
// for the full design reasoning.
//
// Confirmed via ask_questions before writing this file (Step 2's own
// documented checkpoint): Game.cpp's real demo scene spawns ZERO mesh
// entities by default (only a Camera - see Game::EnsureDefaultCameraExists()'s
// own doc comment), this project's only real imported asset
// (build/Project/terrain.gta) is fully TEXTURED (batching-ineligible per
// PHASE0's Locked Design Decision 8), and every PrimitiveMeshGenerator shape
// is non-indexed (also ineligible - Locked Design Decision 6). No existing
// content already produces a qualifying GPU-driven batch
// (>= kMinInstancesForGpuDrivenBatch instances sharing one untextured,
// indexed, VertexLayout::PositionNormal Mesh+Pipeline) - this file is the
// small, explicit, clearly-labeled validation spawn helper the project owner
// confirmed should exist for this purpose.
//
// Editor-only (GTE_ENABLE_EDITOR) by explicit project-owner decision - this
// is debug/validation tooling, not a production gameplay API (unlike
// Game::InstantiatePrimitive(), which a real game could call too). Reachable
// two ways, both funneling into this ONE Spawn() function so they can never
// drift apart:
//   1. The Editor's "Hierarchy" panel, right-click "Create GPU-Driven Test
//      Batch" context-menu entry (Panels/HierarchyPanel.cpp) - calls this
//      directly (that file is itself Editor-only, so no IEditorLayer
//      indirection is needed there).
//   2. POST /spawn_gpu_driven_test_batch (see AGENTS.md, "Networking") -
//      routed through IEditorLayer::SpawnGpuDrivenTestBatch()
//      (EditorLayer.h)/EditorUiCommandBridge, since a Network route handler
//      (background thread) never touches Game/Renderer directly - the real
//      ImGuiEditorLayer implementation calls this same Spawn() function;
//      NullEditorLayer's own no-op stub means this whole feature is
//      structurally unavailable (a clean 503, mirroring /save_scene's own
//      "editorAvailable" convention) whenever GTE_ENABLE_EDITOR is OFF.
//
// Builds ONE shared, hand-authored, untextured, indexed, unit quad Mesh
// (VertexLayout::PositionNormal - the ONE real content shape PHASE0's Locked
// Design Decision 7(c) allows) + a matching Pipeline, lazily, once per
// process (cached in this file's own .cpp as function-local statics - safe
// since exactly one Game/RenderSystem instance ever exists per process
// lifetime), reusing the EXACT SAME "shaders/Mesh.vert.spv"/
// "shaders/Mesh.frag.spv" pair MeshAssetGpuCatalog::EnsureMeshPipeline()
// already uses for real untextured imported-mesh content - this is what
// lets GpuDrivenBatchCache::ResolveInstancedPipeline()'s own hardcoded
// shader-pair lookup (PHASE5) resolve a real
// VertexLayout::PositionNormalInstanced sibling for it with zero further
// wiring.
#include <volk.h>

#include <cstdint>
#include <string>

namespace gte {

class Game;
class Renderer;

// Result of one Spawn() call - deliberately a small, dependency-free struct
// (no Entity/Registry type leaked here), mirroring every other
// Outcome-style struct this engine already returns from a spawn-style API
// (see e.g. InstantiatePrimitiveOutcome, EngineCommandResults.h).
struct GpuDrivenBatchTestSpawnResult {
    bool success = false;
    std::string errorMessage;
    // Only meaningful when success == true - the number of new entities
    // actually created (always == the requested instanceCount on success;
    // this function never partially spawns a batch).
    std::uint32_t instanceCount = 0;
};

class GpuDrivenBatchTestSpawner {
public:
    // Spawns `instanceCount` new entities (each: a Transform placed in a
    // simple left-to-right row so the whole batch sits comfortably in front
    // of the engine's own default Camera - Vec3{0,0,-5}, identity rotation,
    // looking down +Z - plus a MeshRenderer referencing the one shared
    // Mesh/Pipeline pair this class lazily builds), each with a unique,
    // auto-de-duplicated Name (MakeUniqueEntityName(), "GpuDrivenTestBatch" -
    // mirrors Game::InstantiatePrimitive()'s own naming convention).
    //
    // Returns success == false (creates NO entities at all) only for
    // instanceCount == 0 - never throws otherwise. A real GPU/Vulkan failure
    // building the shared Mesh/Pipeline the very first time this is called
    // (e.g. a missing .spv) propagates as a genuine exception, exactly like
    // every other Renderer::CreateMesh()/CreatePipeline() call site in this
    // engine - this function does not add its own defensive catch, matching
    // Game::CreatePrimitiveEntity()'s own identical "let a real GPU failure
    // surface" precedent.
    // `sceneServicesSetLayout` must be Core's real SceneServicesDescriptorSet::
    // Layout() - forwarded into the shared Mesh/Pipeline this class lazily builds.
    static GpuDrivenBatchTestSpawnResult Spawn(
        Game& game, Renderer& renderer, std::uint32_t instanceCount, VkDescriptorSetLayout sceneServicesSetLayout);
};

} // namespace gte
