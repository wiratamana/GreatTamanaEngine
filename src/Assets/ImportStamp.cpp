#include "ImportStamp.h"

#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace gte {

namespace {

template <typename T>
bool ParseNumber(std::string_view text, T& out)
{
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

} // namespace

bool WriteImportStamp(const std::filesystem::path& stampPath, const ImportStamp& stamp)
{
    std::filesystem::path tempPath = stampPath;
    tempPath += ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << "importerVersion=" << stamp.importerVersion << '\n'
            << "sourceBytes=" << stamp.sourceBytes << '\n'
            << "materialCount=" << stamp.materialCount << '\n'
            << "textureCount=" << stamp.textureCount << '\n';
        out.flush();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(tempPath, ignored);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tempPath, stampPath, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

std::optional<ImportStamp> ReadImportStamp(const std::filesystem::path& stampPath)
{
    std::ifstream in(stampPath, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    ImportStamp stamp;
    bool hasVersion = false;
    bool hasBytes = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string_view key(line.data(), eq);
        const std::string_view value(line.data() + eq + 1, line.size() - eq - 1);
        bool ok = true;
        if (key == "importerVersion") {
            ok = ParseNumber(value, stamp.importerVersion);
            hasVersion = ok;
        } else if (key == "sourceBytes") {
            ok = ParseNumber(value, stamp.sourceBytes);
            hasBytes = ok;
        } else if (key == "materialCount") {
            ok = ParseNumber(value, stamp.materialCount);
        } else if (key == "textureCount") {
            ok = ParseNumber(value, stamp.textureCount);
        }
        if (!ok) {
            return std::nullopt;
        }
    }
    if (!hasVersion || !hasBytes) {
        return std::nullopt;
    }
    return stamp;
}

bool IsImportStampCurrent(const std::optional<ImportStamp>& stamp, int importerVersion, std::uint64_t sourceBytes)
{
    return stamp.has_value() && stamp->importerVersion == importerVersion && stamp->sourceBytes == sourceBytes;
}

bool IsPathStrictlyInsideDirectory(const std::filesystem::path& directory, const std::filesystem::path& candidate)
{
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(directory, ec);
    if (ec || root.empty()) {
        return false;
    }
    const std::filesystem::path path = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || path.empty()) {
        return false;
    }
    const std::filesystem::path relative = path.lexically_relative(root);
    if (relative.empty() || relative == ".") {
        return false;
    }
    return *relative.begin() != "..";
}

} // namespace gte
