// Registers the built-in components that scenes save and load.
// Called once, lazily, by ComponentTypeRegistry::Instance().

#include "ComponentTypeRegistry.h"
#include "ReflectFieldMacros.h"
#include "MathJsonAdapters.h"

#include "../Components/Camera.h"
#include "../Components/DirectionalLight.h"
#include "../Components/Name.h"
#include "../Components/PrimitiveSource.h"
#include "../Components/ToonShading.h"
#include "../Components/Transform.h"
#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h" // ToString(PrimitiveType)/TryParsePrimitiveTypeName()

namespace gte {

void RegisterBuiltinComponentReflections()
{
    // Hierarchy (parent, sibling order) is saved per entity record, not as a Transform field.
    RegisterComponentType<Transform>("Transform", {
        GTE_REFLECT_FIELD(Transform, position),
        GTE_REFLECT_FIELD(Transform, rotation),
        GTE_REFLECT_FIELD(Transform, scale),
    });

    RegisterComponentType<Name>("Name", {
        GTE_REFLECT_FIELD(Name, value),
    });

    RegisterComponentType<Camera>("Camera", {
        GTE_REFLECT_FIELD(Camera, fovYDegrees),
        GTE_REFLECT_FIELD(Camera, nearZ),
        GTE_REFLECT_FIELD(Camera, farZ),
        GTE_REFLECT_FIELD(Camera, active),
    });

    RegisterComponentType<DirectionalLight>("DirectionalLight", {
        GTE_REFLECT_FIELD(DirectionalLight, color),
        GTE_REFLECT_FIELD(DirectionalLight, illuminanceLux),
        GTE_REFLECT_FIELD(DirectionalLight, active),
    });

    RegisterComponentType<PrimitiveSource>("PrimitiveSource", {
        GTE_REFLECT_ENUM_FIELD(PrimitiveSource, type, ToString, TryParsePrimitiveTypeName),
    });

    RegisterComponentType<ToonShading>("ToonShading", {
        GTE_REFLECT_FIELD(ToonShading, enabled),
    });

    // Not registered on purpose: MeshAssetSource (saved as an asset guid), MeshRenderer (live GPU handles),
    // SkeletalAnimator, DynamicChainRig and ResolvedAnimationPose (runtime state, rebuilt on load).
}

} // namespace gte
