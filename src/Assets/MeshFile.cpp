#include "MeshFile.h"

#include "Crc32.h"
#include "MeshClusterValidation.h"

#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace gte {

static_assert(sizeof(Vec4) == 16 && std::is_trivially_copyable_v<Vec4>);

namespace {

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template <typename T>
void AppendArray(std::vector<std::uint8_t>& bytes, const std::vector<T>& values)
{
    if (values.empty()) {
        return;
    }
    const std::size_t offset = bytes.size();
    const std::size_t byteSize = values.size() * sizeof(T);
    bytes.resize(offset + byteSize);
    std::memcpy(bytes.data() + offset, values.data(), byteSize);
}

// Reads `count` T's at `*cursor` and advances it; false (nothing touched) when the range does not fit.
template <typename T>
bool ReadArray(const std::vector<std::uint8_t>& bytes, std::size_t* cursor, std::size_t count, std::vector<T>* out)
{
    const std::size_t remaining = bytes.size() - *cursor;
    if (count > remaining / sizeof(T)) {
        return false;
    }
    const std::size_t byteSize = count * sizeof(T);
    out->resize(count);
    if (byteSize > 0) {
        std::memcpy(out->data(), bytes.data() + *cursor, byteSize);
    }
    *cursor += byteSize;
    return true;
}

bool IndicesInRange(const std::vector<std::uint32_t>& indices, std::size_t vertexCount)
{
    for (const std::uint32_t index : indices) {
        if (index >= vertexCount) {
            return false;
        }
    }
    return true;
}

bool MeshIsEncodable(const MeshData& mesh)
{
    return MeshCountsFitUint32(mesh.positions.size(), mesh.indices.size(), mesh.clusters.size())
        && mesh.normals.size() == mesh.positions.size()
        && mesh.uvs.size() == mesh.positions.size()
        && (mesh.tangents.empty() || mesh.tangents.size() == mesh.positions.size())
        && IndicesInRange(mesh.indices, mesh.positions.size())
        && ValidateMeshClusters(mesh, nullptr);
}

// Parses the cluster tail at `cursor` (which points at the magic). Fills mesh.clusters on success,
// else leaves it empty and sets `why`.
bool ParseClusterTail(const std::vector<std::uint8_t>& bytes, std::size_t cursor, MeshData& mesh, std::string& why)
{
    constexpr std::size_t kMagicSize = sizeof(kMeshClusterTailMagic);
    constexpr std::size_t kCountSize = sizeof(std::uint32_t);
    constexpr std::size_t kCrcSize = sizeof(std::uint32_t);

    const std::size_t remaining = bytes.size() - cursor;
    if (remaining < kMagicSize + kCountSize) {
        why = "cluster tail is truncated";
        return false;
    }

    std::uint32_t clusterCount = 0;
    std::memcpy(&clusterCount, bytes.data() + cursor + kMagicSize, kCountSize);
    if (clusterCount == 0) {
        why = "cluster count is 0";
        return false;
    }

    const std::uint64_t needed = static_cast<std::uint64_t>(clusterCount) * sizeof(MeshCluster) + kCrcSize;
    const std::uint64_t available = remaining - kMagicSize - kCountSize;
    if (needed > available) {
        why = "cluster tail is shorter than its cluster count";
        return false;
    }
    if (needed < available) {
        why = "trailing bytes after the cluster table";
        return false;
    }

    const std::size_t crcBodySize = kCountSize + static_cast<std::size_t>(clusterCount) * sizeof(MeshCluster);
    const std::uint8_t* crcBody = bytes.data() + cursor + kMagicSize;
    std::uint32_t storedCrc = 0;
    std::memcpy(&storedCrc, crcBody + crcBodySize, kCrcSize);
    if (Crc32(std::as_bytes(std::span<const std::uint8_t>(crcBody, crcBodySize))) != storedCrc) {
        why = "cluster table checksum mismatch";
        return false;
    }

    mesh.clusters.resize(clusterCount);
    std::memcpy(mesh.clusters.data(), crcBody + kCountSize, static_cast<std::size_t>(clusterCount) * sizeof(MeshCluster));
    if (!ValidateMeshClusters(mesh, &why)) {
        mesh.clusters.clear();
        mesh.clusters.shrink_to_fit();
        return false;
    }
    return true;
}

bool HasClusterTailMagic(const std::vector<std::uint8_t>& bytes, std::size_t cursor)
{
    return bytes.size() - cursor >= sizeof(kMeshClusterTailMagic)
        && std::memcmp(bytes.data() + cursor, kMeshClusterTailMagic, sizeof(kMeshClusterTailMagic)) == 0;
}

} // namespace

bool MeshCountsFitUint32(std::size_t vertexCount, std::size_t indexCount, std::size_t clusterCount)
{
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint64_t>(vertexCount) <= kMax && static_cast<std::uint64_t>(indexCount) <= kMax
        && static_cast<std::uint64_t>(clusterCount) <= kMax;
}

