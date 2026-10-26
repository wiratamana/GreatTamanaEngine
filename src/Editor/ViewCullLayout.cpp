#include "ViewCullLayout.h"

#include <algorithm>

namespace gte {

std::optional<ViewCullBox> PlaceViewCullBox(Vec2 imageMin, Vec2 imageSize, float switcherBottom)
{
    const float width = std::min(kViewCullMaxWidth, imageSize.x - 2.0f * kViewCullMargin);
    if (!(width >= kViewCullMinWidth)) {
        return std::nullopt;
    }
    const float top = std::max(imageMin.y, switcherBottom) + kViewCullMargin;
    const float maxHeight = imageMin.y + imageSize.y - top - kViewCullMargin;
    if (!(maxHeight >= kViewCullMinHeight)) {
        return std::nullopt;
    }
    ViewCullBox box;
    box.min = Vec2(imageMin.x + imageSize.x - width - kViewCullMargin, top);
    box.width = width;
    box.maxHeight = maxHeight;
    return box;
}

} // namespace gte
