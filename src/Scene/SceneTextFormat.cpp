#include "SceneTextFormat.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace gte {

namespace {

std::string Trim(const std::string& s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Splits `text` into lines, tolerating both "\n" and "\r\n" endings (a
// hand-edited file on Windows may use either) - a trailing '\r' left over
// from a "\r\n" ending is stripped defensively from every line.
std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty() && current.back() == '\r') {
        current.pop_back();
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::vector<std::string> SplitCsv(const std::string& value)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : value) {
        if (c == ',') {
            tokens.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    tokens.push_back(current);
    return tokens;
}

bool ParseFloatToken(const std::string& token, float& outValue)
{
    std::string trimmed = Trim(token);
    if (trimmed.empty()) {
        return false;
    }
    char* endPtr = nullptr;
    float value = std::strtof(trimmed.c_str(), &endPtr);
    if (endPtr == trimmed.c_str() || *endPtr != '\0') {
        return false;
    }
    outValue = value;
    return true;
}

// Reused for all three CSV vector/quat fields (position/scale use 3
// components via Vec3, rotation uses 4 via Quat) - fails on the wrong token
// count or any token that doesn't fully parse as a float (including
// trailing garbage after the number).
bool ParseFloatCsv(const std::string& value, int expectedCount, float* out)
{
    std::vector<std::string> tokens = SplitCsv(value);
    if (static_cast<int>(tokens.size()) != expectedCount) {
        return false;
    }
    for (int i = 0; i < expectedCount; ++i) {
        if (!ParseFloatToken(tokens[static_cast<std::size_t>(i)], out[i])) {
            return false;
        }
    }
    return true;
}

bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// A pure SYNTAX check (exactly 32 hex characters) - deliberately separate
// from Guid::Parse() itself (which never fails outright and instead
// degrades to Guid::Invalid() for anything malformed per its own doc
// comment), since this format must distinguish "syntactically garbage" (an
// immediate parse failure regardless of `kind`) from "syntactically valid
// but semantically Guid::Invalid()" (only rejected for kind == Asset, at
// END-time - see SceneTextFormat.h's own doc comment).
bool IsSyntacticallyValidGuidString(const std::string& text)
{
    if (text.size() != 32) {
        return false;
    }
    for (char c : text) {
        if (!IsHexDigit(c)) {
            return false;
        }
    }
    return true;
}

bool ParsePrimitiveTypeToken(const std::string& value, PrimitiveType& out)
{
    static const PrimitiveType kAllTypes[] = {
        PrimitiveType::Cube,
        PrimitiveType::Sphere,
        PrimitiveType::Capsule,
        PrimitiveType::Cone,
        PrimitiveType::Plane,
    };
    for (PrimitiveType type : kAllTypes) {
        if (value == ToString(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

} // namespace

std::string SerializeSceneDocument(const SceneDocument& document)
{
    std::string text;
    text += "GTSCENE " + std::to_string(kSceneTextFormatVersion) + "\n";

    char buffer[256];
    for (const SceneObjectRecord& record : document.objects) {
        text += "OBJECT\n";
        text += "kind=";
        text += (record.kind == SceneObjectKind::Primitive ? "Primitive" : "Asset");
        text += "\n";

        if (!record.name.empty()) {
            text += "name=" + record.name + "\n";
        }

        if (record.kind == SceneObjectKind::Primitive) {
            text += "primitiveType=";
            text += ToString(record.primitiveType);
            text += "\n";
        }

        if (record.kind == SceneObjectKind::Asset) {
            text += "assetGuid=" + record.assetGuid.ToString() + "\n";
        }

        std::snprintf(buffer, sizeof(buffer), "position=%.6f,%.6f,%.6f\n", record.position.x, record.position.y,
            record.position.z);
        text += buffer;

        std::snprintf(buffer, sizeof(buffer), "rotation=%.6f,%.6f,%.6f,%.6f\n", record.rotation.x, record.rotation.y,
            record.rotation.z, record.rotation.w);
        text += buffer;

        std::snprintf(buffer, sizeof(buffer), "scale=%.6f,%.6f,%.6f\n", record.scale.x, record.scale.y,
            record.scale.z);
        text += buffer;

        text += "END\n";
    }

    return text;
}

std::optional<SceneDocument> DeserializeSceneDocument(const std::string& text)
{
    std::vector<std::string> rawLines = SplitLines(text);

    std::vector<std::string> lines;
    lines.reserve(rawLines.size());
    for (const std::string& rawLine : rawLines) {
        std::string trimmed = Trim(rawLine);
        if (!trimmed.empty()) {
            lines.push_back(trimmed);
        }
    }

    if (lines.empty()) {
        return std::nullopt;
    }

    const std::string expectedHeader = "GTSCENE " + std::to_string(kSceneTextFormatVersion);
    if (lines[0] != expectedHeader) {
        return std::nullopt;
    }

    SceneDocument document;
    bool insideObject = false;
    SceneObjectRecord current;

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];

        if (line == "OBJECT") {
            if (insideObject) {
                return std::nullopt;
            }
            current = SceneObjectRecord{};
            insideObject = true;
            continue;
        }

        if (line == "END") {
            if (!insideObject) {
                return std::nullopt;
            }
            // v2 cross-field check - see SceneTextFormat.h's own doc
            // comment for exactly why this is scoped to kind == Asset only,
            // checked here (at object-close time) rather than per-line.
            if (current.kind == SceneObjectKind::Asset && !current.assetGuid.IsValid()) {
                return std::nullopt;
            }
            document.objects.push_back(current);
            insideObject = false;
            continue;
        }

        if (!insideObject) {
            // A stray key=value (or anything else) outside any OBJECT/END
            // block is malformed.
            return std::nullopt;
        }

        std::size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            return std::nullopt;
        }
        std::string key = line.substr(0, equalsPos);
        std::string value = line.substr(equalsPos + 1);

        if (key == "kind") {
            if (value == "Primitive") {
                current.kind = SceneObjectKind::Primitive;
            } else if (value == "Asset") {
                current.kind = SceneObjectKind::Asset;
            } else {
                return std::nullopt;
            }
        } else if (key == "name") {
            current.name = value;
        } else if (key == "primitiveType") {
            PrimitiveType parsed = PrimitiveType::Cube;
            if (!ParsePrimitiveTypeToken(value, parsed)) {
                return std::nullopt;
            }
            current.primitiveType = parsed;
        } else if (key == "assetGuid") {
            // Syntax check FIRST (see IsSyntacticallyValidGuidString's own
            // comment) - a syntactically malformed value fails immediately,
            // regardless of `kind`; only a syntactically valid but
            // semantically Guid::Invalid() value is deferred to the
            // END-time, kind==Asset-only check above.
            if (!IsSyntacticallyValidGuidString(value)) {
                return std::nullopt;
            }
            current.assetGuid = Guid::Parse(value);
        } else if (key == "position") {
            float values[3];
            if (!ParseFloatCsv(value, 3, values)) {
                return std::nullopt;
            }
            current.position = Vec3(values[0], values[1], values[2]);
        } else if (key == "rotation") {
            float values[4];
            if (!ParseFloatCsv(value, 4, values)) {
                return std::nullopt;
            }
            current.rotation = Quat(values[0], values[1], values[2], values[3]);
        } else if (key == "scale") {
            float values[3];
            if (!ParseFloatCsv(value, 3, values)) {
                return std::nullopt;
            }
            current.scale = Vec3(values[0], values[1], values[2]);
        }
        // Any other, unrecognized key is silently ignored - forward
        // compatibility, per this format's own spec (see SceneTextFormat.h).
    }

    if (insideObject) {
        // The last "OBJECT" was never closed with a matching "END".
        return std::nullopt;
    }

    return document;
}

} // namespace gte
