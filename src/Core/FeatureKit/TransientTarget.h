#pragma once

#include "Frame.h"
#include "PersistentTarget.h"
#include "Texture.h"
#include "../Extent.h"
#include "../PixelFormat.h"

#include <memory>
#include <string_view>

namespace gte::fx {

// A color texture for one frame, taken from the graph's pool; contents do not survive the frame.
// The pool recycles by exact size and format, so view-sized targets should prefer PersistentTarget.
class TransientTarget {
public:
    explicit TransientTarget(std::string_view baseName);
    ~TransientTarget();

    TransientTarget(const TransientTarget&) = delete;
    TransientTarget& operator=(const TransientTarget&) = delete;
    TransientTarget(TransientTarget&&) = delete;
    TransientTarget& operator=(TransientTarget&&) = delete;

    // The texture for this frame's view, kept so its writer is never culled; invalid without a render callback.
    Texture Acquire(Frame& frame, Extent2D extent, PixelFormat format, bool storage = true);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
