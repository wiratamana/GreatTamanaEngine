#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gte {

// The kind of binary payload a *.gta file (see GtaFile.h) actually carries -
// stored as a plain uint64_t in GtaHeader::assetType (see AGENTS.md-style
// "identify by a stable value, never a string" convention already used for
// GpuResourceHandle/Entity elsewhere in this engine). Explicit numeric
// values are pinned on purpose: this enum is serialized straight into a
// binary file format, so a value must never change meaning/renumber once
// shipped - only ever APPEND a new one.
enum class AssetType : std::uint64_t {
    Unknown = 0,
    Texture = 1, // Imported PNG/JPEG/etc, eventually wrapped as KTX2 (see README.md's "future notes").
    Mesh = 2, // Imported 3D model geometry (OBJ/glTF/... - see TODO.md's asset pipeline roadmap).
    Material = 3,
    Shader = 4,
    Audio = 5,
    Scene = 6, // A serialized Registry/World - see TODO.md's "Scene serialization" roadmap item.
    Text = 7, // Deliberately unimplemented for now (see README.md: "text file can stay still for now").
    Font = 8,
    Animation = 9,
    Prefab = 10,
    Other = 1000, // Escape hatch for anything not covered above yet.
};

// A bitmask stored in GtaHeader::flags (uint64_t) - same "explicit, never
// renumbered" rule as AssetType above, since this is also serialized
// straight into the binary format.
enum class AssetFlags : std::uint64_t {
    None = 0,
    Compressed = 1ull << 0,
    Encrypted = 1ull << 1,
};

constexpr AssetFlags operator|(AssetFlags a, AssetFlags b) noexcept
{
    return static_cast<AssetFlags>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}

constexpr AssetFlags operator&(AssetFlags a, AssetFlags b) noexcept
{
    return static_cast<AssetFlags>(static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}

constexpr bool HasFlag(AssetFlags value, AssetFlags flag) noexcept
{
    return (value & flag) == flag && flag != AssetFlags::None;
}

// A stable 128-bit identifier for one tracked asset, stored directly inside
// its *.gta file's own header (GtaHeader::guidLow/guidHigh) rather than in a
// separate sidecar ".meta" file the way Unity's AssetDatabase does it - so
// there is nothing that can ever desync from the asset it identifies (the
// id travels inside the same file it names). Plain POD, copied/compared/
// hashed by value like every other handle type in this engine (Entity,
// GpuResourceHandle) - never referenced by pointer.
struct Guid {
    std::uint64_t low = 0;
    std::uint64_t high = 0;

    bool IsValid() const noexcept { return low != 0 || high != 0; }

    friend bool operator==(const Guid& a, const Guid& b) noexcept { return a.low == b.low && a.high == b.high; }
    friend bool operator!=(const Guid& a, const Guid& b) noexcept { return !(a == b); }

    // All-zero is the one reserved "no asset"/"never assigned" value -
    // Generate() below never produces it (see AssetTypes.cpp).
    static Guid Invalid() noexcept { return Guid{}; }

    // Produces a fresh, practically-unique Guid (a process-local
    // std::mt19937_64, NOT cryptographically secure - good enough for an
    // asset id, same spirit as this engine's other generational handles).
    static Guid Generate();

    // Lowercase, 32 hex digits, no separators - e.g.
    // "0123456789abcdef0123456789abcdef". Deterministic, round-trips
    // through Parse() below.
    std::string ToString() const;

    // Parses ToString()'s exact format back into a Guid. Returns
    // Guid::Invalid() (and never throws) for anything malformed - callers
    // that need to distinguish "parsed as all-zero" from "failed to parse"
    // should validate the input length/hex-ness themselves first.
    static Guid Parse(const std::string& text);
};

// Best-effort AssetType guess from a lowercase, dot-prefixed file extension
// (e.g. ".png") - the same convention AssetInspectorData.h's
// IsSupportedImageExtension() already uses. Purely a classification helper
// for a future import pipeline (see TODO.md) to decide what an
// AssetDatabase::ImportRawFile() call should tag a freshly-wrapped *.gta
// with; never guesses AssetType::Scene/Prefab (those are only ever produced
// by the engine itself, not inferred from an imported file's extension).
AssetType AssetTypeFromExtension(const std::string& extensionLowercaseWithDot);

} // namespace gte

namespace std {
template <> struct hash<gte::Guid> {
    std::size_t operator()(const gte::Guid& guid) const noexcept
    {
        // Simple, deterministic 64-bit mix - good enough for an
        // unordered_map key; this is not used for anything
        // security-sensitive.
        std::size_t h1 = std::hash<std::uint64_t>{}(guid.low);
        std::size_t h2 = std::hash<std::uint64_t>{}(guid.high);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace std
