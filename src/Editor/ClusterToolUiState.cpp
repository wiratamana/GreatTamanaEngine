#include "ClusterToolUiState.h"

#include "../Assets/GtaFile.h"
#include "../Assets/ImportStamp.h"
#include "../Assets/MeshClusterPersistence.h"
#include "../Encoding/Utf8Path.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <system_error>

namespace gte {

int ClampClusterMaxVertices(int value) noexcept
{
    return std::clamp(value, 3, 255);
}

int ClampClusterMaxTriangles(int value) noexcept
{
    const int clamped = std::clamp(value, 4, 512);
    return clamped - clamped % 4;
}

float ClampClusterConeWeight(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.5f;
}

std::string FormatClusterStatusLine(std::size_t clusterCount, float avgTrianglesPerCluster, std::size_t verticesBefore,
    std::size_t verticesAfter, double milliseconds)
{
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%zu clusters, avg %.1f tris, vertices %zu -> %zu, %.0f ms", clusterCount,
        static_cast<double>(avgTrianglesPerCluster), verticesBefore, verticesAfter, milliseconds);
    return buffer;
}

const ClusterEligibilityCache& RefreshClusterEligibility(
    ClusterToolUiState& state, const std::string& utf8Path, std::chrono::steady_clock::time_point now)
{
    ClusterEligibilityCache& cache = state.eligibility;
    const bool samePath = cache.valid && cache.path == utf8Path;
    if (samePath && now - cache.lastCheck < kClusterEligibilityRecheckInterval) {
        return cache;
    }

    const std::filesystem::path path = PathFromUtf8(utf8Path);
    std::error_code sizeEc;
    std::error_code timeEc;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeEc);
    const std::filesystem::file_time_type lastWrite = std::filesystem::last_write_time(path, timeEc);
    if (samePath && !sizeEc && !timeEc && size == cache.size && lastWrite == cache.lastWrite) {
        cache.lastCheck = now;
        return cache;
    }

    cache.path = utf8Path;
    cache.size = sizeEc ? 0 : size;
    cache.lastWrite = timeEc ? std::filesystem::file_time_type{} : lastWrite;
    cache.lastCheck = now;
    cache.valid = true;
    if (const std::optional<GtaMetadataData> metadata = ReadGtaMetadataOnly(path); metadata.has_value()) {
        const std::optional<std::string> reason = ClusterBuildRefusalReason(*metadata);
        cache.refusalReason = reason.has_value() ? *reason : std::string();
    } else {
        cache.refusalReason = "The asset metadata cannot be read.";
    }
    return cache;
}

std::optional<std::string> ValidateMeshAssetPath(
    const std::string& utf8Path, const std::filesystem::path& projectRoot, std::filesystem::path& outPath)
{
    outPath = PathFromUtf8(utf8Path);
    std::string extension = PathToUtf8(outPath.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".gta") {
        return "the path must end in .gta";
    }
    if (!outPath.is_absolute() || !IsPathStrictlyInsideDirectory(projectRoot, outPath)) {
        return "the path must be absolute and inside the project root";
    }
    const std::optional<GtaHeader> header = ReadGtaHeader(outPath);
    if (!header.has_value() || header->Type() != AssetType::Mesh) {
        return "the path is not a readable mesh asset";
    }
    return std::nullopt;
}

std::string ProjectRelativeSlashPath(const std::filesystem::path& absolute, const std::filesystem::path& projectRoot)
{
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(absolute, projectRoot, ec);
    if (ec || relative.empty() || *relative.begin() == "..") {
        return {};
    }
    std::string text = PathToUtf8(relative);
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

} // namespace gte
