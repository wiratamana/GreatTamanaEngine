#pragma once

#include "../../ECS/Entity.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../../Features/ToonCharacter/ToonCharacterTypes.h"

#include <cstdint>
#include <span>

namespace gte {

class Registry;

// Mark rule: only a hierarchy root with mesh parts inside can be marked.
// The nearest ToonShading up the parent chain wins, so a child mark would override the root.
enum class ToonMarkVerdict : std::uint8_t {
    CanMark,
    AlreadyMarked,
    DeadEntity,
    NoMeshInside,    // Neither the entity nor a descendant has a MeshRenderer.
    HasParent,       // A child part: mark the root instead.
    MarkedPartInside // A descendant already has its own mark that would override this one.
};

ToonMarkVerdict EvaluateToonMark(Registry& registry, Entity entity);

// Short reason shown next to a refused mark; null when nothing should be shown.
const char* ToonMarkHint(ToonMarkVerdict verdict) noexcept;

// Adds an enabled ToonShading when the verdict is CanMark. False when refused.
bool EnableToonShading(Registry& registry, Entity entity);

// Removes the mark. False when the entity is dead or has none.
bool RemoveToonShading(Registry& registry, Entity entity);

// True when the ToonCharacter.Surface pass entry is on and runs in at least one view.
// No such entry means false.
bool ComputeToonGraphEnabled(std::span<const RenderFeatureDebugEntry> entries) noexcept;

// What the ToonCharacter window publishes each frame. Counts are scene totals, not per entity.
struct ToonFeatureReport {
    bool published = false;    // False when the ToonCharacter window does not exist.
    bool graphEnabled = false; // The feature is on in the render graph (Render Features panel).
    ToonCharacterFrameStatus status = ToonCharacterFrameStatus::NotRendered;
    std::uint32_t candidatePartCount = 0;
    std::uint32_t claimedEntityCount = 0;
};

enum class ToonFeedbackLevel : std::uint8_t { Ok, Info, Warning };

struct ToonFeedback {
    ToonFeedbackLevel level = ToonFeedbackLevel::Info;
    const char* text = ""; // Static string.
};

// Honest state of the toon look for a marked entity. Ok only when the surface pass really drew parts.
ToonFeedback EvaluateToonFeedback(const ToonFeatureReport& report, bool componentEnabled) noexcept;

} // namespace gte
