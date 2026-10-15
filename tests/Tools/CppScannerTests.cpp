// Tier-1 tests for the CI guardrail scanner: tokenizer, token matching, include scan, allowlist parsing.

#include "CppScanner.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte::ci {
namespace {

std::vector<std::string> Tokens(const std::vector<Hit>& hits)
{
    std::vector<std::string> out;
    for (const Hit& hit : hits) {
        out.push_back(hit.token);
    }
    return out;
}

std::vector<Hit> Scan(const std::string& source, const std::string& tokenFile, MaskOptions mask, bool ignoreCase = false)
{
    return FindTokens(Mask(source, Classify(source), mask), ParseTokens(tokenFile, ignoreCase));
}

constexpr MaskOptions kCodeOnly{ false, false };
constexpr MaskOptions kCodeAndStrings{ false, true };
constexpr MaskOptions kEverything{ true, true };

TEST(CppScannerTest, LineCommentIsSkippedOrScanned)
{
    const std::string source = "int a; // VkHidden\nint VkShown;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkShown" });
    EXPECT_EQ(Scan(source, "Vk*", kEverything).size(), 2u);
}

TEST(CppScannerTest, SlashSlashInsideAStringIsNotAComment)
{
    const std::string source = "const char* s = \"a // VkInString\"; int VkInCode;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeAndStrings)), (std::vector<std::string>{ "VkInString", "VkInCode" }));
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkInCode" });
}

TEST(CppScannerTest, BlockCommentSwallowsALineCommentMarker)
{
    const std::string source = "/* a // b */ int VkAfter; /* VkInside */\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkAfter" });
}

TEST(CppScannerTest, LineCommentContinuesAcrossAnEscapedNewline)
{
    const std::string source = "// hidden \\\nVkStillHidden\nVkShown\n";
    const std::vector<Hit> hits = Scan(source, "Vk*", kCodeOnly);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].token, "VkShown");
    EXPECT_EQ(hits[0].line, 3u);
}

TEST(CppScannerTest, RawStringHidesItsContentsFromCodeScans)
{
    const std::string source = "auto s = R\"d(// VkNo \" still inside )d\"; int VkYes;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkYes" });
    EXPECT_EQ(Scan(source, "Vk*", kCodeAndStrings).size(), 2u);
}

TEST(CppScannerTest, EncodingPrefixedRawStringIsStillRaw)
{
    const std::string source = "auto s = u8R\"x(VkNo)x\"; int VkYes;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkYes" });
}

TEST(CppScannerTest, DigitSeparatorDoesNotStartACharLiteral)
{
    const std::string source = "int x = 1'000'000; int VkAfter;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkAfter" });
}

TEST(CppScannerTest, EscapedQuoteInACharLiteralDoesNotEndItEarly)
{
    const std::string source = "char c = '\\''; int VkAfter; char d = 'VkInChar';\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkAfter" });
}

TEST(CppScannerTest, EscapedQuoteInAStringDoesNotEndItEarly)
{
    const std::string source = "const char* s = \"a \\\" VkInString\"; int VkAfter;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkAfter" });
}

TEST(CppScannerTest, UnterminatedStringEndsAtTheNewline)
{
    const std::string source = "const char* s = \"oops\nint VkAfter;\n";
    EXPECT_EQ(Tokens(Scan(source, "Vk*", kCodeOnly)), std::vector<std::string>{ "VkAfter" });
}

TEST(CppScannerTest, MaskKeepsLengthAndNewlines)
{
    const std::string source = "a /* x\ny */ b // z\n";
    const std::string masked = Mask(source, Classify(source), kCodeOnly);
    ASSERT_EQ(masked.size(), source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        EXPECT_EQ(masked[i] == '\n', source[i] == '\n') << "offset " << i;
    }
}

TEST(CppScannerTest, WholeWordDoesNotMatchInsideALongerIdentifier)
{
    const std::string source = "Renderer& r; ShadowMaskRenderer m; Renderer2 x; renderer y;\n";
    const std::vector<Hit> hits = Scan(source, "Renderer", kCodeOnly);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].token, "Renderer");
}

TEST(CppScannerTest, WildcardNeedsAnUppercaseLetterAfterThePrefix)
{
    const std::string source = "vkCmdDraw(); vkernel = 1; VK_SUCCESS; VkBuffer b; Vkx y;\n";
    EXPECT_EQ(Tokens(Scan(source, "vk*\nVk*\nVK_*", kCodeOnly)),
        (std::vector<std::string>{ "vkCmdDraw", "VK_SUCCESS", "VkBuffer" }));
}

