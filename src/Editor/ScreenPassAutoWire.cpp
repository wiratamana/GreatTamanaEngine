// src/Editor/ScreenPassAutoWire.cpp
//
// editor-core-separation-24 campaign, PHASE4
// (PHASE4_AUTO_WIRE_HELPER_TRY_AUTO_WIRE_REGISTER_CALL.md). See
// ScreenPassAutoWire.h for the full contract.
//
// DISCOVERED DEVIATION FROM THIS PHASE'S OWN WRITTEN STEP 3.1/3.2/3.3 TEXT,
// CONFIRMED VIA `ask_questions` BEFORE IMPLEMENTING (see
// PHASE4_COMPLETION_REPORT.md's own dedicated section for the full writeup -
// restated here too, at the point it actually matters, so a future reader of
// just this .cpp file is never misled):
//
// 1. The phase file's own Step 3.1/3.2 quote each anchor comment as if it
//    were a single physical output line ending in a period, e.g.
//    "// GTE_AUTO_REGISTER_FORWARD_DECLARATIONS - do not remove or edit this
//    line.". Re-reading PHASE2's OWN ACTUAL, ALREADY-COMMITTED template
//    output (EditorProjectLifecycleCapability.cpp's
//    BuildGameStubCppContent()) confirms that is NOT what a real generated
//    <ProjectName>Game.cpp file actually contains: the forward-declaration
//    anchor's own comment wraps its sentence across SEVEN physical lines
//    ("...do not remove or edit this" / "// line. The Editor's..." / ...),
//    and the body anchor's own comment wraps across seven lines too. A
//    literal, whole-line-equality search for either phase-file-quoted
//    sentence would therefore NEVER match any real generated file,
//    permanently, silently breaking this entire feature for every future
//    project. User-confirmed fix: locate each anchor by a plain substring
//    search for its bare keyword token - "GTE_AUTO_REGISTER_FORWARD_DECLARATIONS"
//    / "GTE_AUTO_REGISTER_ANCHOR" - which appears EXACTLY ONCE, on its own
//    comment line, nowhere else in a real generated file. Still a plain
//    `std::string` find-based search, never `<regex>`.
// 2. Given each anchor is a genuine multi-line comment BLOCK (not one
//    line), the new code line is inserted immediately after the WHOLE
//    anchor comment block ends - i.e. scan forward from the marker's own
//    line, skip every subsequent line that is ALSO a comment line (trimmed
//    content starts with "//"), and insert right before the first
//    non-comment line found (a blank line before "namespace {" for the
//    forward-declaration anchor; the closing "}" for the body anchor) -
//    user-confirmed as the cleaner option over splitting a line into the
//    middle of the wrapped comment paragraph.
#include "ScreenPassAutoWire.h"

#include "../Core/Logging.h"

#include <fstream>
#include <sstream>
#include <vector>

namespace gte {

namespace {

// The two anchor keyword tokens - see the file header comment above for why
// these bare tokens are searched for instead of a full quoted sentence.
constexpr const char* kForwardDeclAnchorMarker = "GTE_AUTO_REGISTER_FORWARD_DECLARATIONS";
constexpr const char* kBodyAnchorMarker = "GTE_AUTO_REGISTER_ANCHOR";

constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

std::string TrimLeadingWhitespace(const std::string& line)
{
    std::size_t start = 0;
    while (start < line.size() &&
        (line[start] == ' ' || line[start] == '\t' || line[start] == '\r')) {
        ++start;
    }
    return line.substr(start);
}

bool IsCommentLine(const std::string& line)
{
    return TrimLeadingWhitespace(line).rfind("//", 0) == 0;
}

// Splits on '\n' only - this file is always written by this same engine's
// own WriteTextFile(), which never adds a BOM and always ends with '\n', so
// a straightforward '\n'-delimited split-and-rejoin is safe here (matching
// this phase file's own Step 3.3 guidance - do not over-engineer CRLF
// handling beyond what this codebase's own existing helpers already
// assume). If the content ends with '\n' (the normal case), the final
// element of the returned vector is an empty string, which JoinLines()
// below correctly turns back into a single trailing '\n' when rejoined -
// never an extra blank line.
std::vector<std::string> SplitLines(const std::string& content)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t newlinePos = content.find('\n', start);
        if (newlinePos == std::string::npos) {
            lines.push_back(content.substr(start));
            break;
        }
        lines.push_back(content.substr(start, newlinePos - start));
        start = newlinePos + 1;
    }
    return lines;
}

std::string JoinLines(const std::vector<std::string>& lines)
{
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i + 1 < lines.size()) {
            result += '\n';
        }
    }
    return result;
}

