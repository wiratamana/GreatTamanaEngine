#include "ProjectAssemblyNameValidation.h"

#include <array>
#include <cctype>
#include <algorithm>

namespace gte {

namespace {

bool IsAsciiLetterOrUnderscore(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsAsciiAlnumOrUnderscore(char c)
{
    return IsAsciiLetterOrUnderscore(c) || (c >= '0' && c <= '9');
}

bool IsReservedWindowsDeviceName(const std::string& name)
{
    // Case-insensitive exact match against the fixed list - COM1-COM9/
    // LPT1-LPT9 checked via a shared prefix+digit test rather than 18
    // separate literals.
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });

    static const std::array<const char*, 4> kFixedNames = { "CON", "PRN", "AUX", "NUL" };
    for (const char* fixedName : kFixedNames) {
        if (upper == fixedName) {
            return true;
        }
    }
    if (upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0)) {
        const char lastChar = upper.back();
        if (lastChar >= '1' && lastChar <= '9') {
            return true;
        }
    }
    return false;
}

} // namespace

bool IsValidProjectAssemblyIdentifierName(const std::string& name, std::string& outErrorMessage)
{
    if (name.empty()) {
        outErrorMessage = "name must not be empty";
        return false;
    }
    if (!IsAsciiLetterOrUnderscore(name.front())) {
        outErrorMessage = "name must start with a letter or underscore";
        return false;
    }
    for (char c : name) {
        if (!IsAsciiAlnumOrUnderscore(c)) {
            outErrorMessage = "name may only contain letters, digits, and underscores (no spaces, '.', '/', '\\')";
            return false;
        }
    }
    if (IsReservedWindowsDeviceName(name)) {
        outErrorMessage = "'" + name + "' is a reserved Windows device name and cannot be used";
        return false;
    }
    return true;
}

} // namespace gte
