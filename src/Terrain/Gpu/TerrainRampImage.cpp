#include "TerrainRampImage.h"

#include "../TerrainMaterial.h"

#include <algorithm>
#include <cstring>

namespace gte::terrain::gpu {

RampUvMap MakeRampUvMap(int size, int border) noexcept
{
    const int inner = size - 2 * border;
    RampUvMap map;
    map.scale = static_cast<float>(inner - 1) / static_cast<float>(size);
    map.bias = (static_cast<float>(border) + 0.5f) / static_cast<float>(size);
    return map;
}

RampImage BuildPaddedRamp(const TerrainSettings& settings, int size, int border)
{
    size = std::clamp(size, 16, 1024);
    border = std::clamp(border, 1, size / 4);
    const int inner = size - 2 * border;

    const std::vector<std::uint8_t> core = BuildRampImage(settings, inner);
    RampImage image;
    image.size = size;
    image.border = border;
    image.uvMap = MakeRampUvMap(size, border);
    image.rgba.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);
    for (int y = 0; y < size; ++y) {
        const int sy = std::clamp(y - border, 0, inner - 1);
        for (int x = 0; x < size; ++x) {
            const int sx = std::clamp(x - border, 0, inner - 1);
            std::memcpy(&image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4u],
                &core[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(inner) + static_cast<std::size_t>(sx)) * 4u], 4u);
        }
    }
    return image;
}

} // namespace gte::terrain::gpu
