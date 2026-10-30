#include "ScopeSource.h"

#include "FunctionName.h"
#include "NameTable.h"
#include "ProfilingBudget.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>

namespace gte::Profiling {
namespace {

// Packed site: bits 0-31 line, bits 32-61 basename id, bits 62-63 origin. 0 = not set.
constexpr std::uint64_t kLineMask = 0xFFFFFFFFull;
constexpr std::uint64_t kNameMask = 0x3FFFFFFFull;
constexpr unsigned kNameShift = 32;
constexpr unsigned kOriginShift = 62;
constexpr std::string_view kUnknownFunction = "<unknown function>";

std::array<std::atomic<std::uint64_t>, kMaxNames + 2>& SourceTable() noexcept
{
    static std::array<std::atomic<std::uint64_t>, kMaxNames + 2> table{};
    return table;
}

constexpr char Normalized(char c) noexcept
{
    return c == '\\' ? '/' : c;
}

// Compares with '\' and '/' treated as the same separator.
constexpr bool PathMatchesAt(std::string_view path, std::size_t at, std::string_view part) noexcept
{
    if (at + part.size() > path.size()) {
        return false;
    }
    for (std::size_t k = 0; k < part.size(); ++k) {
        if (Normalized(path[at + k]) != Normalized(part[k])) {
            return false;
        }
    }
    return true;
}

constexpr bool ContainsPath(std::string_view path, std::string_view needle) noexcept
{
    for (std::size_t i = 0; i + needle.size() <= path.size(); ++i) {
        if (PathMatchesAt(path, i, needle)) {
            return true;
        }
    }
    return false;
}

// Directory of this file's checkout, with a trailing separator. Empty when the path has an unexpected shape.
constexpr std::string_view kEngineSourceSuffix = "src/Profiling/ScopeSource.cpp";
constexpr std::string_view kEngineRoot = [] {
    constexpr std::string_view file = __FILE__;
    if (file.size() > kEngineSourceSuffix.size()
        && PathMatchesAt(file, file.size() - kEngineSourceSuffix.size(), kEngineSourceSuffix)) {
        return file.substr(0, file.size() - kEngineSourceSuffix.size());
    }
    return std::string_view();
}();

std::string_view Basename(std::string_view path) noexcept
{
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string_view::npos ? path : path.substr(cut + 1);
}

} // namespace

std::string_view EngineRoot() noexcept
{
    return kEngineRoot;
}

ScopeOrigin OriginOfPath(std::string_view path, std::string_view engineRoot) noexcept
{
    if (!engineRoot.empty() && PathMatchesAt(path, 0, engineRoot)) {
        if (PathMatchesAt(path, engineRoot.size(), "src/")) {
            return ScopeOrigin::Engine;
        }
        return PathMatchesAt(path, engineRoot.size(), "Projects/") ? ScopeOrigin::Project : ScopeOrigin::Unknown;
    }
    if (ContainsPath(path, "/Projects/")) {
        return ScopeOrigin::Project;
    }
    return ContainsPath(path, "/src/") ? ScopeOrigin::Engine : ScopeOrigin::Unknown;
}

NameId InternNameAt(const char* text, const char* file, std::uint32_t line) noexcept
{
    const NameId id = InternName(text);
    if (id <= kOverflowNameId) {
        return id;
    }

    std::atomic<std::uint64_t>& slot = SourceTable()[id];
    if (slot.load(std::memory_order_relaxed) != 0) {
        return id;
    }

    const std::string_view path = file ? std::string_view(file) : std::string_view();
    std::array<char, kMaxNameLength> baseName{};
    const std::string_view base = Basename(path);
    const std::size_t length = std::min(base.size(), baseName.size() - 1);
    std::copy_n(base.data(), length, baseName.data());

    const std::uint64_t fileId = InternName(length != 0 ? baseName.data() : nullptr) & kNameMask;
    const std::uint64_t packed = static_cast<std::uint64_t>(line)
        | (fileId << kNameShift)
        | (static_cast<std::uint64_t>(OriginOfPath(path, EngineRoot())) << kOriginShift);
    if (packed != 0) {
        std::uint64_t expected = 0;
        slot.compare_exchange_strong(expected, packed, std::memory_order_relaxed);
    }
    return id;
}

NameId InternFunctionName(const char* signature, const char* file, std::uint32_t line) noexcept
{
    std::array<char, kMaxNameLength> trimmed{};
    const std::size_t length = TrimFunctionName(signature ? std::string_view(signature) : std::string_view(), trimmed);
    return InternNameAt(length != 0 ? trimmed.data() : kUnknownFunction.data(), file, line);
}

ScopeSource SourceOf(NameId id) noexcept
{
    if (id >= kMaxNames + 2) {
        return {};
    }
    const std::uint64_t packed = SourceTable()[id].load(std::memory_order_relaxed);
    ScopeSource source;
    source.line = static_cast<std::uint32_t>(packed & kLineMask);
    source.fileName = static_cast<NameId>((packed >> kNameShift) & kNameMask);
    source.origin = static_cast<ScopeOrigin>(packed >> kOriginShift);
    return source;
}

} // namespace gte::Profiling
