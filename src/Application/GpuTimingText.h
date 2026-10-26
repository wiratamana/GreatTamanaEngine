#pragma once

// GPU timing as text for panels and HTTP: a measured value or the status word, never a made-up number.

#include <cstdio>
#include <string>
#include <string_view>

namespace gte {

// "Present" -> "%.2f ms", "Partial" -> "~%.2f ms (partial)", any other status -> the status word.
inline std::string FormatGpuTimingText(std::string_view status, double milliseconds)
{
    char buffer[40];
    if (status == "Present") {
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", milliseconds);
        return std::string(buffer);
    }
    if (status == "Partial") {
        std::snprintf(buffer, sizeof(buffer), "~%.2f ms (partial)", milliseconds);
        return std::string(buffer);
    }
    return std::string(status);
}

} // namespace gte
