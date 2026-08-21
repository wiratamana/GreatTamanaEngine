#include "AssetInspectorData.h"

#include "ProjectPanelData.h" // PathToUtf8()

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace gte {

namespace {

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Converts a file's last-write-time to a human-readable LOCAL time string
// ("YYYY-MM-DD HH:MM:SS"). Returns an empty string if the conversion fails
// for any reason (never throws) - the caller treats that identically to
// "no last-write-time available".
std::string FormatLastWriteTime(std::filesystem::file_time_type fileTime)
{
    const std::chrono::system_clock::time_point systemTime = std::chrono::file_clock::to_sys(fileTime);
    const std::time_t asTimeT = std::chrono::system_clock::to_time_t(systemTime);

    std::tm localTm{};
    if (localtime_s(&localTm, &asTimeT) != 0) {
        return std::string();
    }

    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTm) == 0) {
        return std::string();
    }
    return std::string(buffer);
}

} // namespace

AssetMetadata BuildAssetMetadata(const std::filesystem::path& absolutePath)
{
    AssetMetadata metadata;
    metadata.name = PathToUtf8(absolutePath.filename());
    metadata.extension = ToLowerAscii(PathToUtf8(absolutePath.extension()));

    std::error_code existsEc;
    metadata.exists = std::filesystem::exists(absolutePath, existsEc) && !existsEc;
    if (!metadata.exists) {
        return metadata;
    }

    std::error_code dirEc;
    metadata.isDirectory = std::filesystem::is_directory(absolutePath, dirEc) && !dirEc;

    if (!metadata.isDirectory) {
        std::error_code sizeEc;
        const std::uintmax_t size = std::filesystem::file_size(absolutePath, sizeEc);
        metadata.sizeBytes = sizeEc ? 0 : size;
    }

    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(absolutePath, timeEc);
    if (!timeEc) {
        metadata.lastWriteTimeText = FormatLastWriteTime(writeTime);
        metadata.hasLastWriteTime = !metadata.lastWriteTimeText.empty();
    }

    return metadata;
}

bool IsSupportedImageExtension(const std::string& extensionLowercaseWithDot)
{
    // Matches stb_image's own documented supported formats (see
    // https://github.com/nothings/stb/blob/master/stb_image.h) - JPEG,
    // PNG, BMP, TGA, GIF, PSD, HDR, PIC, PNM (PPM/PGM). Deliberately
    // excludes formats stb_image only partially supports (e.g. progressive
    // JPEG variants) - AssetPreviewTexture's own decode attempt is still
    // the final word; this is only a cheap up-front filter.
    static constexpr std::array<const char*, 12> kSupported = {
        ".png",
        ".jpg",
        ".jpeg",
        ".bmp",
        ".tga",
        ".gif",
        ".psd",
        ".hdr",
        ".pic",
        ".pnm",
        ".ppm",
        ".pgm",
    };
    return std::any_of(
        kSupported.begin(), kSupported.end(), [&](const char* ext) { return extensionLowercaseWithDot == ext; });
}

} // namespace gte
