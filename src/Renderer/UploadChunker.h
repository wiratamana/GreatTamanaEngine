#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gte {

// A run of consecutive items that is uploaded with one submit.
struct UploadChunk {
    std::size_t first = 0;
    std::size_t count = 0;
    std::uint64_t bytes = 0;
};

// Counters of finished texture uploads (read for logs and tests).
struct GpuUploadStats {
    std::uint64_t submitCount = 0;
    std::uint64_t imageCount = 0;
    std::uint64_t stagingBytes = 0;
};

// Splits items, in order, into chunks of at most `budgetBytes` and `maxItems`. An item larger than
// the budget is a chunk of its own. Items of size 0 take no bytes. No item is dropped or reordered.
inline std::vector<UploadChunk> PlanUploadChunks(
    std::span<const std::uint64_t> itemBytes, std::uint64_t budgetBytes, std::size_t maxItems)
{
    std::vector<UploadChunk> chunks;
    const std::size_t itemLimit = maxItems == 0 ? 1 : maxItems;
    for (std::size_t i = 0; i < itemBytes.size(); ++i) {
        const std::uint64_t bytes = itemBytes[i];
        if (!chunks.empty()) {
            UploadChunk& open = chunks.back();
            const bool fits = open.count < itemLimit
                && (bytes == 0 || (bytes <= budgetBytes && open.bytes <= budgetBytes - bytes));
            if (fits) {
                ++open.count;
                open.bytes += bytes;
                continue;
            }
        }
        chunks.push_back({ i, 1, bytes });
    }
    return chunks;
}

} // namespace gte
