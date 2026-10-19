// Tier-1 tests for the UTF-8 <-> std::filesystem::path helpers.

#include "Encoding/Utf8Path.h"

#include <string>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Builds a std::string from UTF-8 bytes without relying on the source file encoding.
std::string Bytes(const std::u8string& utf8)
{
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

TEST(Utf8PathTest, RoundTripsAsciiName)
{
    const std::filesystem::path path = PathFromUtf8("plain.txt");
    EXPECT_EQ(PathToUtf8(path), "plain.txt");
    EXPECT_EQ(PathFromUtf8(PathToUtf8(path)), path);
}

TEST(Utf8PathTest, RoundTripsJapaneseName)
{
    const std::string name = Bytes(u8"\u30c6\u30b9\u30c8_\u65e5\u672c\u8a9e.txt");
    const std::filesystem::path path = PathFromUtf8(name);
    EXPECT_EQ(PathToUtf8(path), name);
    EXPECT_EQ(path.u8string(), std::u8string(u8"\u30c6\u30b9\u30c8_\u65e5\u672c\u8a9e.txt"));
    EXPECT_EQ(PathFromUtf8(PathToUtf8(path)), path);
}

TEST(Utf8PathTest, RoundTripsEmojiName)
{
    const std::string name = Bytes(u8"model_\U0001F600.gta");
    const std::filesystem::path path = PathFromUtf8(name);
    EXPECT_EQ(PathToUtf8(path), name);
    EXPECT_EQ(PathFromUtf8(PathToUtf8(path)), path);
}

TEST(Utf8PathTest, EmptyStringGivesEmptyPath)
{
    EXPECT_TRUE(PathFromUtf8("").empty());
    EXPECT_TRUE(PathToUtf8(std::filesystem::path()).empty());
}

} // namespace
} // namespace gte
