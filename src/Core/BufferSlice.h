#pragma once

#include <cstddef>

namespace gte {

class Buffer;

// A byte range inside an existing Buffer - engine-wide, zero Vulkan
// dependency. Never calls Buffer::Native(); that accessor stays a
// Renderer-internal detail.
struct BufferSlice {
    const Buffer* buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0; // 0 means "rest of buffer".
};

} // namespace gte
