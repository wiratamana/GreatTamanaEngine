#include "FlatListRangeSelection.h"

#include <algorithm>
#include <cstddef>

namespace gte {

std::vector<std::int32_t> BuildInclusiveIndexRange(std::int32_t anchorIndex, std::int32_t clickedIndex)
{
    if (anchorIndex < 0) {
        return { clickedIndex };
    }

    const std::int32_t lo = std::min(anchorIndex, clickedIndex);
    const std::int32_t hi = std::max(anchorIndex, clickedIndex);

    std::vector<std::int32_t> result;
    result.reserve(static_cast<std::size_t>(hi - lo + 1));
    for (std::int32_t i = lo; i <= hi; ++i) {
        result.push_back(i);
    }
    return result;
}

} // namespace gte
