#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gte::fx {

// Number of slots in the scene-services set.
inline constexpr std::uint32_t kSceneSlotCount = 8;

// Size limit of the scene uniform block in bytes.
inline constexpr std::size_t kMaxSceneUniformBytes = 128;

// What a slot holds; picks the resource type and, for Depth2D, the sampling layout.
enum class SceneSlotKind : std::uint8_t { Image2D, Depth2D, Image3D };

// A wrong depth flag can crash the GPU driver, so it is derived from the kind and never typed by hand.
constexpr bool IsDepthSlotKind(SceneSlotKind kind) noexcept
{
    return kind == SceneSlotKind::Depth2D;
}

// A slot in the scene-services set that material shaders sample. Invalid when declaring it failed.
struct SceneSlot {
    std::uint32_t index = kSceneSlotCount;
    SceneSlotKind kind = SceneSlotKind::Image2D;

    constexpr bool IsValid() const noexcept { return index < kSceneSlotCount; }
};

// Declares slots with the engine; implemented by the module adapter. Absent in unit tests.
class ISceneSlotRegistrar {
public:
    virtual ~ISceneSlotRegistrar() = default;

    // Invalid slot (and one log) on any refusal; never throws.
    virtual SceneSlot Declare(std::string_view name, SceneSlotKind kind, std::uint32_t preferredIndex) = 0;
};

} // namespace gte::fx
