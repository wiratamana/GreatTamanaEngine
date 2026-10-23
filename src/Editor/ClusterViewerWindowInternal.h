#pragma once

// Private helpers shared by the cluster viewer window sources. Not part of the editor API.

#include "ClusterViewerWindow.h"

#include <string>
#include <utility>

namespace gte::viewer_detail {

inline constexpr int kMaxImageSize = 8192;

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

} // namespace gte::viewer_detail
