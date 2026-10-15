#include "ShaderFile.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gte {

namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203u;

// Directory of the running executable (computed once, thread-safe).
const std::filesystem::path& ExecutableDir()
{
    static const std::filesystem::path dir = [] {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (len == 0) {
                return std::filesystem::path();
            }
            if (len < buffer.size()) {
                buffer.resize(len);
                return std::filesystem::path(buffer).parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }
    }();
    return dir;
}

[[noreturn]] void Fail(const std::string& path, const char* reason)
{
    throw std::runtime_error("shader file '" + path + "' " + reason + " - was it compiled?");
}

// Relative paths resolve against the executable directory only, never the working directory.
std::filesystem::path ResolvePath(const std::string& path)
{
    const std::filesystem::path requested(path);
    if (requested.is_absolute()) {
        return requested;
    }
    if (ExecutableDir().empty()) {
        Fail(path, "cannot be resolved: executable directory unknown");
    }
    return ExecutableDir() / requested;
}

} // namespace

std::vector<std::byte> ReadShaderFileBytes(const std::string& path)
{
    std::ifstream file(ResolvePath(path), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Fail(path, "could not be opened");
    }

    const std::streamoff size = file.tellg();
    if (size < 0) {
        Fail(path, "has an unreadable size");
    }
    if (size == 0) {
        Fail(path, "is empty");
    }
    if (size % 4 != 0) {
        Fail(path, "size is not a multiple of 4");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (file.gcount() != size) {
        Fail(path, "could not be read completely");
    }

    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    if (magic != kSpirvMagic) {
        Fail(path, "has no SPIR-V magic number");
    }
    return bytes;
}

} // namespace gte
