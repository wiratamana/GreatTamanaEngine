// src/Editor/ScreenPassPriorityAssignment.cpp
//
// editor-core-separation-24 campaign, PHASE5
// (PHASE5_PRIORITY_AUTO_ASSIGNMENT_HELPER.md). See
// ScreenPassPriorityAssignment.h for the full contract.
//
// Note on ToLowerAsciiLocal() below (see this phase file's own Step 2 for
// the full reasoning): EditorProjectLifecycleCapability.cpp already has a
// ToLowerAscii() helper, but it lives inside that file's own anonymous
// namespace and has INTERNAL linkage - it is not reachable from this,
// separate translation unit. Per this phase's own Step 2 ("either approach
// is acceptable; pick whichever produces the smaller, clearer diff"), this
// file duplicates a tiny, private, file-local lowercase helper instead of
// promoting the existing one to a shared public header - this is a
// deliberate, NOT-accidental duplication of a genuinely tiny (5-line)
// helper, consistent with this codebase's general tolerance for small,
// single-purpose, non-shared helpers (confirmed by PHASE4's own
// ScreenPassAutoWire.cpp precedent, which took the same approach for its
// own small, file-local helpers rather than reaching into
// EditorProjectLifecycleCapability.cpp's internals).
#include "ScreenPassPriorityAssignment.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string>

namespace gte {

namespace {

// File-local lowercase helper - see this file's own header comment above for
// why this is NOT the same ToLowerAscii() EditorProjectLifecycleCapability.cpp
// uses internally (that one has internal linkage and is not reachable here).
std::string ToLowerAsciiLocal(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

std::int32_t ComputeNextScreenPassPriority(const std::filesystem::path& assetsDirectory)
{
    std::error_code iterationError;
    if (!std::filesystem::is_directory(assetsDirectory, iterationError)) {
        return 0;
    }

    static const std::string kSuffix = "screenpass.cpp"; // already-lowered, matches ToLowerAsciiLocal(filename) below.
    static const std::string kToken = "/*priority=*/";

    bool foundAny = false;
    std::int32_t highest = 0;

    for (const auto& entry : std::filesystem::directory_iterator(assetsDirectory, iterationError)) {
        if (!entry.is_regular_file()) continue;
        const std::string lowerName = ToLowerAsciiLocal(entry.path().filename().string());
        if (lowerName.size() < kSuffix.size()
            || lowerName.compare(lowerName.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }

        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        const std::size_t tokenPos = content.find(kToken);
        if (tokenPos == std::string::npos) continue;

        std::size_t digitsStart = tokenPos + kToken.size();
        std::size_t digitsEnd = digitsStart;
        if (digitsEnd < content.size() && content[digitsEnd] == '-') ++digitsEnd;
        while (digitsEnd < content.size() && std::isdigit(static_cast<unsigned char>(content[digitsEnd]))) {
            ++digitsEnd;
        }
        if (digitsEnd == digitsStart) continue; // token present but nothing parseable followed it.

        std::int32_t parsed = 0;
        const auto parseResult = std::from_chars(
            content.data() + digitsStart, content.data() + digitsEnd, parsed);
        if (parseResult.ec != std::errc{}) continue; // failed to parse - skip, never crash the scan.

        if (!foundAny || parsed > highest) {
            highest = parsed;
        }
        foundAny = true;
    }

    return foundAny ? (highest + 1) : 0;
}

} // namespace gte
