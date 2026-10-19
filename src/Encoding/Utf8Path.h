#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gte {

// UTF-8 <-> std::filesystem::path. Engine strings hold paths as UTF-8; on Windows a plain
// std::string -> path conversion would use the ANSI code page and break non-ASCII names.
inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

inline std::filesystem::path PathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace gte
