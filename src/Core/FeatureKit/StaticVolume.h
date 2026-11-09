#pragma once

#include "Frame.h"
#include "PersistentVolume.h"
#include "Texture.h"
#include "../Extent.h"

#include <memory>
#include <string>
#include <string_view>

namespace gte::fx {

// A baked 3D texture loaded once from "assets/<assetFile>" (a GTV1 file with depth > 1).
// It is read-only after the load, so one copy serves every view and every frame in flight.
// A file with the tileable flag is sampled with REPEAT, any other with CLAMP.
class StaticVolume {
public:
    explicit StaticVolume(std::string_view assetFile);
    ~StaticVolume();

    StaticVolume(const StaticVolume&) = delete;
    StaticVolume& operator=(const StaticVolume&) = delete;

    // The volume for the frame's view, loaded on first use on the main thread.
    // Invalid while the file is missing or bad (reported once, retried on a cooldown).
    Texture Acquire(Frame& frame);

    // Zero until the file was loaded.
    VolumeSize Size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// The same for a baked 2D texture (a GTV1 file with depth == 1), bound as a sampler2D.
class StaticTexture {
public:
    explicit StaticTexture(std::string_view assetFile);
    ~StaticTexture();

    StaticTexture(const StaticTexture&) = delete;
    StaticTexture& operator=(const StaticTexture&) = delete;

    Texture Acquire(Frame& frame);

    // Zero until the file was loaded.
    Extent2D Size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
