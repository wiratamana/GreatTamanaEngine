// editor-core-separation-4 campaign, PHASE6
// (PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md) - Tier-1 tests for the new,
// pure ReadFixedBuffer() helper (src/Core/Plugins/FixedBufferReader.h),
// proving the bounded strnlen()-based read is genuinely safe: a normal
// null-terminated buffer reads correctly, a deliberately NON-terminated
// full buffer reads exactly its fixed size and never more, and an empty
// buffer reads as an empty string.

#include "Core/Plugins/FixedBufferReader.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

TEST(FixedBufferReaderTest, ReadFixedBuffer_NormalNullTerminatedStringReadsCorrectly)
{
    char buffer[16] = {};
    std::strncpy(buffer, "hello", sizeof(buffer) - 1);
    EXPECT_EQ(gte::ReadFixedBuffer(buffer, sizeof(buffer)), "hello");
}

TEST(FixedBufferReaderTest, ReadFixedBuffer_NonTerminatedFullBufferReadsExactlyItsFixedSizeNeverMore)
{
    char buffer[16];
    std::memset(buffer, 'A', sizeof(buffer)); // deliberately NO null terminator anywhere
    const std::string result = gte::ReadFixedBuffer(buffer, sizeof(buffer));
    EXPECT_EQ(result.size(), sizeof(buffer));
    EXPECT_EQ(result, std::string(sizeof(buffer), 'A'));
}

TEST(FixedBufferReaderTest, ReadFixedBuffer_EmptyBufferReadsEmptyString)
{
    char buffer[16] = {};
    EXPECT_EQ(gte::ReadFixedBuffer(buffer, sizeof(buffer)), "");
}
