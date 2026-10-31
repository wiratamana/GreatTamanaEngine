#include "SceneViewAxes.h"

#include <algorithm>

namespace gte {

namespace {

// Two views count as aligned when the dot product of the look directions is above this (about 0.36 degrees).
constexpr float kAlignedCosine = 0.99998f;

struct AxisInfo {
    const char* name;
    const char* parseName;
    Vec3 lookDirection;
    SceneViewAngles angles;
};

// Indexed by SceneViewAxis. Look direction is the opposite of the head's axis.
const std::array<AxisInfo, kSceneViewAxisCount>& Axes()
{
    static const std::array<AxisInfo, kSceneViewAxisCount> axes = { {
        { "Right", "right", Vec3(-1.0f, 0.0f, 0.0f), { -90.0f, 0.0f } },
        { "Left", "left", Vec3(1.0f, 0.0f, 0.0f), { 90.0f, 0.0f } },
        { "Top", "top", Vec3(0.0f, -1.0f, 0.0f), { 0.0f, 90.0f } },
        { "Bottom", "bottom", Vec3(0.0f, 1.0f, 0.0f), { 0.0f, -90.0f } },
        { "Back", "back", Vec3(0.0f, 0.0f, -1.0f), { 180.0f, 0.0f } },
        { "Front", "front", Vec3(0.0f, 0.0f, 1.0f), { 0.0f, 0.0f } },
    } };
    return axes;
}

const AxisInfo& InfoOf(SceneViewAxis axis) noexcept
{
    return Axes()[static_cast<std::size_t>(axis)];
}

} // namespace

SceneViewAngles SceneViewAnglesFor(SceneViewAxis axis) noexcept { return InfoOf(axis).angles; }

Vec3 SceneViewLookDirection(SceneViewAxis axis) noexcept { return InfoOf(axis).lookDirection; }

const char* SceneViewAxisName(SceneViewAxis axis) noexcept { return InfoOf(axis).name; }

bool TryParseSceneViewAxis(std::string_view text, SceneViewAxis& out) noexcept
{
    for (std::size_t i = 0; i < kSceneViewAxisCount; ++i) {
        if (text == Axes()[i].parseName) {
            out = static_cast<SceneViewAxis>(i);
            return true;
        }
    }
    return false;
}

std::optional<SceneViewAxis> AlignedSceneViewAxis(const Vec3& lookDirection) noexcept
{
    for (std::size_t i = 0; i < kSceneViewAxisCount; ++i) {
        if (Dot(lookDirection, Axes()[i].lookDirection) > kAlignedCosine) {
            return static_cast<SceneViewAxis>(i);
        }
    }
    return std::nullopt;
}

std::string SceneViewLabel(std::optional<SceneViewAxis> aligned, bool orthographic)
{
    std::string label = aligned.has_value() ? std::string(SceneViewAxisName(*aligned)) + " " : std::string();
    label += orthographic ? "Iso" : "Persp";
    return label;
}

SceneViewGizmoLayout LayoutSceneViewGizmo(
    const Vec3& right, const Vec3& up, const Vec3& forward, float armLength) noexcept
{
    SceneViewGizmoLayout layout;
    for (std::size_t i = 0; i < kSceneViewAxisCount; ++i) {
        const Vec3 axisVector = Axes()[i].lookDirection * -1.0f;
        SceneViewGizmoHead& head = layout.heads[i];
        head.axis = static_cast<SceneViewAxis>(i);
        head.offset = Vec2(Dot(axisVector, right) * armLength, -Dot(axisVector, up) * armLength);
        head.depth = Dot(axisVector, forward);
    }
    std::stable_sort(layout.heads.begin(), layout.heads.end(),
        [](const SceneViewGizmoHead& a, const SceneViewGizmoHead& b) { return a.depth < b.depth; });
    return layout;
}

std::optional<SceneViewAxis> HitTestSceneViewGizmo(
    const SceneViewGizmoLayout& layout, Vec2 pointFromCenter, float hitRadius) noexcept
{
    for (const SceneViewGizmoHead& head : layout.heads) {
        if (Length(pointFromCenter - head.offset) <= hitRadius) {
            return head.axis;
        }
    }
    return std::nullopt;
}

} // namespace gte
