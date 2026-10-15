// gte_ci_scan: one guardrail check over a source tree (each check folder under tools/ci explains its own use).
// Check mode exits 1 on a new hit or a stale allowlist line; --report lists files with hits and exits 0.
// Exit 2 means the scan itself is broken (bad arguments, nothing scanned, unreadable file).

#include "CppScanner.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace gte::ci;

namespace {

struct Options {
    fs::path root;
    fs::path tokensFile;
    fs::path includesFile;
    fs::path allowlistFile;
    std::vector<std::string> onlyPrefixes;
    std::vector<std::string> skipPrefixes;
    std::vector<std::string> skipDirs;
    MaskOptions mask{ false, true };
    bool ignoreCase = false;
    bool report = false;
};

bool ReadFile(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool ParseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--root") {
            o.root = value();
        } else if (arg == "--tokens") {
            o.tokensFile = value();
        } else if (arg == "--includes") {
            o.includesFile = value();
        } else if (arg == "--allowlist") {
            o.allowlistFile = value();
        } else if (arg == "--only") {
            o.onlyPrefixes.emplace_back(value());
        } else if (arg == "--skip") {
            o.skipPrefixes.emplace_back(value());
        } else if (arg == "--skip-dir") {
            o.skipDirs.emplace_back(value());
        } else if (arg == "--comments") {
            o.mask.scanComments = std::string(value()) == "scan";
        } else if (arg == "--strings") {
            o.mask.scanStrings = std::string(value()) == "scan";
        } else if (arg == "--ignore-case") {
            o.ignoreCase = true;
        } else if (arg == "--report") {
            o.report = true;
        } else {
            std::fprintf(stderr, "gte_ci_scan: unknown argument '%s'\n", arg.c_str());
            return false;
        }
    }
    if (o.root.empty() || (o.tokensFile.empty() && o.includesFile.empty())) {
        std::fprintf(stderr, "gte_ci_scan: --root and at least one of --tokens/--includes are required\n");
        return false;
    }
    return true;
}

bool IsSkipped(const std::string& rel, const Options& o)
{
    if (!o.onlyPrefixes.empty()
        && std::none_of(o.onlyPrefixes.begin(), o.onlyPrefixes.end(),
            [&rel](const std::string& prefix) { return rel.rfind(prefix, 0) == 0; })) {
        return true;
    }
    for (const std::string& prefix : o.skipPrefixes) {
        if (rel.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    const fs::path dirs = fs::path(rel).parent_path();
    for (const fs::path& part : dirs) {
        if (std::find(o.skipDirs.begin(), o.skipDirs.end(), part.string()) != o.skipDirs.end()) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ReadLines(const fs::path& path)
{
    std::string text;
    std::vector<std::string> lines;
    if (path.empty() || !ReadFile(path, text)) {
        return lines;
    }
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        const std::string_view trimmed = Trim(line.substr(0, line.find('#')));
        if (!trimmed.empty()) {
            std::string needle(trimmed);
            std::replace(needle.begin(), needle.end(), '\\', '/');
            lines.push_back(std::move(needle));
        }
    }
    return lines;
}

struct ScanResult {
    HitsByFile found;                      // Relative path -> hits, for every scanned file that has at least one.
    std::size_t scanned = 0;               // Files that were in scope and read.
    std::vector<std::string> unreadable;   // Files in scope that could not be read.
};

ScanResult Scan(const Options& o, const TokenSet& tokens, const std::vector<std::string>& includes)
{
    ScanResult result;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(o.root, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const std::string ext = entry.path().extension().string();
        if (!entry.is_regular_file() || (ext != ".h" && ext != ".cpp")) {
            continue;
        }
        const std::string rel = fs::relative(entry.path(), o.root).generic_string();
        if (IsSkipped(rel, o)) {
            continue;
        }
        std::string text;
        if (!ReadFile(entry.path(), text)) {
            result.unreadable.push_back(rel);
            continue;
        }
        ++result.scanned;
        const std::vector<Kind> kinds = Classify(text);
        std::vector<Hit> hits = FindTokens(Mask(text, kinds, o.mask), tokens);
        std::vector<Hit> includeHits = FindForbiddenIncludes(text, kinds, includes);
        hits.insert(hits.end(), includeHits.begin(), includeHits.end());
        if (!hits.empty()) {
            result.found[rel] = std::move(hits);
        }
    }
    if (ec) {
        result.unreadable.push_back("<directory walk failed: " + ec.message() + ">");
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    Options o;
    if (!ParseArgs(argc, argv, o)) {
        return 2;
    }
    std::string tokenText;
    if (!o.tokensFile.empty() && !ReadFile(o.tokensFile, tokenText)) {
        std::fprintf(stderr, "gte_ci_scan: cannot read %s\n", o.tokensFile.string().c_str());
        return 2;
    }
    const TokenSet tokens = ParseTokens(tokenText, o.ignoreCase);
    const ScanResult scan = Scan(o, tokens, ReadLines(o.includesFile));

    // Fail closed: a scan that saw nothing, or could not read a file, proves nothing.
    if (!scan.unreadable.empty()) {
        for (const std::string& path : scan.unreadable) {
            std::fprintf(stderr, "gte_ci_scan: cannot read %s\n", path.c_str());
        }
        return 2;
    }
    if (scan.scanned == 0) {
        std::fprintf(stderr, "gte_ci_scan: scanned 0 files under %s - wrong --root/--only?\n", o.root.string().c_str());
        return 2;
    }

    if (o.report) {
        std::size_t total = 0;
        for (const auto& [path, hits] : scan.found) {
            std::printf("%s # %zu hits\n", path.c_str(), hits.size());
            total += hits.size();
        }
        std::printf("# scanned %zu files, %zu files with hits, %zu hits\n", scan.scanned, scan.found.size(), total);
        return 0;
    }

    std::vector<AllowEntry> allowed;
    std::string text;
    std::string error;
    if (o.allowlistFile.empty() || !ReadFile(o.allowlistFile, text) || !ParseAllowlist(text, allowed, error)) {
        std::fprintf(stderr, "gte_ci_scan: bad allowlist: %s\n", error.empty() ? "cannot read it" : error.c_str());
        return 2;
    }

    const RatchetResult ratchet = CheckAllowlist(
        scan.found, allowed, [&o](const std::string& rel) { return fs::exists(o.root / rel); });
    for (const std::string& message : ratchet.messages) {
        std::printf("%s\n", message.c_str());
    }
    if (ratchet.failures == 0) {
        std::printf("ok: scanned %zu files, %zu with hits, all on the allowlist\n", scan.scanned, scan.found.size());
    }
    return ratchet.failures == 0 ? 0 : 1;
}
