// Tier-1 tests for ReadShaderFileBytes (pure file I/O, no Vulkan).

#include "Renderer/ShaderFile.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gte {
namespace {

// Writes `bytes` to a unique temp file and removes it on destruction.
class TempFile {
public:
    explicit TempFile(const std::string& bytes)
    {
        static std::atomic<int> counter{ 0 };
        m_path = std::filesystem::temp_directory_path()
            / ("gte_shaderfile_test_" + std::to_string(counter.fetch_add(1)) + ".spv");
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string Path() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
};

std::string Words(std::initializer_list<std::uint32_t> words)
{
    std::string bytes(words.size() * sizeof(std::uint32_t), '\0');
    std::size_t offset = 0;
    for (const std::uint32_t word : words) {
        std::memcpy(bytes.data() + offset, &word, sizeof(word));
        offset += sizeof(word);
    }
    return bytes;
}

constexpr std::uint32_t kMagic = 0x07230203u;

TEST(ShaderFileTest, MissingFileThrowsAndNamesThePath)
{
    const std::string path = (std::filesystem::temp_directory_path() / "gte_shaderfile_missing.spv").string();
    try {
        ReadShaderFileBytes(path);
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(path), std::string::npos);
    }
}

TEST(ShaderFileTest, EmptyFileThrows)
{
    const TempFile file("");
    EXPECT_THROW(ReadShaderFileBytes(file.Path()), std::runtime_error);
}

TEST(ShaderFileTest, SizeNotMultipleOfFourThrows)
{
    std::string bytes = Words({ kMagic, 1u });
    bytes.push_back('\0');
    const TempFile file(bytes);
    EXPECT_THROW(ReadShaderFileBytes(file.Path()), std::runtime_error);
}

TEST(ShaderFileTest, BadMagicThrowsAndNamesThePath)
{
    const TempFile file(Words({ 0xDEADBEEFu, 1u, 2u }));
    try {
        ReadShaderFileBytes(file.Path());
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(file.Path()), std::string::npos);
    }
}

TEST(ShaderFileTest, GoodFileReturnsExactBytes)
{
    const std::string expected = Words({ kMagic, 0x00010600u, 0x12345678u });
    const TempFile file(expected);

    const std::vector<std::byte> bytes = ReadShaderFileBytes(file.Path());

    ASSERT_EQ(bytes.size(), expected.size());
    EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), expected.size()), 0);
}

// Directory of the test executable (the base ReadShaderFileBytes uses for relative paths).
std::filesystem::path TestExecutableDir()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size()) {
            buffer.resize(len);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

// Writes a file at `path` and removes it on destruction.
class ScopedFileAt {
public:
    ScopedFileAt(std::filesystem::path path, const std::string& bytes)
        : m_path(std::move(path))
    {
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    ~ScopedFileAt()
    {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }
    ScopedFileAt(const ScopedFileAt&) = delete;
    ScopedFileAt& operator=(const ScopedFileAt&) = delete;

private:
    std::filesystem::path m_path;
};

// Switches the working directory and restores it on destruction.
class ScopedWorkingDir {
public:
    explicit ScopedWorkingDir(const std::filesystem::path& dir)
        : m_previous(std::filesystem::current_path())
    {
        std::filesystem::current_path(dir);
    }
    ~ScopedWorkingDir()
    {
        std::error_code ec;
        std::filesystem::current_path(m_previous, ec);
    }
    ScopedWorkingDir(const ScopedWorkingDir&) = delete;
    ScopedWorkingDir& operator=(const ScopedWorkingDir&) = delete;

private:
    std::filesystem::path m_previous;
};

TEST(ShaderFileTest, RelativePathResolvesAgainstExecutableDirectory)
{
    const std::filesystem::path exeDir = TestExecutableDir();
    ASSERT_FALSE(exeDir.empty());

    const std::string expected = Words({ kMagic, 0x00010600u });
    const std::string name = "gte_shaderfile_exe_relative.spv";
    const ScopedFileAt file(exeDir / name, expected);

    const std::vector<std::byte> bytes = ReadShaderFileBytes(name);
    ASSERT_EQ(bytes.size(), expected.size());
    EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), expected.size()), 0);
}

TEST(ShaderFileTest, RelativePathPresentOnlyInWorkingDirectoryIsNotFound)
{
    const std::filesystem::path exeDir = TestExecutableDir();
    ASSERT_FALSE(exeDir.empty());

    const std::filesystem::path cwdDir = std::filesystem::temp_directory_path() / "gte_shaderfile_cwd_only";
    std::filesystem::create_directories(cwdDir);
    const std::string name = "gte_shaderfile_cwd_only.spv";
    ASSERT_FALSE(std::filesystem::exists(exeDir / name));

    const ScopedFileAt file(cwdDir / name, Words({ kMagic, 0x00010600u }));
    const ScopedWorkingDir cwd(cwdDir);

    EXPECT_THROW(ReadShaderFileBytes(name), std::runtime_error);
    EXPECT_NO_THROW(ReadShaderFileBytes((cwdDir / name).string()));
}

} // namespace
} // namespace gte