std::vector<std::uint8_t> EncodeMeshDataToBytes(const MeshData& mesh)
{
    if (!MeshIsEncodable(mesh)) {
        return {};
    }

    const bool writeTangents = !mesh.tangents.empty();
    const bool writeClusters = !mesh.clusters.empty();

    std::vector<std::uint8_t> bytes;
    bytes.reserve(sizeof(kMeshFileMagic) + sizeof(std::uint32_t) * 2
        + mesh.positions.size() * sizeof(Vec3) + mesh.normals.size() * sizeof(Vec3)
        + mesh.uvs.size() * sizeof(Vec2) + mesh.indices.size() * sizeof(std::uint32_t)
        + (writeTangents ? mesh.tangents.size() * sizeof(Vec4) + sizeof(std::uint32_t) : 0)
        + (writeClusters ? sizeof(kMeshClusterTailMagic) + sizeof(std::uint32_t) * 2 + mesh.clusters.size() * sizeof(MeshCluster) : 0));

    bytes.insert(bytes.end(), kMeshFileMagic, kMeshFileMagic + sizeof(kMeshFileMagic));
    AppendU32(bytes, static_cast<std::uint32_t>(mesh.positions.size()));
    AppendU32(bytes, static_cast<std::uint32_t>(mesh.indices.size()));

    AppendArray(bytes, mesh.positions);
    AppendArray(bytes, mesh.normals);
    AppendArray(bytes, mesh.uvs);
    AppendArray(bytes, mesh.indices);
    if (writeTangents) {
        AppendU32(bytes, static_cast<std::uint32_t>(mesh.tangents.size()));
        AppendArray(bytes, mesh.tangents);
    }
    if (writeClusters) {
        bytes.insert(bytes.end(), kMeshClusterTailMagic, kMeshClusterTailMagic + sizeof(kMeshClusterTailMagic));
        const std::size_t crcStart = bytes.size();
        AppendU32(bytes, static_cast<std::uint32_t>(mesh.clusters.size()));
        AppendArray(bytes, mesh.clusters);
        const std::uint32_t crc = Crc32(std::as_bytes(std::span<const std::uint8_t>(bytes.data() + crcStart, bytes.size() - crcStart)));
        AppendU32(bytes, crc);
    }

    return bytes;
}

std::optional<MeshData> DecodeMeshDataFromBytes(const std::vector<std::uint8_t>& bytes, std::string* clusterTailRejection)
{
    if (clusterTailRejection != nullptr) {
        clusterTailRejection->clear();
    }
    constexpr std::size_t kHeaderSize = sizeof(kMeshFileMagic) + sizeof(std::uint32_t) * 2;
    if (bytes.size() < kHeaderSize) {
        return std::nullopt;
    }
    if (std::memcmp(bytes.data(), kMeshFileMagic, sizeof(kMeshFileMagic)) != 0) {
        return std::nullopt;
    }

    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::memcpy(&vertexCount, bytes.data() + sizeof(kMeshFileMagic), sizeof(vertexCount));
    std::memcpy(&indexCount, bytes.data() + sizeof(kMeshFileMagic) + sizeof(vertexCount), sizeof(indexCount));

    std::size_t cursor = kHeaderSize;
    MeshData mesh;
    if (!ReadArray(bytes, &cursor, vertexCount, &mesh.positions)) {
        return std::nullopt;
    }
    if (!ReadArray(bytes, &cursor, vertexCount, &mesh.normals)) {
        return std::nullopt;
    }
    if (!ReadArray(bytes, &cursor, vertexCount, &mesh.uvs)) {
        return std::nullopt;
    }
    if (!ReadArray(bytes, &cursor, indexCount, &mesh.indices)) {
        return std::nullopt;
    }

    if (cursor != bytes.size() && !HasClusterTailMagic(bytes, cursor)) {
        if (bytes.size() - cursor < sizeof(std::uint32_t)) {
            return std::nullopt;
        }
        std::uint32_t tangentCount = 0;
        std::memcpy(&tangentCount, bytes.data() + cursor, sizeof(tangentCount));
        cursor += sizeof(tangentCount);
        if (tangentCount != vertexCount || tangentCount == 0) {
            return std::nullopt;
        }
        if (!ReadArray(bytes, &cursor, tangentCount, &mesh.tangents)) {
            return std::nullopt;
        }
        if (cursor != bytes.size() && !HasClusterTailMagic(bytes, cursor)) {
            return std::nullopt;
        }
    }

    if (cursor != bytes.size()) {
        std::string why;
        if (!ParseClusterTail(bytes, cursor, mesh, why) && clusterTailRejection != nullptr) {
            *clusterTailRejection = std::move(why);
        }
    }

    return mesh;
}

} // namespace gte
