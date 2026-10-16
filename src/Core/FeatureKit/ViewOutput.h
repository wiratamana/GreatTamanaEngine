#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../Extent.h"
#include "../PixelFormat.h"

#include <memory>
#include <string_view>

namespace gte::fx {

// One engine-owned, storage-capable texture per view scope ("GameView<suffix>" / "SceneView<suffix>"), shown by the
// editor panel and HTTP endpoints. Resized on demand after a GPU wait; the format is fixed by the first request.
class ViewOutput {
public:
    explicit ViewOutput(std::string_view nameSuffix);
    ~ViewOutput();

    ViewOutput(const ViewOutput&) = delete;
    ViewOutput& operator=(const ViewOutput&) = delete;

    // The view's texture, imported and kept alive; call only for an enabled pass. Invalid in a Shared callback,
    // for a zero size, or when the engine could not make or resize it (logged once, retried on a cooldown).
    Texture Acquire(Frame& frame, Extent2D extent, PixelFormat format);

private:
    friend class Frame;

    void Expose(Frame& frame, Texture produced);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
