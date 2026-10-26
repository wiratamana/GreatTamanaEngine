#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../Extent.h"
#include "../PixelFormat.h"

#include <memory>
#include <string>
#include <string_view>

namespace gte::fx {

namespace detail {
struct FrameContext;
} // namespace detail

// Cache name of a per-view resource: "<base>.<viewLabel>", or "<base>" when shared.
std::string PersistentName(std::string_view base, rg::ViewScope scope, std::string_view viewLabel);

// Same, with the view label the engine shows for the frame's view scope.
std::string PersistentName(std::string_view base, const detail::FrameContext& context);

// A color texture that outlives the frame, one per view, created and resized by the kit.
// The owner in the cache is the feature name; a resize applies one frame late.
class PersistentTarget {
public:
    explicit PersistentTarget(std::string_view baseName);
    ~PersistentTarget();

    PersistentTarget(const PersistentTarget&) = delete;
    PersistentTarget& operator=(const PersistentTarget&) = delete;
    PersistentTarget(PersistentTarget&&) = delete;
    PersistentTarget& operator=(PersistentTarget&&) = delete;

    // The texture for this frame's view, or an invalid Texture when the request is refused (logged once).
    Texture Acquire(Frame& frame, Extent2D extent, PixelFormat format);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
