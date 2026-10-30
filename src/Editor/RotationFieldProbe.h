#pragma once

#include "../ECS/Entity.h"
#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gte {

// Rotation field of the Inspector as reported to tools (GET /inspector_state).
struct RotationFieldReport {
    bool drawn = false; // True when the field was drawn last frame.
    Entity entity = kInvalidEntity;
    Vec3 shown{};       // Euler degrees on screen.
    Vec2 axisCenter[3]; // Screen centers of the X, Y and Z drag fields.
    std::uint32_t rederiveCount = 0; // Times the Euler was rebuilt from the quaternion.
};

// What the Inspector's Rotation field showed on its last draw. Debug data only, no ImGui dependency.
struct RotationFieldProbe {
    int frame = -1; // ImGui frame count of the draw; -1 = never drawn.
    Entity entity = kInvalidEntity;
    Vec3 shown{};
    Vec2 axisCenter[3];

    // Splits a 3-component drag widget starting at `left` into its field centers (same split as ImGui).
    void Record(int frameCount, Entity e, const Vec3& euler, float left, float centerY, float totalWidth,
        float innerSpacing) noexcept
    {
        frame = frameCount;
        entity = e;
        shown = euler;
        const float one = std::max(1.0f, std::floor((totalWidth - innerSpacing * 2.0f) / 3.0f));
        const float last = std::max(1.0f, std::floor(totalWidth - (one + innerSpacing) * 2.0f));
        for (int i = 0; i < 3; ++i) {
            axisCenter[i] = Vec2(left + static_cast<float>(i) * (one + innerSpacing) + (i < 2 ? one : last) * 0.5f, centerY);
        }
    }

    // Snapshot for tools. `currentFrame` is the frame count now; a draw one frame back still counts as last frame.
    RotationFieldReport Report(int currentFrame, std::uint32_t rederiveCount) const noexcept
    {
        RotationFieldReport report;
        report.drawn = frame >= 0 && currentFrame - frame <= 1;
        report.entity = entity;
        report.shown = shown;
        std::copy(std::begin(axisCenter), std::end(axisCenter), std::begin(report.axisCenter));
        report.rederiveCount = rederiveCount;
        return report;
    }
};

} // namespace gte
