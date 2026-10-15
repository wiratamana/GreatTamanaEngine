#pragma once

// Token-level C++ scanner for the CI guardrail checks (std-only). Classifies every byte as code, comment,
// string or char literal (see Mask), then matches whole identifiers (see FindTokens).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gte::ci {

enum class Kind : std::uint8_t { Code, LineComment, BlockComment, String, Char };

inline bool IsIdentChar(char c) noexcept
{
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u >= 0x80;
}

// Marks the end of a quoted literal that starts at `from` (the opening quote). Stops at an unescaped
// closing quote or at an unescaped newline (unterminated literal).
inline std::size_t EndOfQuoted(std::string_view t, std::size_t from, char quote) noexcept
{
    std::size_t j = from + 1;
    while (j < t.size()) {
        if (t[j] == '\\') {
            j += 2;
            continue;
        }
        if (t[j] == quote) {
            return j + 1;
        }
        if (t[j] == '\n') {
            return j;
        }
        ++j;
    }
    return t.size();
}

// End of a raw string literal whose opening quote is at `from`; npos when `from` is not a raw string.
inline std::size_t EndOfRawString(std::string_view t, std::size_t from) noexcept
{
    if (from == 0 || t[from - 1] != 'R') {
        return std::string_view::npos;
    }
    std::size_t s = from - 1; // Allow the u8 / u / U / L encoding prefixes before the R.
    while (s > 0 && IsIdentChar(t[s - 1])) {
        --s;
    }
    const std::string_view prefix = t.substr(s, from - 1 - s);
    if (!(prefix.empty() || prefix == "u8" || prefix == "u" || prefix == "U" || prefix == "L")) {
        return std::string_view::npos;
    }
    const std::size_t open = t.find('(', from + 1);
    if (open == std::string_view::npos || open - (from + 1) > 16) {
        return std::string_view::npos;
    }
    const std::string closer = ")" + std::string(t.substr(from + 1, open - from - 1)) + "\"";
    const std::size_t end = t.find(closer, open + 1);
    return end == std::string_view::npos ? t.size() : end + closer.size();
}

// True when the quote at `i` is a digit separator (1'000), not the start of a char literal.
inline bool IsDigitSeparator(std::string_view t, std::size_t i) noexcept
{
    if (i == 0 || !IsIdentChar(t[i - 1])) {
        return false;
    }
    std::size_t s = i;
    while (s > 0 && (IsIdentChar(t[s - 1]) || t[s - 1] == '\'' || t[s - 1] == '.')) {
        --s;
    }
    return t[s] >= '0' && t[s] <= '9';
}

// Classifies every byte. A line comment ends at the first newline not escaped by a backslash;
// the newline itself stays Code.
inline std::vector<Kind> Classify(std::string_view t)
{
    std::vector<Kind> kinds(t.size(), Kind::Code);
    const auto fill = [&](std::size_t from, std::size_t to, Kind kind) {
        std::fill(kinds.begin() + static_cast<std::ptrdiff_t>(from), kinds.begin() + static_cast<std::ptrdiff_t>(to), kind);
    };

    std::size_t i = 0;
    while (i < t.size()) {
        const char c = t[i];
        const char next = i + 1 < t.size() ? t[i + 1] : '\0';
        if (c == '/' && next == '/') {
            std::size_t j = i + 2;
            while (j < t.size()) {
                if (t[j] == '\n') {
                    std::size_t p = j;
                    p -= (p > i && t[p - 1] == '\r') ? 1 : 0;
                    if (p > i && t[p - 1] == '\\') {
                        ++j;
                        continue;
                    }
                    break;
                }
                ++j;
            }
            fill(i, j, Kind::LineComment);
            i = j;
        } else if (c == '/' && next == '*') {
            const std::size_t close = t.find("*/", i + 2);
            const std::size_t end = close == std::string_view::npos ? t.size() : close + 2;
            fill(i, end, Kind::BlockComment);
            i = end;
        } else if (c == '"') {
            std::size_t end = EndOfRawString(t, i);
            if (end == std::string_view::npos) {
                end = EndOfQuoted(t, i, '"');
            }
            fill(i, end, Kind::String);
            i = end;
        } else if (c == '\'' && !IsDigitSeparator(t, i)) {
            const std::size_t end = EndOfQuoted(t, i, '\'');
            fill(i, end, Kind::Char);
            i = end;
        } else {
            ++i;
        }
    }
    return kinds;
}

struct MaskOptions {
    bool scanComments = false;
    bool scanStrings = true;
};