TEST(CppScannerTest, PhraseWithPunctuationMatchesDotAndArrowCalls)
{
    const std::string source = "a.Native(); b->Native(); Native(h); GetNative(); xNative();\n";
    const std::vector<Hit> hits = Scan(source, "Native(", kCodeOnly);
    EXPECT_EQ(hits.size(), 3u); // The calls on a, b and the free function; not GetNative / xNative.
}

TEST(CppScannerTest, PhraseEndingInAnIdentifierRespectsTheRightBoundary)
{
    const std::string source = "ctx.cmd; ctx.cmdBuffer;\n";
    EXPECT_EQ(Scan(source, "ctx.cmd", kCodeOnly).size(), 1u);
}

TEST(CppScannerTest, IgnoreCaseMatchesAnyCapitalisation)
{
    const std::string source = "int atmosphere; int ATMOSPHERE; int Atmospheres;\n";
    EXPECT_EQ(Scan(source, "Atmosphere", kCodeOnly, /*ignoreCase=*/true).size(), 2u);
}

TEST(CppScannerTest, HitsCarryOneBasedLineNumbers)
{
    const std::string source = "int a;\n\nint VkAt3;\n";
    const std::vector<Hit> hits = Scan(source, "Vk*", kCodeOnly);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].line, 3u);
}

TEST(CppScannerTest, TokenFileIgnoresCommentsAndBlankLines)
{
    const TokenSet set = ParseTokens("# header\n\nFoo # trailing\nVk*\n.Native(\n", false);
    EXPECT_EQ(set.words.count("Foo"), 1u);
    EXPECT_EQ(set.prefixes, std::vector<std::string>{ "Vk" });
    EXPECT_EQ(set.phrases, std::vector<std::string>{ ".Native(" });
}

std::vector<std::string> IncludeTokens(const std::string& source, const std::vector<std::string>& forbidden)
{
    return Tokens(FindForbiddenIncludes(source, Classify(source), forbidden));
}

TEST(CppScannerTest, ForbiddenIncludeIsFoundWithAnyPathPrefix)
{
    EXPECT_EQ(IncludeTokens("#include \"../../Renderer/Foo.h\"\n", { "Renderer/" }).size(), 1u);
    EXPECT_EQ(IncludeTokens("  #  include <Core/Core.h>\n", { "Core/Core.h" }).size(), 1u);
    EXPECT_EQ(IncludeTokens("#include \"..\\\\Renderer\\\\Foo.h\"\n", { "Renderer/" }).size(), 1u);
}

TEST(CppScannerTest, AllowedIncludeIsNotFlagged)
{
    EXPECT_TRUE(IncludeTokens("#include \"Core/FeatureKit/Kit.h\"\n", { "Core/Core.h", "Renderer/" }).empty());
}

TEST(CppScannerTest, IncludeInACommentOrStringIsIgnored)
{
    const std::string source = "// #include \"Renderer/a.h\"\n/* #include \"Renderer/b.h\" */\n"
                               "const char* s = \"#include \\\"Renderer/c.h\\\"\";\n"
                               "auto t = R\"(\n#include \"Renderer/d.h\"\n)\";\n";
    EXPECT_TRUE(IncludeTokens(source, { "Renderer/" }).empty());
}

TEST(CppScannerTest, AllowlistNormalisesSlashesAndKeepsReasons)
{
    std::vector<AllowEntry> entries;
    std::string error;
    ASSERT_TRUE(ParseAllowlist("# header\nGame\\Game.cpp # SDL window\n\nCore/Core.h # frame loop\n", entries, error));
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].path, "Game/Game.cpp");
    EXPECT_EQ(entries[0].reason, "SDL window");
    EXPECT_EQ(entries[0].line, 2u);
    EXPECT_EQ(entries[1].path, "Core/Core.h");
}

TEST(CppScannerTest, AllowlistLineWithoutAReasonIsRejected)
{
    std::vector<AllowEntry> entries;
    std::string error;
    EXPECT_FALSE(ParseAllowlist("Game/Game.cpp\n", entries, error));
    EXPECT_NE(error.find("Game/Game.cpp"), std::string::npos);
    EXPECT_FALSE(ParseAllowlist("Game/Game.cpp #   \n", entries, error));
}

TEST(CppScannerTest, ContainsTokenMatchesInsideIdentifiersCommentsAndStrings)
{
    const std::string source = "int AtmosphereLutRenderer; int ATMOSPHERE_PHASE2_X; // see atmosphere docs\n"
                               "const char* s = \"Features/Atmosphere/Foo.h\"; int Unrelated;\n";
    const std::vector<Hit> hits = Scan(source, "*Atmosphere*", kEverything, /*ignoreCase=*/true);
    EXPECT_EQ(Tokens(hits),
        (std::vector<std::string>{ "AtmosphereLutRenderer", "ATMOSPHERE_PHASE2_X", "atmosphere", "Atmosphere" }));
}

