#pragma once

#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gte {

// A head of the Scene view gizmo. After a click on it, that head points at the viewer.
enum class SceneViewAxis : std::uint8_t { PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ };

inline constexpr std::size_t kSceneViewAxisCount = 6;

// EditorCamera yaw and pitch, in degrees, that look at the pivot from the side of a head.
struct SceneViewAngles {
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
};

SceneViewAngles SceneViewAnglesFor(SceneViewAxis axis) noexcept;

// Unit direction the camera looks along after a click on the head (the opposite of the head's axis).
Vec3 SceneViewLookDirection(SceneViewAxis axis) noexcept;

// "Right", "Left", "Top", "Bottom", "Back" or "Front".
const char* SceneViewAxisName(SceneViewAxis axis) noexcept;

// Accepts only the lowercase names "right", "left", "top", "bottom", "back", "front".
bool TryParseSceneViewAxis(std::string_view text, SceneViewAxis& out) noexcept;

// The axis a unit look direction follows within about 0.3 degrees, or nothing.
std::optional<SceneViewAxis> AlignedSceneViewAxis(const Vec3& lookDirection) noexcept;

// Text under the gizmo: "Persp", "Iso", "Top Iso", "Front Persp", ...
std::string SceneViewLabel(std::optional<SceneViewAxis> aligned, bool orthographic);

// One head on screen. Offset is in pixels from the gizmo center, +y down. Depth is -1 toward the viewer and +1 away.
struct SceneViewGizmoHead {
    SceneViewAxis axis = SceneViewAxis::PositiveX;
    Vec2 offset;
    float depth = 0.0f;
};

struct SceneViewGizmoLayout {
    std::array<SceneViewGizmoHead, kSceneViewAxisCount> heads; // Nearest to the viewer first.
};

// `right`, `up`, `forward` are the camera's unit axes in world space.
SceneViewGizmoLayout LayoutSceneViewGizmo(const Vec3& right, const Vec3& up, const Vec3& forward, float armLength) noexcept;

// The nearest head whose circle of `hitRadius` pixels holds the point (pixels from the gizmo center).
std::optional<SceneViewAxis> HitTestSceneViewGizmo(
    const SceneViewGizmoLayout& layout, Vec2 pointFromCenter, float hitRadius) noexcept;

} // namespace gte
