#pragma once

// Private helpers shared by the cluster viewer window sources. Not part of the editor API.

#include "ClusterViewerWindow.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace gte::viewer_detail {

inline constexpr const char* kLog = "ClusterViewer";
inline constexpr int kSizeStep = 32;
inline constexpr int kMaxImageSize = 8192;
inline constexpr int kPreviewWidth = 320;
inline constexpr int kDefaultWidth = 960;
inline constexpr int kDefaultHeight = 544;
inline constexpr float kSplitterThickness = 6.0f;
inline constexpr float kMinPaneWidth = 220.0f;
inline constexpr auto kStatInterval = std::chrono::milliseconds(500);

inline const ImVec4 kRed(0.95f, 0.4f, 0.4f, 1.0f);
inline const ImVec4 kOrange(0.95f, 0.7f, 0.3f, 1.0f);
inline const ImVec4 kGreen(0.4f, 0.9f, 0.4f, 1.0f);
inline const ImVec4 kMagenta(1.0f, 0.3f, 0.85f, 1.0f);
inline const ImVec4 kGrey(0.6f, 0.6f, 0.6f, 1.0f);
inline const ImVec4 kBlue(0.55f, 0.75f, 1.0f, 1.0f);

inline std::array<float, 16> ToArray(const Mat4& m) noexcept
{
    std::array<float, 16> a{};
    std::copy_n(m.Data(), 16, a.begin());
    return a;
}

// Rounds up to a multiple of kSizeStep so a window resize does not re-make the target every frame.
inline int Quantize(float value) noexcept
{
    const int steps = static_cast<int>(std::ceil(value / static_cast<float>(kSizeStep)));
    return std::clamp(steps * kSizeStep, kSizeStep, kMaxImageSize);
}

inline ImTextureID ToImTexture(UiTextureHandle handle) noexcept
{
    return static_cast<ImTextureID>(static_cast<std::uint64_t>(handle));
}

inline ClusterViewerResult MakeError(ClusterViewerError error, std::string message)
{
    ClusterViewerResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

inline ClusterViewerResult MakeOk(std::string message)
{
    ClusterViewerResult result;
    result.ok = true;
    result.message = std::move(message);
    return result;
}

inline ClusterViewSlot ToSlot(ClusterViewerImageTarget target) noexcept
{
    return target == ClusterViewerImageTarget::DebugPreview ? ClusterViewSlot::Preview : ClusterViewSlot::Main;
}

inline bool IsHardFailure(ClusterViewerBanner banner) noexcept
{
    return banner == ClusterViewerBanner::NoDeviceSupport || banner == ClusterViewerBanner::DecodeFailed
        || banner == ClusterViewerBanner::EmptyMesh || banner == ClusterViewerBanner::BadIndices;
}

} // namespace gte::viewer_detail
