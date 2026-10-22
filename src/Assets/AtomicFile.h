#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace gte {

using ConstByteSpan = std::span<const std::byte>;

// Size and last write time; read twice to detect a change in between.
struct FileIdentity {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type lastWrite{};

    bool operator==(const FileIdentity&) const = default;
};

// Nullopt when the path is missing or not a regular file. Never throws.
std::optional<FileIdentity> ReadFileIdentity(const std::filesystem::path& path) noexcept;

// Case-insensitive, weakly canonical key of a path (one file = one key on Windows).
std::string WriterRegistryKey(const std::filesystem::path& path);

// Extra free space required on top of the payload before a write starts.
inline constexpr std::uintmax_t kAtomicWriteSpaceMargin = 1024u * 1024u;

// True when `required` bytes plus the margin fit in `available`.
constexpr bool FitsInFreeSpace(std::uintmax_t required, std::uintmax_t available) noexcept
{
    return required <= available && available - required >= kAtomicWriteSpaceMargin;
}

// Process-wide claim on one write target. Movable, released on destruction.
class WriterClaim {
public:
    // Nullopt when another claim on the same file exists.
    static std::optional<WriterClaim> TryAcquire(const std::filesystem::path& target);

    WriterClaim(WriterClaim&& other) noexcept;
    WriterClaim& operator=(WriterClaim&& other) noexcept;
    WriterClaim(const WriterClaim&) = delete;
    WriterClaim& operator=(const WriterClaim&) = delete;
    ~WriterClaim();

    // True while some claim on the file exists in this process.
    static bool IsClaimed(const std::filesystem::path& target);

private:
    explicit WriterClaim(std::string key) noexcept : m_key(std::move(key)) {}
    void Release() noexcept;

    std::string m_key;
};

// Writes `parts` back to back into "<target>.<id>.<n>.tmp", then renames it over `target`.
// The old file stays byte-identical on failure. With `expected`, a target changed since that
// identity was read is not replaced. Atomic against a crash, not forced to disk. Never throws.
bool WriteFileAtomically(const std::filesystem::path& target, std::span<const ConstByteSpan> parts,
    const FileIdentity* expected, std::string* error);

// Removes our own orphaned temp files under `root` that are older than `minAge` and whose
// target is not being written. Returns the number removed. Call at startup, never in a scan.
std::size_t SweepStaleTempFiles(
    const std::filesystem::path& root, std::chrono::seconds minAge = std::chrono::hours(1));

// True when `fileName` is "<name>.gta.<digits>.<digits>.tmp" or
// "<name>.import.stamp.<digits>.<digits>.tmp"; `targetName` gets the part before the counters.
bool IsOwnTempFileName(const std::string& fileName, std::string* targetName = nullptr);

} // namespace gte
