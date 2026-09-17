// The ONE place that bridges Phase 1's component-agnostic reflection core
// (ComponentTypeRegistry.h/.inl, ReflectFieldMacros.h, MathJsonAdapters.h)
// with this engine's real src/ECS/Components/*.h types - see
// task_manager/scene-serialization-2/PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md.
//
// RegisterBuiltinComponentReflections() is called EXACTLY ONCE, lazily, the
// first time anything ever calls ComponentTypeRegistry::Instance()
// (ComponentTypeRegistry.cpp's own self-bootstrapping Instance() - see that
// file), so there is no reliance on cross-translation-unit static
// initialization order and no external call site anywhere in Game/
// Application is ever required.
//
// A future component type is expected to add its OWN registration call
// here (or, if a future contributor prefers, in its own similarly-shaped
// .cpp file that this file's RegisterBuiltinComponentReflections() calls
// out to - either is fine; PHASE0's Locked Design Decision #3 only requires
// "one obvious place to add ~10 lines", not a specific file-per-component
// layout).

#include "ComponentTypeRegistry.h"
#include "ReflectFieldMacros.h"
#include "MathJsonAdapters.h"

#include "../Components/Camera.h"
#include "../Components/DirectionalLight.h"
#include "../Components/Name.h"
#include "../Components/PrimitiveSource.h"
#include "../Components/Transform.h"
#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h" // ToString(PrimitiveType)/TryParsePrimitiveTypeName()

namespace gte {

void RegisterBuiltinComponentReflections()
{
    // --- Transform (ECS/Components/Transform.h) ---
    // Deliberately reflects ONLY position/rotation/scale - NEVER `parent`
    // (an Entity handle, meaningless across a save/load round trip - see
    // PHASE3/PHASE4 for how hierarchy is instead captured at the
    // SceneDocument level via each entity's own array-index-based "parent"
    // field) and NEVER `siblingIndex` (also captured separately, at the
    // SceneDocument entity-record level - see PHASE0's Appendix A - so it
    // sits alongside "parent", not inside the generic "components" bag,
    // keeping ordering/hierarchy concerns visually distinct from plain
    // component data in the saved JSON).
    RegisterComponentType<Transform>("Transform", {
        GTE_REFLECT_FIELD(Transform, position),
        GTE_REFLECT_FIELD(Transform, rotation),
        GTE_REFLECT_FIELD(Transform, scale),
    });

    // --- Name (ECS/Components/Name.h) ---
    RegisterComponentType<Name>("Name", {
        GTE_REFLECT_FIELD(Name, value),
    });

    // --- Camera (ECS/Components/Camera.h) ---
    // THIS is what fixes the reported "camera near-far value" gap.
    RegisterComponentType<Camera>("Camera", {
        GTE_REFLECT_FIELD(Camera, fovYDegrees),
        GTE_REFLECT_FIELD(Camera, nearZ),
        GTE_REFLECT_FIELD(Camera, farZ),
        GTE_REFLECT_FIELD(Camera, active),
    });

    // --- DirectionalLight (ECS/Components/DirectionalLight.h) ---
    RegisterComponentType<DirectionalLight>("DirectionalLight", {
        GTE_REFLECT_FIELD(DirectionalLight, color), // Vec3 - needs MathJsonAdapters.h, already included above.
        GTE_REFLECT_FIELD(DirectionalLight, illuminanceLux),
        GTE_REFLECT_FIELD(DirectionalLight, active),
    });

    // --- PrimitiveSource (ECS/Components/PrimitiveSource.h) ---
    // Enum field - uses PrimitiveMeshGenerator.h's OWN, already-existing
    // ToString(PrimitiveType)/TryParsePrimitiveTypeName() - never a second,
    // parallel string mapping.
    RegisterComponentType<PrimitiveSource>("PrimitiveSource", {
        GTE_REFLECT_ENUM_FIELD(PrimitiveSource, type, ToString, TryParsePrimitiveTypeName),
    });

    // --- Deliberately NOT registered here - see this phase's own Step 2/3.3
    // for the full, per-type reasoning already written down so a future
    // contributor does not "helpfully" add one of these back in without
    // re-reading why it was excluded:
    //
    //   MeshAssetSource (gtaPath: std::string) - a raw filesystem path is
    //   not a stable enough reference across machines/after a project is
    //   moved - this is exactly why scene-serialization-1 already resolves
    //   it through AssetDatabase to a stable Guid instead (see
    //   Scene/SceneBuilder.h's own existing doc comment). This campaign
    //   PRESERVES that Guid-resolution behavior, just moved to the
    //   SceneDocument's own "asset_guid" entity-level field (PHASE0's
    //   Appendix A) rather than inside the generic "components" bag -
    //   Phase 3/4 implement the actual save/load logic for this.
    //
    //   MeshRenderer (mesh: MeshHandle, pipeline: PipelineHandle, texture:
    //   TextureHandle) - every field is a live, session-local, opaque
    //   GPU-resource-pool index (Renderer/ResourcePool.h) - serializing one
    //   and reading it back in a LATER session would silently reference
    //   whatever unrelated resource happens to occupy that pool slot THIS
    //   time, a serious, silent-corruption-class bug (see PHASE0's own
    //   Cross-Phase Invariant). MeshRenderer is always rebuilt fresh by
    //   re-running Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile()
    //   (Phase 4), never restored from saved data.
    //
    //   SkeletalAnimator (meshGtaPath, animationGtaPath: std::string; frame,
    //   speed: float; playing, loop: bool) - technically ALL plain data (no
    //   handles) and COULD be safely field-reflected in principle.
    //   Deliberately deferred to a FUTURE campaign anyway: correctly
    //   restoring it also needs re-running Game::PlayAnimationOnEntity()'s
    //   own cache-registration side effects (AnimationSystem::Play() -
    //   bone-name resolution against the model's skeleton), not just a
    //   field copy - the exact same "needs a spawn-time helper re-run, not a
    //   raw field copy" shape MeshAssetSource needs, just for a component
    //   this specific campaign's reported requirements (Transform + Camera
    //   near/far) never asked for. Noted in TODO.md (Phase 6) as a
    //   well-scoped future follow-up, not a silent gap.
    //
    //   DynamicChainRig (chainStates: std::vector<DynamicChainRuntimeState>,
    //   accumulatedSeconds: float, ...) - this is live physics SIMULATION
    //   state (particle positions mid-verlet-integration) - restoring stale
    //   simulation state from a save file would look like a physics glitch
    //   on the very next frame, not a feature. A freshly loaded scene should
    //   always start this component's physics from its own natural
    //   rest/bind-pose recomputation, exactly like a freshly SPAWNED (not
    //   loaded) model already does today.
    //
    //   ResolvedAnimationPose (pose: std::vector<BoneLocalOffset>) - purely
    //   DERIVED, per-frame-recomputed data (see its own doc comment:
    //   "Written EXCLUSIVELY by AnimationSystem::EvaluatePoses(), always
    //   OVERWRITING wholesale") - there is nothing here that is ever
    //   meaningful to persist; it is entirely recomputed from
    //   SkeletalAnimator/the model's own skeleton every single frame
    //   regardless.
}

} // namespace gte
