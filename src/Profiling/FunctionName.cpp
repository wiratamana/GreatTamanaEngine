#include "FunctionName.h"

#include <algorithm>
#include <cstring>

namespace gte::Profiling {
namespace {

constexpr std::string_view kOperatorKeyword = "operator";
constexpr std::string_view kWithSuffix = " [with ";
constexpr std::string_view kLambdaSuffix = "::<lambda";
constexpr std::string_view kLambdaName = "::<lambda>";
constexpr std::string_view kOperatorSymbols = "<>=!+-*/%&|^~,[]";

bool IsIdentifierChar(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool IsOperatorKeywordAt(std::string_view text, std::size_t i) noexcept
{
    const std::size_t end = i + kOperatorKeyword.size();
    return text.compare(i, kOperatorKeyword.size(), kOperatorKeyword) == 0
        && (i == 0 || !IsIdentifierChar(text[i - 1]))
        && (end >= text.size() || !IsIdentifierChar(text[end]));
}

// Index just after the symbol of an operator name ("==", "()", "<<"), so its characters are not parsed.
std::size_t SkipOperatorSymbol(std::string_view text, std::size_t i) noexcept
{
    if (text.compare(i, 2, "()") == 0) {
        return i + 2;
    }
    while (i < text.size() && kOperatorSymbols.find(text[i]) != std::string_view::npos) {
        ++i;
    }
    return i;
}

// Index just after the ')' that closes the '(' at `open`; text.size() when unbalanced.
std::size_t SkipArguments(std::string_view text, std::size_t open) noexcept
{
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if (text[i] == ')' && --depth == 0) {
            return i + 1;
        }
    }
    return text.size();
}

} // namespace

std::size_t TrimFunctionName(std::string_view signature, std::span<char> out) noexcept
{
    if (out.empty()) {
        return 0;
    }

    if (const std::size_t with = signature.rfind(kWithSuffix); with != std::string_view::npos) {
        signature = signature.substr(0, with);
    }

    // Find the first '(' outside template brackets and the start of the name after the return type.
    int angle = 0;
    std::size_t nameStart = 0;
    std::size_t open = std::string_view::npos;
    for (std::size_t i = 0; i < signature.size();) {
        const char c = signature[i];
        if (c == 'o' && IsOperatorKeywordAt(signature, i)) {
            i = SkipOperatorSymbol(signature, i + kOperatorKeyword.size());
            continue;
        }
        if (c == '<') {
            ++angle;
        } else if (c == '>') {
            if (angle > 0 && !(i > 0 && signature[i - 1] == '-')) {
                --angle;
            }
        } else if (c == ' ' && angle == 0) {
            nameStart = i + 1;
        } else if (c == '(' && angle == 0) {
            open = i;
            break;
        }
        ++i;
    }

    std::string_view name;
    bool lambda = false;
    if (open == std::string_view::npos) {
        name = signature;
    } else {
        name = signature.substr(nameStart, open - nameStart);
        const std::string_view rest = signature.substr(SkipArguments(signature, open));
        lambda = rest.starts_with(kLambdaSuffix);
    }

    const std::size_t capacity = out.size() - 1;
    std::size_t written = std::min(name.size(), capacity);
    std::memcpy(out.data(), name.data(), written);
    if (lambda && !name.empty()) {
        const std::size_t extra = std::min(kLambdaName.size(), capacity - written);
        std::memcpy(out.data() + written, kLambdaName.data(), extra);
        written += extra;
    }
    out[written] = '\0';
    return written;
}

} // namespace gte::Profiling