// Copy of `text` with every byte of a skipped kind replaced by a space. Newlines survive, so line numbers hold.
inline std::string Mask(std::string_view text, const std::vector<Kind>& kinds, MaskOptions options)
{
    std::string out(text);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Kind kind = kinds[i];
        const bool isComment = kind == Kind::LineComment || kind == Kind::BlockComment;
        const bool isLiteral = kind == Kind::String || kind == Kind::Char;
        if ((isComment && !options.scanComments) || (isLiteral && !options.scanStrings)) {
            out[i] = (out[i] == '\n' || out[i] == '\r') ? out[i] : ' ';
        }
    }
    return out;
}

struct Hit {
    std::size_t line = 0; // 1-based.
    std::string token;
};

// One token per line ('#' starts a comment): `Foo` whole identifier, `Vk*` identifier starting with Vk + uppercase,
// `*Foo*` identifier containing Foo anywhere, anything else is plain text whose identifier-character ends must sit
// on identifier boundaries. A plain-text token ending in '(' also matches with whitespace before the '('.
struct TokenSet {
    std::unordered_set<std::string> words;
    std::vector<std::string> prefixes;
    std::vector<std::string> contains;
    std::vector<std::string> phrases;
    bool ignoreCase = false;
};

inline std::string ToLower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline std::string_view Trim(std::string_view s) noexcept
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

inline TokenSet ParseTokens(std::string_view fileText, bool ignoreCase)
{
    TokenSet set;
    set.ignoreCase = ignoreCase;
    std::size_t pos = 0;
    while (pos <= fileText.size()) {
        std::size_t eol = fileText.find('\n', pos);
        eol = eol == std::string_view::npos ? fileText.size() : eol;
        std::string_view line = fileText.substr(pos, eol - pos);
        pos = eol + 1;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        const std::string token = ignoreCase ? ToLower(line) : std::string(line);
        const auto isIdentText = [](std::string_view s) {
            return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return IsIdentChar(c); });
        };
        if (token.size() > 2 && token.front() == '*' && token.back() == '*'
            && isIdentText(std::string_view(token).substr(1, token.size() - 2))) {
            set.contains.push_back(token.substr(1, token.size() - 2));
            continue;
        }
        const bool wildcard = token.size() > 1 && token.back() == '*';
        const std::string body = wildcard ? token.substr(0, token.size() - 1) : token;
        const bool allIdent = isIdentText(body);
        if (wildcard && allIdent) {
            set.prefixes.push_back(body);
        } else if (allIdent) {
            set.words.insert(body);
        } else {
            set.phrases.push_back(token);
        }
    }
    return set;
}

inline std::vector<std::size_t> LineStarts(std::string_view text)
{
    std::vector<std::size_t> starts{ 0 };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

inline std::size_t LineOf(const std::vector<std::size_t>& starts, std::size_t offset) noexcept
{
    return static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin());
}