TEST(CppScannerTest, ContainsTokenIsCaseSensitiveWithoutIgnoreCase)
{
    const std::string source = "int AtmosphereX; int atmosphereY;\n";
    EXPECT_EQ(Tokens(Scan(source, "*Atmosphere*", kCodeOnly)), std::vector<std::string>{ "AtmosphereX" });
}

TEST(CppScannerTest, WholeWordTokenStillIgnoresLongerIdentifiersNextToAContainsToken)
{
    const std::string source = "int Sky; int SkyBox; int SkyShadow;\n";
    EXPECT_EQ(Tokens(Scan(source, "Sky\n*Shadow*", kCodeOnly, /*ignoreCase=*/true)),
        (std::vector<std::string>{ "Sky", "SkyShadow" }));
}

TEST(CppScannerTest, ContainsTokenIsParsedIntoItsOwnList)
{
    const TokenSet set = ParseTokens("*Shadow*\nSky\nVk*\n", true);
    EXPECT_EQ(set.contains, std::vector<std::string>{ "shadow" });
    EXPECT_EQ(set.words.count("sky"), 1u);
    EXPECT_EQ(set.prefixes, std::vector<std::string>{ "vk" });
}

TEST(CppScannerTest, CallPhraseMatchesWhitespaceAndNewlineBeforeTheParenthesis)
{
    const std::string source = "a.Native ();\nb\n    .Native\n(h);\nNative (h);\nxNative(h);\nNativeX(h);\nNative;\n";
    const std::vector<Hit> hits = Scan(source, "Native(", kCodeOnly);
    ASSERT_EQ(hits.size(), 3u);
    EXPECT_EQ(hits[0].line, 1u);
    EXPECT_EQ(hits[1].line, 3u);
    EXPECT_EQ(hits[2].line, 5u);
}

HitsByFile OneHit(const std::string& path)
{
    HitsByFile found;
    found[path].push_back(Hit{ 7, "Token" });
    return found;
}

AllowEntry Entry(const std::string& path, std::size_t line)
{
    return AllowEntry{ path, "reason", line };
}

const auto kAllExist = [](const std::string&) { return true; };

TEST(CppScannerTest, RatchetPassesWhenEveryHitFileIsListed)
{
    const RatchetResult result = CheckAllowlist(OneHit("a.cpp"), { Entry("a.cpp", 1) }, kAllExist);
    EXPECT_EQ(result.failures, 0);
    EXPECT_TRUE(result.messages.empty());
}

TEST(CppScannerTest, RatchetReportsANewViolationWithFileLineAndToken)
{
    const RatchetResult result = CheckAllowlist(OneHit("b.cpp"), {}, kAllExist);
    EXPECT_EQ(result.failures, 1);
    ASSERT_EQ(result.messages.size(), 1u);
    EXPECT_EQ(result.messages[0], "violation: b.cpp:7: Token");
}

TEST(CppScannerTest, RatchetFailsOnAStaleLineForAMissingFile)
{
    const auto missing = [](const std::string& path) { return path != "gone.cpp"; };
    const RatchetResult result = CheckAllowlist({}, { Entry("gone.cpp", 4) }, missing);
    EXPECT_EQ(result.failures, 1);
    ASSERT_EQ(result.messages.size(), 1u);
    EXPECT_NE(result.messages[0].find("no longer exists"), std::string::npos);
    EXPECT_NE(result.messages[0].find("line 4"), std::string::npos);
}

TEST(CppScannerTest, RatchetFailsOnAStaleLineForACleanFile)
{
    const RatchetResult result = CheckAllowlist({}, { Entry("clean.cpp", 2) }, kAllExist);
    EXPECT_EQ(result.failures, 1);
    ASSERT_EQ(result.messages.size(), 1u);
    EXPECT_NE(result.messages[0].find("is clean now"), std::string::npos);
}

TEST(CppScannerTest, RatchetFailsOnADuplicateLine)
{
    const RatchetResult result = CheckAllowlist(OneHit("a.cpp"), { Entry("a.cpp", 1), Entry("a.cpp", 2) }, kAllExist);
    EXPECT_EQ(result.failures, 1);
    ASSERT_EQ(result.messages.size(), 1u);
    EXPECT_NE(result.messages[0].find("listed twice"), std::string::npos);
}

TEST(CppScannerTest, RatchetCountsEveryProblemOnce)
{
    HitsByFile found = OneHit("new.cpp");
    found["new.cpp"].push_back(Hit{ 9, "Other" });
    const RatchetResult result = CheckAllowlist(found, { Entry("clean.cpp", 1) }, kAllExist);
    EXPECT_EQ(result.failures, 2); // One stale line plus one file with violations.
    EXPECT_EQ(result.messages.size(), 3u);
}
} // namespace
} // namespace gte::ci
