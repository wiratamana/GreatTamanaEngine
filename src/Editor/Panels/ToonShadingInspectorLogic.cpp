#include "ToonShadingInspectorLogic.h"

#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/ToonShading.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"

#include <cstddef>
#include <string_view>

namespace gte {

namespace {

// True when `root` or any descendant has a T. Stops at the first hit.
template <typename T>
bool HasComponentInside(Registry& registry, Entity root)
{
    if (registry.HasComponent<T>(root)) {
        return true;
    }
    ComponentStorage<T>& storage = registry.Storage<T>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        if (IsDescendantOf(registry, storage.EntityAt(i), root)) {
            return true;
        }
    }
    return false;
}

bool IsHierarchyRoot(const Registry& registry, Entity entity)
{
    const Transform* transform = registry.TryGetComponent<Transform>(entity);
    return transform == nullptr || !EffectiveParent(registry, *transform).IsValid();
}

} // namespace

ToonMarkVerdict EvaluateToonMark(Registry& registry, Entity entity)
{
    if (!registry.IsAlive(entity)) {
        return ToonMarkVerdict::DeadEntity;
    }
    if (registry.HasComponent<ToonShading>(entity)) {
        return ToonMarkVerdict::AlreadyMarked;
    }
    if (!HasComponentInside<MeshRenderer>(registry, entity)) {
        return ToonMarkVerdict::NoMeshInside;
    }
    if (!IsHierarchyRoot(registry, entity)) {
        return ToonMarkVerdict::HasParent;
    }
    if (HasComponentInside<ToonShading>(registry, entity)) {
        return ToonMarkVerdict::MarkedPartInside;
    }
    return ToonMarkVerdict::CanMark;
}

const char* ToonMarkHint(ToonMarkVerdict verdict) noexcept
{
    switch (verdict) {
    case ToonMarkVerdict::HasParent:
        return "Toon Shading is set on the model root. Select the root entity.";
    case ToonMarkVerdict::MarkedPartInside:
        return "A child part already has Toon Shading and would override this one. Remove it there first.";
    case ToonMarkVerdict::CanMark:
    case ToonMarkVerdict::AlreadyMarked:
    case ToonMarkVerdict::DeadEntity:
    case ToonMarkVerdict::NoMeshInside:
        return nullptr;
    }
    return nullptr;
}

bool EnableToonShading(Registry& registry, Entity entity)
{
    if (EvaluateToonMark(registry, entity) != ToonMarkVerdict::CanMark) {
        return false;
    }
    registry.AddComponent<ToonShading>(entity, ToonShading{ .enabled = true });
    return true;
}

bool RemoveToonShading(Registry& registry, Entity entity)
{
    return registry.IsAlive(entity) && registry.RemoveComponent<ToonShading>(entity);
}

bool ComputeToonGraphEnabled(std::span<const RenderFeatureDebugEntry> entries) noexcept
{
    constexpr std::string_view kBase = kToonCharacterFeatureName;
    constexpr std::string_view kSurfaceSuffix = ".Surface";
    for (const RenderFeatureDebugEntry& entry : entries) {
        const std::string_view name = entry.name;
        if (name.starts_with(kBase) && name.substr(kBase.size()) == kSurfaceSuffix) {
            return entry.enabled && (entry.enabledPrimary || entry.enabledSecondary);
        }
    }
    return false;
}

ToonFeedback EvaluateToonFeedback(const ToonFeatureReport& report, bool componentEnabled) noexcept
{
    using Level = ToonFeedbackLevel;
    if (!componentEnabled) {
        return { Level::Info, "Toon Shading is off for this entity. It keeps the normal look." };
    }
    if (!report.published) {
        return { Level::Warning, "The ToonCharacter feature is not loaded. No toon look." };
    }
    if (!report.graphEnabled) {
        return { Level::Warning, "The ToonCharacter.Surface pass is switched off in Render Features. No toon look." };
    }
    switch (report.status) {
    case ToonCharacterFrameStatus::Ok:
        return report.claimedEntityCount > 0
            ? ToonFeedback{ Level::Ok, "ToonCharacter is drawing parts in this scene." }
            : ToonFeedback{ Level::Warning, "The ToonCharacter pass drew no parts." };
    case ToonCharacterFrameStatus::FeatureDisabled:
        return { Level::Warning, "ToonCharacter is disabled in its own window (Enabled). No toon look." };
    case ToonCharacterFrameStatus::NoToonParts:
        return { Level::Warning, "No textured mesh part was found under a marked entity." };
    case ToonCharacterFrameStatus::PassDisabled:
        return { Level::Warning, "The ToonCharacter surface pass is switched off." };
    case ToonCharacterFrameStatus::DrawRefused:
        return { Level::Warning, "The ToonCharacter pass could not draw the parts. See the log." };
    case ToonCharacterFrameStatus::CaptureArmed:
        return { Level::Info, "A frame capture is armed. The normal look is drawn for now." };
    case ToonCharacterFrameStatus::NotRendered:
        return { Level::Info, "Not rendered yet. No view with the feature has been drawn." };
    }
    return { Level::Info, "Unknown state." };
}

} // namespace gte
