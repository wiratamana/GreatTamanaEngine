#include "Base64.h"

namespace gte::Encoding {

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

} // namespace

std::string EncodeBase64(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return std::string();
    }

    std::string result;
    // Every 3 input bytes become 4 output chars, rounded up - the standard
    // base64 expansion ratio.
    result.reserve(((size + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= size) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8) |
            static_cast<std::uint32_t>(data[i + 2]);

        result.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        result.push_back(kBase64Alphabet[(chunk >> 6) & 0x3F]);
        result.push_back(kBase64Alphabet[chunk & 0x3F]);

        i += 3;
    }

    const std::size_t remaining = size - i;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        result.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        result.push_back('=');
        result.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                     (static_cast<std::uint32_t>(data[i + 1]) << 8);
        result.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        result.push_back(kBase64Alphabet[(chunk >> 6) & 0x3F]);
        result.push_back('=');
    }

    return result;
}

std::string EncodeBase64(const std::vector<std::uint8_t>& bytes) {
    return EncodeBase64(bytes.empty() ? nullptr : bytes.data(), bytes.size());
}

} // namespace gte::Encoding
