// Implementation of src/Assets/StlLoader.h - see that file's own doc comment
// for the full public contract (binary vs. ASCII detection rule, non-finite
// hardening, non-shared-vertex convention, etc.) - this file only documents
// implementation-internal details not already covered there.
//
// Binary STL layout (see LoadStlModel()'s own doc comment in StlLoader.h for
// the detection rule that decides whether a file is parsed this way):
//   - 80-byte free-form header (never interpreted - not even checked for a
//     "solid" prefix, since a well-known real-world quirk is that some
//     genuinely-binary STL files still start their header with the text
//     "solid" for documentation purposes; see StlLoader.h's own doc comment).
//   - 4-byte little-endian uint32_t triangle count.
//   - `triangleCount` fixed 50-byte triangle records, each:
//       - 12 bytes: facet normal (3x IEEE-754 float, x/y/z).
//       - 12 bytes: vertex 0 (3x float).
//       - 12 bytes: vertex 1 (3x float).
//       - 12 bytes: vertex 2 (3x float).
//       - 2 bytes: "attribute byte count" - always ignored by this engine.
//
// ASCII STL layout: a plain-text `solid <name>` ... repeated
// `facet normal nx ny nz` / `outer loop` / `vertex x y z` (x3) / `endloop` /
// `endfacet` ... `endsolid [name]` block structure - see StlLoader.h's own
// doc comment for exactly which deviations from a "perfect" file are
// tolerated (extra whitespace, mixed line endings, mixed-case keywords,
// scientific notation) vs. rejected (wrong vertex count per facet,
// non-numeric/non-finite tokens).

