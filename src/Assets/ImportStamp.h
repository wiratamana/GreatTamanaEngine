#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace gte {

// Marker written LAST after an import finished completely. Its presence and values tell
// whether the files next to it are a full result of the current importer.
struct ImportStamp {
    int importerVersion = 0;
    std::uint64_t sourceBytes = 0; // Size of the imported source file.
    std::uint32_t materialCount = 0;
    std::uint32_t textureCount = 0;
};

// Plain text, one "key=value" per line. The file is replaced atomically (temp file + rename).
bool WriteImportStamp(const std::filesystem::path& stampPath, const ImportStamp& stamp);

// Missing file, unreadable file or a bad value gives nullopt. Unknown keys are ignored.
std::optional<ImportStamp> ReadImportStamp(const std::filesystem::path& stampPath);

// True when the stamp exists and was made by this importer version from a source of the same size.
bool IsImportStampCurrent(const std::optional<ImportStamp>& stamp, int importerVersion, std::uint64_t sourceBytes);

// True when `candidate` lies strictly below `directory` (never equal to it, never outside it).
// Guards recursive removals.
bool IsPathStrictlyInsideDirectory(const std::filesystem::path& directory, const std::filesystem::path& candidate);

} // namespace gte
