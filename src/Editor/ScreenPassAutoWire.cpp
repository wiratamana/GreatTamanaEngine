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
//
// better-render-pass-1 campaign, PHASE9 (R6 - Finding 10(b)), IDEMPOTENCY
// GUARD FIX: the ORIGINAL idempotency guard (Step 3, below, before this
// phase) only ever scanned for an already-ACTIVE call line
// (`registerFunctionName + "(core);"` on a non-comment line) - if that
// single scan found nothing, BOTH a new forward declaration AND a new call
// line were inserted unconditionally. If a human manually commented out
// ONLY the call line (e.g. "// RegisterFooScreenPass(core);", to
// temporarily disable one effect - a workflow this file's own header
// comment above already anticipates) while the forward declaration itself
// remained active/un-commented, re-triggering the scaffolding tool for that
// same pass name produced a genuine, confirmed DUPLICATE forward
// declaration (the old, still-active one survives; a second one gets
// inserted right alongside it). Fixed by making each of the two presence
// checks - "is the forward declaration already active?" and "is the call
// already active?" - fully INDEPENDENT, each gating its own, independent
// insertion. The two insertions are resolved against ONE shared ordering
// helper (see InsertPendingLines() below) so this remains correct whether
// BOTH lines need inserting, or only ONE of them does.
#include "ScreenPassAutoWire.h"

#include "../Core/Logging.h"

#include <algorithm>
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

// True if some NON-COMMENT line contains `needle` - the shared primitive
// behind BOTH of PHASE9's independent "is this already active?" checks
// (the active call line, and the active forward-declaration line).
bool AnyNonCommentLineContains(const std::vector<std::string>& lines, const std::string& needle)
{
    for (const std::string& line : lines) {
        if (IsCommentLine(line)) {
            continue;
        }
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
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

// better-render-pass-1 campaign, PHASE9 - one line still waiting to be
// inserted, at the index that was resolved against the ORIGINAL,
// pre-any-insertion `lines` vector.
struct PendingLineInsertion {
    std::size_t index;
    std::string line;
};

// Inserts every entry of `insertions` into `lines`, each at the index it
// was originally resolved against (BEFORE any of these insertions
// happened). Insertions are applied in DESCENDING index order so an
// earlier-applied insertion's own already-resolved index is never
// invalidated by a later one that lands before it - this is exactly the
// ORIGINAL (pre-PHASE9) "insert at the larger index first" discipline
// (Step 3.3), generalized here to correctly handle EITHER one OR both
// pending insertions actually happening, with no duplicated branching
// logic for the two cases.
//
// When two insertions tie at the exact same resolved index,
// std::stable_sort preserves their RELATIVE ORDER from `insertions` itself
// - and since a later-processed insertion at an identical index always
// ends up landing IN FRONT of an earlier one already inserted there (the
// earlier one gets pushed one slot further along), whichever entry appears
// LATER in `insertions` ends up physically BEFORE the one that appears
// EARLIER, once both have been applied. Callers needing a specific
// relative order for a same-index tie must therefore order `insertions`
// with that in mind (see TryAutoWireRegisterCall() below, which relies on
// this to put the forward declaration before the call line for a tied
// index, exactly matching the ORIGINAL implementation's own tie-breaking
// behavior).
void InsertPendingLines(std::vector<std::string>& lines, std::vector<PendingLineInsertion> insertions)
{
    std::stable_sort(insertions.begin(), insertions.end(),
        [](const PendingLineInsertion& a, const PendingLineInsertion& b) { return a.index > b.index; });
    for (const PendingLineInsertion& insertion : insertions) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertion.index), insertion.line);
    }
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

    // Step 3 (idempotency guard, PHASE9: TWO INDEPENDENT presence checks,
    // not one): scan every NON-COMMENT line for an already-active CALL, and
    // separately for an already-active FORWARD DECLARATION. A commented-out
    // line (e.g. a human manually disabling one effect:
    // "// RegisterFooScreenPass(core);") must NOT be mistaken for
    // still-active - IsCommentLine() skips any such line in both checks.
    const std::string activeCallNeedle = registerFunctionName + "(core);";
    const std::string forwardDeclLine = "void " + registerFunctionName + "(gte::Core& core);";
    const std::string callLine = "    " + registerFunctionName + "(core);";

    const bool callAlreadyActive = AnyNonCommentLineContains(lines, activeCallNeedle);
    if (callAlreadyActive) {
        // Already correctly, ACTIVELY wired - nothing to do, regardless of
        // the forward declaration's own state (if the call is active, the
        // forward declaration must already be active too, or this project
        // would not compile - nothing further to check or insert).
        return true;
    }
    const bool forwardDeclAlreadyActive = AnyNonCommentLineContains(lines, forwardDeclLine);

    // Resolve BOTH insertion indices from the ORIGINAL (pre-insert) lines
    // vector first - PHASE9: each of the two lines below is now inserted
    // INDEPENDENTLY, conditioned on its OWN already-active flag. Having
    // reached this point, `callAlreadyActive` is always false (the function
    // already returned early above if it were true), so the call line is
    // unconditionally scheduled for insertion; only the forward-declaration
    // insertion is truly conditional.
    const std::size_t forwardDeclInsertionIndex = FindInsertionIndexAfterAnchorBlock(lines, forwardDeclAnchorIndex);
    const std::size_t bodyInsertionIndex = FindInsertionIndexAfterAnchorBlock(lines, bodyAnchorIndex);

    std::vector<PendingLineInsertion> insertions;
    insertions.push_back({ bodyInsertionIndex, callLine });
    if (!forwardDeclAlreadyActive) {
        insertions.push_back({ forwardDeclInsertionIndex, forwardDeclLine });
    }
    InsertPendingLines(lines, std::move(insertions));

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