// Returns the 0-based index of the first line whose content contains
// `marker`, or kNotFound if no line contains it.
std::size_t FindLineContaining(const std::vector<std::string>& lines, const char* marker)
{
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(marker) != std::string::npos) {
            return i;
        }
    }
    return kNotFound;
}

// User-confirmed placement rule: skip every comment line immediately
// following the marker's own line (the rest of that same wrapped comment
// paragraph), and return the index of the first non-comment line after it -
// the new code line is inserted THERE (pushing that line, and everything
// after it, down by one), landing cleanly after the whole anchor comment
// block instead of splitting it.
std::size_t FindInsertionIndexAfterAnchorBlock(const std::vector<std::string>& lines, std::size_t markerLineIndex)
{
    std::size_t i = markerLineIndex + 1;
    while (i < lines.size() && IsCommentLine(lines[i])) {
        ++i;
    }
    return i;
}

} // namespace

bool TryAutoWireRegisterCall(const std::filesystem::path& projectGameCppPath,
    const std::string& registerFunctionName)
{
    std::ifstream inStream(projectGameCppPath, std::ios::binary);
    if (!inStream.is_open()) {
        // Missing file (e.g. an old-project fixture predating this whole
        // convention) - safe, silent, all-or-nothing "no" - nothing written.
        return false;
    }
    std::ostringstream buffer;
    buffer << inStream.rdbuf();
    const std::string originalContent = buffer.str();
    inStream.close();

    // Step 2: both anchor keyword tokens must be present SOMEWHERE in the
    // file, or this is an all-or-nothing refusal - nothing written.
    if (originalContent.find(kForwardDeclAnchorMarker) == std::string::npos ||
        originalContent.find(kBodyAnchorMarker) == std::string::npos) {
        return false;
    }

    std::vector<std::string> lines = SplitLines(originalContent);

    const std::size_t forwardDeclAnchorIndex = FindLineContaining(lines, kForwardDeclAnchorMarker);
    const std::size_t bodyAnchorIndex = FindLineContaining(lines, kBodyAnchorMarker);
    if (forwardDeclAnchorIndex == kNotFound || bodyAnchorIndex == kNotFound) {
        // Should be unreachable given the whole-content check above (a
        // marker found in the raw blob but not in any split line would mean
        // SplitLines() itself is broken) - stay safe rather than assert.
        return false;
    }

    // Step 3 (idempotency guard): scan every NON-COMMENT line for an already
    // active call. A commented-out call (e.g. a human manually disabling one
    // effect: "// RegisterFooScreenPass(core);") must NOT be mistaken for
    // still-active - skip any line that is itself a comment line.
    const std::string activeCallNeedle = registerFunctionName + "(core);";
    for (const std::string& line : lines) {
        if (IsCommentLine(line)) {
            continue;
        }
        if (line.find(activeCallNeedle) != std::string::npos) {
            // Already correctly, ACTIVELY wired - nothing to do.
            return true;
        }
    }

    const std::string forwardDeclLine = "void " + registerFunctionName + "(gte::Core& core);";
    const std::string callLine = "    " + registerFunctionName + "(core);";

    // Resolve BOTH insertion indices from the ORIGINAL (pre-insert) lines
    // vector first, then insert at the LARGER index first, so an earlier
    // insertion never shifts a not-yet-processed insertion point (Step 3.3).
    const std::size_t forwardDeclInsertionIndex = FindInsertionIndexAfterAnchorBlock(lines, forwardDeclAnchorIndex);
    const std::size_t bodyInsertionIndex = FindInsertionIndexAfterAnchorBlock(lines, bodyAnchorIndex);

    if (bodyInsertionIndex >= forwardDeclInsertionIndex) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(bodyInsertionIndex), callLine);
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(forwardDeclInsertionIndex), forwardDeclLine);
    } else {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(forwardDeclInsertionIndex), forwardDeclLine);
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(bodyInsertionIndex), callLine);
    }

    const std::string newContent = JoinLines(lines);

    std::ofstream outStream(projectGameCppPath, std::ios::binary);
    if (!outStream.is_open()) {
        GTE_LOG_ERROR("ScreenPassAutoWire",
            "TryAutoWireRegisterCall: failed to open " + projectGameCppPath.string() +
            " for writing while auto-wiring " + registerFunctionName);
        return false;
    }
    outStream << newContent;
    if (!outStream.good()) {
        GTE_LOG_ERROR("ScreenPassAutoWire",
            "TryAutoWireRegisterCall: failed to write " + projectGameCppPath.string() +
            " while auto-wiring " + registerFunctionName);
        return false;
    }
    return true;
}

} // namespace gte
