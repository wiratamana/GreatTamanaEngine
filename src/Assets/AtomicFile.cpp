#include "AtomicFile.h"

#include "../Encoding/Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <exception>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace gte {

namespace {

struct WriterRegistry {
    std::mutex mutex;
    std::set<std::string> keys;
};

WriterRegistry& Registry()
{
    static WriterRegistry registry;
    return registry;
}

bool AllDigits(std::string_view text) noexcept
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Unique per process and per call: random nonce + counter, digits only.
std::string MakeTempName(const std::filesystem::path& target)
{
    static const std::uint64_t nonce = [] {
        std::random_device device;
        const std::uint64_t mixed = (static_cast<std::uint64_t>(device()) << 32) ^ device();
        return mixed % 1000000000000ull;
    }();
    static std::atomic<std::uint64_t> counter{ 0 };
    return PathToUtf8(target.filename()) + "." + std::to_string(nonce) + "."
        + std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
}

// Removes the temp file unless released after a successful rename.
class TempFileGuard {
public:
    explicit TempFileGuard(std::filesystem::path path) : m_path(std::move(path)) {}
    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;
    ~TempFileGuard()
    {
        if (m_armed) {
            std::error_code ignored;
            std::filesystem::remove(m_path, ignored);
        }
    }
    void Disarm() noexcept { m_armed = false; }

private:
    std::filesystem::path m_path;
    bool m_armed = true;
};

} // namespace

std::optional<FileIdentity> ReadFileIdentity(const std::filesystem::path& path) noexcept
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::nullopt;
    }
    FileIdentity identity;
    identity.size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::nullopt;
    }
    identity.lastWrite = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    return identity;
}

std::string WriterRegistryKey(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec || canonical.empty()) {
        canonical = path.lexically_normal();
    }
    std::wstring wide = canonical.wstring();
    std::transform(wide.begin(), wide.end(), wide.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return PathToUtf8(std::filesystem::path(wide));
}

std::optional<WriterClaim> WriterClaim::TryAcquire(const std::filesystem::path& target)
{
    std::string key = WriterRegistryKey(target);
    WriterRegistry& registry = Registry();
    std::lock_guard lock(registry.mutex);
    if (!registry.keys.insert(key).second) {
        return std::nullopt;
    }
    return WriterClaim(std::move(key));
}

bool WriterClaim::IsClaimed(const std::filesystem::path& target)
{
    const std::string key = WriterRegistryKey(target);
    WriterRegistry& registry = Registry();
    std::lock_guard lock(registry.mutex);
    return registry.keys.contains(key);
}

WriterClaim::WriterClaim(WriterClaim&& other) noexcept : m_key(std::move(other.m_key))
{
    other.m_key.clear();
}

WriterClaim& WriterClaim::operator=(WriterClaim&& other) noexcept
{
    if (this != &other) {
        Release();
        m_key = std::move(other.m_key);
        other.m_key.clear();
    }
    return *this;
}

WriterClaim::~WriterClaim()
{
    Release();
}

void WriterClaim::Release() noexcept
{
    if (m_key.empty()) {
        return;
    }
    WriterRegistry& registry = Registry();
    std::lock_guard lock(registry.mutex);
    registry.keys.erase(m_key);
    m_key.clear();
}

bool WriteFileAtomically(const std::filesystem::path& target, std::span<const ConstByteSpan> parts,
    const FileIdentity* expected, std::string* error, bool createOnly)
{
    const auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    try {
        const std::optional<WriterClaim> claim = WriterClaim::TryAcquire(target);
        if (!claim.has_value()) {
            return fail("busy");
        }

        std::uintmax_t total = 0;
        for (const ConstByteSpan part : parts) {
            total += part.size();
        }

        std::filesystem::path parent = target.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        std::error_code ec;
        const std::filesystem::space_info space = std::filesystem::space(parent, ec);
        if (!ec && !FitsInFreeSpace(total, space.available)) {
            return fail("not enough free disk space");
        }

        std::filesystem::path tempPath = target;
        tempPath.replace_filename(MakeTempName(target));
        TempFileGuard tempGuard(tempPath);
        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return fail("cannot create temp file");
            }
            for (const ConstByteSpan part : parts) {
                if (!part.empty()) {
                    out.write(reinterpret_cast<const char*>(part.data()), static_cast<std::streamsize>(part.size()));
                }
            }
            out.flush();
            out.close();
            if (out.fail()) {
                return fail("write failed");
            }
        }

        const std::optional<FileIdentity> written = ReadFileIdentity(tempPath);
        if (!written.has_value() || written->size != total) {
            return fail("temp file size mismatch");
        }

        if (createOnly) {
            std::error_code existsEc;
            if (std::filesystem::exists(target, existsEc) || existsEc) {
                return fail("destination exists");
            }
        } else if (expected != nullptr) {
            const std::optional<FileIdentity> current = ReadFileIdentity(target);
            if (!current.has_value() || !(*current == *expected)) {
                return fail("changed on disk");
            }
        }

        std::filesystem::rename(tempPath, target, ec);
        if (ec) {
            return fail("cannot replace target (in use?): " + ec.message());
        }
        tempGuard.Disarm();
        return true;
    } catch (const std::exception& e) {
        return fail(std::string("write failed: ") + e.what());
    } catch (...) {
        return fail("write failed");
    }
}

bool IsOwnTempFileName(const std::string& fileName, std::string* targetName)
{
    constexpr std::string_view kTmp = ".tmp";
    std::string_view rest = fileName;
    if (rest.size() <= kTmp.size() || !rest.ends_with(kTmp)) {
        return false;
    }
    rest.remove_suffix(kTmp.size());
    for (int i = 0; i < 2; ++i) {
        const std::size_t dot = rest.rfind('.');
        if (dot == std::string_view::npos || !AllDigits(rest.substr(dot + 1))) {
            return false;
        }
        rest = rest.substr(0, dot);
    }
    for (const std::string_view suffix : { std::string_view(".gta"), std::string_view(".import.stamp") }) {
        if (rest.size() > suffix.size() && rest.ends_with(suffix)) {
            if (targetName != nullptr) {
                *targetName = std::string(rest);
            }
            return true;
        }
    }
    return false;
}

std::size_t SweepStaleTempFiles(const std::filesystem::path& root, std::chrono::seconds minAge)
{
    std::size_t removed = 0;
    try {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        const auto now = std::filesystem::file_time_type::clock::now();
        while (!ec && it != end) {
            const std::filesystem::directory_entry& entry = *it;
            std::error_code entryEc;
            std::string targetName;
            if (!entry.is_symlink(entryEc) && entry.is_regular_file(entryEc) && !entryEc
                && IsOwnTempFileName(PathToUtf8(entry.path().filename()), &targetName)) {
                const auto lastWrite = entry.last_write_time(entryEc);
                const bool oldEnough = !entryEc && now - lastWrite >= minAge;
                if (oldEnough && !WriterClaim::IsClaimed(entry.path().parent_path() / PathFromUtf8(targetName))) {
                    if (std::filesystem::remove(entry.path(), entryEc) && !entryEc) {
                        ++removed;
                    }
                }
            }
            it.increment(ec);
        }
    } catch (...) {
        // A failed sweep is harmless: the files stay for the next run.
    }
    return removed;
}

} // namespace gte
