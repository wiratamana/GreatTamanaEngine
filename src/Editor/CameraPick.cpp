#include "CameraPick.h"

#include "ViewportProjection.h"

#include <cmath>
#include <limits>

namespace gte {

namespace {

constexpr float kTieDistance = 1.0e-3f;

struct Scored {
    CameraPickHit hit;
    float depth = 0.0f; // clip w of the apex
};

// Ranks apex hits first, then by distance, then by nearer apex depth.
bool IsBetter(const Scored& candidate, const Scored& best) noexcept
{
    if (candidate.hit.onApex != best.hit.onApex) {
        return candidate.hit.onApex;
    }
    if (std::fabs(candidate.hit.distancePixels - best.hit.distancePixels) > kTieDistance) {
        return candidate.hit.distancePixels < best.hit.distancePixels;
    }
    return candidate.depth < best.depth;
}

} // namespace

std::optional<CameraPickHit> PickCamera(std::span<const CameraPickCandidate> candidates, const Mat4& viewProjection,
    Vec2 rectMin, Vec2 rectMax, Vec2 cursor)
{
    if (!(rectMax.x > rectMin.x) || !(rectMax.y > rectMin.y) || !std::isfinite(cursor.x)
        || !std::isfinite(cursor.y)) {
        return std::nullopt;
    }

    std::optional<Scored> best;
    for (const CameraPickCandidate& candidate : candidates) {
        if (!FrustumInputIsValid(candidate.frustum)) {
            continue;
        }
        const FrustumCorners corners = ComputeFrustumCorners(candidate.frustum);

        Scored scored;
        scored.hit.id = candidate.id;
        scored.depth = (viewProjection * Vec4(corners.apex, 1.0f)).w;

        float apexDistance = std::numeric_limits<float>::max();
        Vec2 apexScreen;
        if (ProjectToScreen(corners.apex, viewProjection, rectMin, rectMax, apexScreen)) {
            apexDistance = Length(cursor - apexScreen);
        }

        if (apexDistance < kCameraPickApexPixels) {
            scored.hit.onApex = true;
            scored.hit.distancePixels = apexDistance;
        } else {
            float edgeDistance = std::numeric_limits<float>::max();
            for (const FrustumSegment& segment : BuildFrustumSegments(corners)) {
                Vec4 ca;
                Vec4 cb;
                if (!ClipSegmentToView(viewProjection, segment.a, segment.b, ca, cb)) {
                    continue;
                }
                const float d = PointToSegmentDistance(cursor, ClipToScreen(ca, rectMin, rectMax),
                    ClipToScreen(cb, rectMin, rectMax));
                if (d < edgeDistance) {
                    edgeDistance = d;
                }
            }
            if (!(edgeDistance < kCameraPickEdgePixels)) {
                continue;
            }
            scored.hit.onApex = false;
            scored.hit.distancePixels = edgeDistance;
        }

        if (!best || IsBetter(scored, *best)) {
            best = scored;
        }
    }

    if (!best) {
        return std::nullopt;
    }
    return best->hit;
}

} // namespace gte