// Every token occurrence in `masked` (already masked by Mask), in source order per token kind.
inline std::vector<Hit> FindTokens(std::string_view masked, const TokenSet& tokens)
{
    const std::string lowered = tokens.ignoreCase ? ToLower(masked) : std::string();
    const std::string_view text = tokens.ignoreCase ? std::string_view(lowered) : masked;
    const std::vector<std::size_t> starts = LineStarts(text);
    std::vector<Hit> hits;

    for (std::size_t i = 0; i < text.size();) {
        if (!IsIdentChar(text[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && IsIdentChar(text[end])) {
            ++end;
        }
        const std::string_view run = text.substr(i, end - i);
        bool matched = tokens.words.count(std::string(run)) != 0;
        for (const std::string& prefix : tokens.prefixes) {
            const bool upperNext = run.size() > prefix.size() && run.substr(0, prefix.size()) == prefix
                && std::isupper(static_cast<unsigned char>(masked[i + prefix.size()])) != 0;
            matched = matched || upperNext;
        }
        for (const std::string& part : tokens.contains) {
            matched = matched || run.find(part) != std::string_view::npos;
        }
        if (matched) {
            hits.push_back(Hit{ LineOf(starts, i), std::string(masked.substr(i, end - i)) });
        }
        i = end;
    }

    for (const std::string& phrase : tokens.phrases) {
        // A phrase ending in '(' also matches with whitespace (or a newline) before the '('.
        const bool callForm = phrase.size() > 1 && phrase.back() == '(';
        const std::string_view stem = callForm ? std::string_view(phrase).substr(0, phrase.size() - 1) : std::string_view(phrase);
        for (std::size_t at = text.find(stem); at != std::string_view::npos; at = text.find(stem, at + 1)) {
            const bool leftOk = !IsIdentChar(stem.front()) || at == 0 || !IsIdentChar(text[at - 1]);
            std::size_t after = at + stem.size();
            bool rightOk = false;
            if (callForm) {
                while (after < text.size() && (text[after] == ' ' || text[after] == '\t' || text[after] == '\r' || text[after] == '\n')) {
                    ++after;
                }
                rightOk = after < text.size() && text[after] == '(';
            } else {
                rightOk = !IsIdentChar(stem.back()) || after >= text.size() || !IsIdentChar(text[after]);
            }
            if (leftOk && rightOk) {
                hits.push_back(Hit{ LineOf(starts, at), phrase });
            }
        }
    }
    return hits;
}

// #include directives (a '#' in code) whose path contains one of the forbidden substrings.
inline std::vector<Hit> FindForbiddenIncludes(
    std::string_view text, const std::vector<Kind>& kinds, const std::vector<std::string>& forbidden)
{
    const std::vector<std::size_t> starts = LineStarts(text);
    std::vector<Hit> hits;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '#' || kinds[i] != Kind::Code) {
            continue;
        }
        std::size_t back = i;
        while (back > 0 && (text[back - 1] == ' ' || text[back - 1] == '\t')) {
            --back;
        }
        if (back > 0 && text[back - 1] != '\n') {
            continue; // Not the first thing on its line.
        }
        std::size_t p = i + 1;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
            ++p;
        }
        if (text.substr(p, 7) != "include") {
            continue;
        }
        p += 7;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
            ++p;
        }
        if (p >= text.size() || (text[p] != '"' && text[p] != '<')) {
            continue;
        }
        const char closer = text[p] == '"' ? '"' : '>';
        const std::size_t close = text.find(closer, p + 1);
        if (close == std::string_view::npos) {
            continue;
        }
        std::string path(text.substr(p + 1, close - p - 1));
        std::replace(path.begin(), path.end(), '\\', '/');
        for (const std::string& needle : forbidden) {
            if (path.find(needle) != std::string::npos) {
                hits.push_back(Hit{ LineOf(starts, i), "#include " + path });
                break;
            }
        }
    }
    return hits;
}

struct AllowEntry {
    std::string path; // Forward slashes, relative to the scan root.
    std::string reason;
    std::size_t line = 0;
};

// `path # one-line reason`. Backslashes are normalised. Returns false (with `error`) on a missing reason.
inline bool ParseAllowlist(std::string_view fileText, std::vector<AllowEntry>& out, std::string& error)
{
    std::size_t pos = 0;
    std::size_t lineNo = 0;
    while (pos <= fileText.size()) {
        std::size_t eol = fileText.find('\n', pos);
        eol = eol == std::string_view::npos ? fileText.size() : eol;
        std::string_view line = fileText.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;
        line = Trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        AllowEntry entry;
        entry.line = lineNo;
        const std::size_t hash = line.find('#');
        entry.path = std::string(Trim(line.substr(0, hash)));
        entry.reason = hash == std::string_view::npos ? std::string() : std::string(Trim(line.substr(hash + 1)));
        std::replace(entry.path.begin(), entry.path.end(), '\\', '/');
        if (entry.reason.empty()) {
            error = "allowlist line " + std::to_string(lineNo) + " (" + entry.path + ") has no reason after '#'";
            return false;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

using HitsByFile = std::map<std::string, std::vector<Hit>>;

struct RatchetResult {
    std::vector<std::string> messages;
    int failures = 0;
};

// Allowlist ratchet. Fails on: a duplicate line, a line whose file is gone, a line whose file is clean,
// and every file with hits that is not listed. `exists` answers whether a scan-root-relative path exists.
inline RatchetResult CheckAllowlist(
    const HitsByFile& found, const std::vector<AllowEntry>& allowed, const std::function<bool(const std::string&)>& exists)
{
    RatchetResult result;
    std::set<std::string> seen;
    for (const AllowEntry& entry : allowed) {
        const std::string where = " (line " + std::to_string(entry.line) + ")";
        if (!seen.insert(entry.path).second) {
            result.messages.push_back("stale line: remove it - " + entry.path + " is listed twice" + where);
            ++result.failures;
        } else if (!exists(entry.path)) {
            result.messages.push_back("stale line: remove it - " + entry.path + " no longer exists" + where);
            ++result.failures;
        } else if (found.find(entry.path) == found.end()) {
            result.messages.push_back("stale line: remove it - " + entry.path + " is clean now" + where);
            ++result.failures;
        }
    }
    for (const auto& [path, hits] : found) {
        if (seen.count(path) != 0) {
            continue;
        }
        for (const Hit& hit : hits) {
            result.messages.push_back("violation: " + path + ":" + std::to_string(hit.line) + ": " + hit.token);
        }
        ++result.failures;
    }
    return result;
}

} // namespace gte::ci
