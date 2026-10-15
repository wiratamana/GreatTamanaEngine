#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../PixelFormat.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gte::fx {

// Fixed size of a persistent volume in texels.
struct VolumeSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
};

// False for a zero axis; such a volume is never created.
constexpr bool IsValidVolumeSize(VolumeSize size) noexcept
{
    return size.width != 0 && size.height != 0 && size.depth != 0;
}

// A 3D texture (storage + sampled) owned by the feature, one per view scope, imported into the graph every frame.
// Its contents do not carry over between frames unless a pass rewrites them: it is imported with an undefined layout.
class PersistentVolume {
public:
    PersistentVolume(std::string_view baseName, VolumeSize size, PixelFormat format);
    ~PersistentVolume();

    PersistentVolume(const PersistentVolume&) = delete;
    PersistentVolume& operator=(const PersistentVolume&) = delete;

    // The volume for the frame's view; kept alive so the pass that writes it is never culled.
    // Invalid when the size is invalid or the engine could not make it (logged once, retried on a cooldown).
    Texture Acquire(Frame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