#include "StlLoader.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace gte {
namespace {

constexpr std::uint64_t kBinaryHeaderSize = 80;
constexpr std::uint64_t kBinaryPreludeSize = 84; // header (80) + uint32_t triangle count (4).
constexpr std::uint64_t kBinaryTriangleRecordSize = 50;

float ReadF32LE(const std::uint8_t* bytes)
{
    float value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

Vec3 ReadVec3LE(const std::uint8_t* bytes)
{
    return Vec3(ReadF32LE(bytes), ReadF32LE(bytes + 4), ReadF32LE(bytes + 8));
}

bool IsFiniteVec3(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsDegenerateNormal(const Vec3& n)
{
    return LengthSquared(n) <= kEpsilon * kEpsilon;
}

// Resolves the normal to actually store for one triangle: the file's own
// stored normal if it is both finite and non-degenerate, otherwise a
// recomputed Normalize(Cross(v1 - v0, v2 - v0)) - see StlLoader.h's own doc
// comment ("Per-vertex normals" / "Non-finite hardening" paragraphs).
Vec3 ResolveNormal(const Vec3& stored, const Vec3& v0, const Vec3& v1, const Vec3& v2)
{
    if (IsFiniteVec3(stored) && !IsDegenerateNormal(stored)) {
        return stored;
    }
    return Normalize(Cross(v1 - v0, v2 - v0));
}

// Pushes one triangle's 3 brand-new, never-shared vertices into `mesh` -
// see PHASE0_MASTER_STRATEGY.md's Locked Design Decision 1.
void PushTriangle(MeshData& mesh, const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& normal)
{
    const std::uint32_t baseIndex = static_cast<std::uint32_t>(mesh.positions.size());

    mesh.positions.push_back(v0);
    mesh.positions.push_back(v1);
    mesh.positions.push_back(v2);

    mesh.normals.push_back(normal);
    mesh.normals.push_back(normal);
    mesh.normals.push_back(normal);

    mesh.uvs.push_back(Vec2::Zero());
    mesh.uvs.push_back(Vec2::Zero());
    mesh.uvs.push_back(Vec2::Zero());

    mesh.indices.push_back(baseIndex);
    mesh.indices.push_back(baseIndex + 1);
    mesh.indices.push_back(baseIndex + 2);
}

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

// Parses one whitespace-delimited token as a float - never throws. Rejects
// (returns false for): an empty/non-numeric token, a token that isn't fully
// consumed by the parse (trailing garbage), an out-of-range value, AND a
// syntactically-valid-but-non-finite value (e.g. the literal text "nan"/
// "inf"/"-inf", which std::strtof happily parses into a real NaN/Infinity
// float) - see StlLoader.h's own "Non-finite hardening" doc comment
// paragraph for why the last case matters.
bool TryParseFloatToken(const std::string& token, float& outValue)
{
    if (token.empty()) {
        return false;
    }

    errno = 0;
    const char* begin = token.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(begin, &end);

    if (end == begin || *end != '\0') {
        return false; // Not a (fully) numeric token.
    }
    if (errno == ERANGE) {
        return false; // Overflow/underflow.
    }
    if (!std::isfinite(parsed)) {
        return false; // e.g. literal "nan"/"inf"/"-inf" text.
    }

    outValue = parsed;
    return true;
}

// Reads exactly 3 whitespace-delimited float tokens from `iss` into `out` -
// returns false (never throws) if the stream runs out early or any token
// fails TryParseFloatToken() above.
bool ReadThreeFloatTokens(std::istringstream& iss, Vec3& out)
{
    std::string tokens[3];
    if (!(iss >> tokens[0]) || !(iss >> tokens[1]) || !(iss >> tokens[2])) {
        return false;
    }

    float values[3];
    for (int i = 0; i < 3; ++i) {
        if (!TryParseFloatToken(tokens[i], values[i])) {
            return false;
        }
    }

    out = Vec3(values[0], values[1], values[2]);
    return true;
}

// --- Binary path -------------------------------------------------------

// Attempts to parse `bytes` as a binary STL. Returns true if the binary
// size-formula (see StlLoader.h) matched - regardless of whether the parse
// that followed subsequently succeeded or failed on non-finite/corrupt
// vertex data (the caller must check outResult.success either way). Returns
// false ONLY when the size formula itself did not match, telling the caller
// to fall through and attempt the ASCII path instead.
bool TryParseBinaryStl(const std::vector<std::uint8_t>& bytes, const std::string& filePath, StlLoadResult& outResult)
{
    const std::uint64_t fileSize = static_cast<std::uint64_t>(bytes.size());
    if (fileSize < kBinaryPreludeSize) {
        return false; // Too short to even contain an 80-byte header + count field.
    }

    std::uint32_t triangleCount = 0;
    std::memcpy(&triangleCount, bytes.data() + kBinaryHeaderSize, sizeof(triangleCount));

    // 64-bit arithmetic throughout - a hostile/corrupt 32-bit count times 50
    // must never wrap/truncate silently (see PHASE0_MASTER_STRATEGY.md's
    // Risk Register).
    const std::uint64_t expectedSize = kBinaryPreludeSize
        + static_cast<std::uint64_t>(triangleCount) * kBinaryTriangleRecordSize;

    if (expectedSize != fileSize) {
        return false; // Not a valid binary STL by this engine's detection rule.
    }

    outResult.mesh.positions.reserve(static_cast<std::size_t>(triangleCount) * 3);
    outResult.mesh.normals.reserve(static_cast<std::size_t>(triangleCount) * 3);
    outResult.mesh.uvs.reserve(static_cast<std::size_t>(triangleCount) * 3);
    outResult.mesh.indices.reserve(static_cast<std::size_t>(triangleCount) * 3);

    for (std::uint32_t i = 0; i < triangleCount; ++i) {
        const std::size_t recordOffset = static_cast<std::size_t>(kBinaryPreludeSize)
            + static_cast<std::size_t>(i) * static_cast<std::size_t>(kBinaryTriangleRecordSize);
        const std::uint8_t* record = bytes.data() + recordOffset;

        const Vec3 storedNormal = ReadVec3LE(record);
        const Vec3 v0 = ReadVec3LE(record + 12);
        const Vec3 v1 = ReadVec3LE(record + 24);
        const Vec3 v2 = ReadVec3LE(record + 36);
        // Bytes [48, 50) are the "attribute byte count" - always ignored.

        if (!IsFiniteVec3(v0) || !IsFiniteVec3(v1) || !IsFiniteVec3(v2)) {
            outResult = StlLoadResult{};
            outResult.success = false;
            outResult.message = "Failed to load STL file: " + filePath
                + " (non-finite/corrupt vertex position in binary triangle record "
                + std::to_string(i) + ")";
            return true; // Binary format WAS detected; the parse itself failed.
        }

        const Vec3 normal = ResolveNormal(storedNormal, v0, v1, v2);
        PushTriangle(outResult.mesh, v0, v1, v2, normal);
    }

    outResult.success = true;
    outResult.wasBinaryFormat = true;
    outResult.message = "Loaded STL file: " + filePath + " ("
        + std::to_string(outResult.mesh.positions.size()) + " vertices, "
        + std::to_string(triangleCount) + " triangles, binary)";
    return true;
}

// --- ASCII path ----------------------------------------------------------

StlLoadResult ParseAsciiStl(const std::vector<std::uint8_t>& bytes, const std::string& filePath)
{
    StlLoadResult result;

    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::string lowerText = ToLowerAscii(text);

    const std::size_t firstNonSpace = lowerText.find_first_not_of(" \t\r\n\f\v");
    const std::string trimmed = (firstNonSpace == std::string::npos) ? std::string() : lowerText.substr(firstNonSpace);

    if (trimmed.rfind("solid", 0) != 0) {
        result.message = "Failed to load STL file: " + filePath
            + " (unrecognized/corrupt STL file - matches neither the binary size formula nor ASCII text starting with \"solid\")";
        return result;
    }

    // Cheap up-front reserve-size estimate: count non-overlapping "facet"
    // occurrences (see PHASE0_MASTER_STRATEGY.md's Risk Register). Does not
    // need to be exact, just a reasonable upper bound.
    std::size_t estimatedTriangleCount = 0;
    {
        std::size_t pos = 0;
        while ((pos = lowerText.find("facet", pos)) != std::string::npos) {
            ++estimatedTriangleCount;
            pos += 5; // length of "facet"
        }
    }

    result.mesh.positions.reserve(estimatedTriangleCount * 3);
    result.mesh.normals.reserve(estimatedTriangleCount * 3);
    result.mesh.uvs.reserve(estimatedTriangleCount * 3);
    result.mesh.indices.reserve(estimatedTriangleCount * 3);

    std::istringstream iss(lowerText);
    std::string token;

    while (iss >> token) {
        if (token == "endsolid") {
            break; // Stop parsing entirely, even if more bytes remain (only the first solid is imported).
        }

        if (token != "facet") {
            continue; // Ignore the "solid <name>" tokens and anything else at the top level.
        }

        std::string normalKeyword;
        if (!(iss >> normalKeyword) || normalKeyword != "normal") {
            result.mesh = MeshData{};
            result.message = "Failed to load STL file: " + filePath
                + " (malformed ASCII STL: expected \"normal\" after \"facet\")";
            return result;
        }

        Vec3 storedNormal;
        if (!ReadThreeFloatTokens(iss, storedNormal)) {
            result.mesh = MeshData{};
            result.message = "Failed to load STL file: " + filePath
                + " (malformed ASCII STL: expected 3 numeric tokens after \"facet normal\")";
            return result;
        }

        Vec3 verts[3];
        int vertexCount = 0;
        bool sawEndFacet = false;

        std::string innerToken;
        while (iss >> innerToken) {
            if (innerToken == "vertex") {
                if (vertexCount >= 3) {
                    result.mesh = MeshData{};
                    result.message = "Failed to load STL file: " + filePath
                        + " (malformed ASCII STL: facet has more than 3 \"vertex\" entries)";
                    return result;
                }

                Vec3 v;
                if (!ReadThreeFloatTokens(iss, v)) {
                    result.mesh = MeshData{};
                    result.message = "Failed to load STL file: " + filePath
                        + " (malformed ASCII STL: expected 3 numeric tokens after \"vertex\")";
                    return result;
                }
                verts[vertexCount++] = v;
            } else if (innerToken == "endfacet") {
                if (vertexCount != 3) {
                    result.mesh = MeshData{};
                    result.message = "Failed to load STL file: " + filePath
                        + " (malformed ASCII STL: facet has fewer than 3 \"vertex\" entries)";
                    return result;
                }
                sawEndFacet = true;
                break;
            } else if (innerToken == "outer" || innerToken == "loop" || innerToken == "endloop") {
                continue; // No data of their own - skip/ignore.
            } else {
                result.mesh = MeshData{};
                result.message = "Failed to load STL file: " + filePath
                    + " (malformed ASCII STL: unexpected token \"" + innerToken + "\" inside a facet block)";
                return result;
            }
        }

        if (!sawEndFacet) {
            result.mesh = MeshData{};
            result.message = "Failed to load STL file: " + filePath
                + " (malformed ASCII STL: truncated facet block, missing \"endfacet\")";
            return result;
        }

        const Vec3 normal = ResolveNormal(storedNormal, verts[0], verts[1], verts[2]);
        PushTriangle(result.mesh, verts[0], verts[1], verts[2], normal);
    }

    result.success = true;
    result.wasBinaryFormat = false;
    result.message = "Loaded STL file: " + filePath + " ("
        + std::to_string(result.mesh.positions.size()) + " vertices, "
        + std::to_string(result.mesh.indices.size() / 3) + " triangles, ASCII)";
    return result;
}

} // namespace

StlLoadResult LoadStlModel(const std::string& filePath)
{
    StlLoadResult result;

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        result.message = "Failed to open STL file: " + filePath;
        return result;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff sizeOff = file.tellg();
    if (sizeOff < 0) {
        result.message = "Failed to determine the size of STL file: " + filePath;
        return result;
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sizeOff));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            result.message = "Failed to read STL file: " + filePath;
            return result;
        }
    }

    StlLoadResult binaryAttempt;
    if (TryParseBinaryStl(bytes, filePath, binaryAttempt)) {
        return binaryAttempt;
    }

    return ParseAsciiStl(bytes, filePath);
}

} // namespace gte
