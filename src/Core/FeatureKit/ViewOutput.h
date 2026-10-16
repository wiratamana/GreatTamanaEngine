#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../Extent.h"
#include "../PixelFormat.h"

#include <memory>
#include <string_view>

namespace gte::fx {

// A texture the editor panel and the HTTP endpoints show for the view (see Frame::ExposeAsViewOutput()).
// One engine-owned, storage-capable texture per view scope, named "GameView<suffix>" / "SceneView<suffix>".
// It is resized on demand (waiting for the GPU first); the format is fixed by the first request.
class ViewOutput {
public:
    explicit ViewOutput(std::string_view nameSuffix);
    ~ViewOutput();

    ViewOutput(const ViewOutput&) = delete;
    ViewOutput& operator=(const ViewOutput&) = delete;

    // The texture for the frame's view, imported into the graph and kept alive. Call it only for an enabled pass
    // (see Frame::PassEnabled()). Invalid in a Shared callback, for a zero size, or when the engine could not make
    // or resize it (logged once, retried on a cooldown).
    Texture Acquire(Frame& frame, Extent2D extent, PixelFormat format);

private:
    friend class Frame;

    void Expose(Frame& frame, Texture produced);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
