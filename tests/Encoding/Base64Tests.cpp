// Unit tests for Base64 (src/Encoding/Base64.h) - known-vector tests plus a
// hand-written reference decoder for a longer, non-ASCII-safe binary buffer.
// Pure logic, no GPU/SDL/ImGui/filesystem involved.

#include "Encoding/Base64.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gte::Encoding {
namespace {

// A small, hand-written reference base64 DECODER, used only by this test
// file to round-trip-verify EncodeBase64()'s output for a longer/binary
// buffer - deliberately independent of EncodeBase64()'s own implementation
// so a bug shared between "encode" and "a matching decode" couldn't hide
// from this test.
std::vector<std::uint8_t> ReferenceDecodeBase64(const std::string& text) {
    auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bitsCollected = 0;
    for (char c : text) {
        if (c == '=') {
            break;
        }
        const int v = valueOf(c);
        if (v < 0) {
            continue;
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bitsCollected) & 0xFF));
        }
    }
    return out;
}

TEST(Base64Test, EmptyInput_ReturnsEmptyString)
{
    EXPECT_EQ(EncodeBase64(nullptr, 0), "");

    std::vector<std::uint8_t> empty;
    EXPECT_EQ(EncodeBase64(empty), "");
}

TEST(Base64Test, KnownVector_SingleByte_F)
{
    const std::vector<std::uint8_t> bytes{'f'};
    EXPECT_EQ(EncodeBase64(bytes), "Zg==");
}

TEST(Base64Test, KnownVector_TwoBytes_Fo)
{
    const std::vector<std::uint8_t> bytes{'f', 'o'};
    EXPECT_EQ(EncodeBase64(bytes), "Zm8=");
}

TEST(Base64Test, KnownVector_ThreeBytes_Foo)
{
    const std::vector<std::uint8_t> bytes{'f', 'o', 'o'};
    EXPECT_EQ(EncodeBase64(bytes), "Zm9v");
}

TEST(Base64Test, KnownVector_LongerString_FooBar)
{
    const std::string text = "foobar";
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    EXPECT_EQ(EncodeBase64(bytes), "Zm9vYmFy");
}

TEST(Base64Test, LongBinaryBuffer_RoundTripsThroughReferenceDecoder)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(300);
    for (int i = 0; i < 300; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((i * 37 + 11) & 0xFF));
    }

    const std::string encoded = EncodeBase64(bytes);
    // Length must be a multiple of 4, per base64's own padding rule.
    EXPECT_EQ(encoded.size() % 4, 0u);

    const std::vector<std::uint8_t> decoded = ReferenceDecodeBase64(encoded);
    ASSERT_EQ(decoded.size(), bytes.size());
    EXPECT_EQ(decoded, bytes);
}

TEST(Base64Test, PaddingCharacterCount_MatchesRemainderRule)
{
    // 4 bytes -> 1 remainder byte after the last full 3-byte group -> "=="
    // padding.
    const std::vector<std::uint8_t> oneRemainder{'a', 'b', 'c', 'd'};
    const std::string encodedOne = EncodeBase64(oneRemainder);
    EXPECT_EQ(encodedOne.substr(encodedOne.size() - 2), "==");

    // 5 bytes -> 2 remainder bytes after the last full 3-byte group -> a
    // single "=" padding character.
    const std::vector<std::uint8_t> twoRemainder{'a', 'b', 'c', 'd', 'e'};
    const std::string encodedTwo = EncodeBase64(twoRemainder);
    EXPECT_EQ(encodedTwo.back(), '=');
    EXPECT_NE(encodedTwo[encodedTwo.size() - 2], '=');

    // 0 remainder bytes -> no padding at all.
    const std::vector<std::uint8_t> noRemainder{'a', 'b', 'c'};
    const std::string encodedNone = EncodeBase64(noRemainder);
    EXPECT_EQ(encodedNone.find('='), std::string::npos);
}

} // namespace
} // namespace gte::Encoding
