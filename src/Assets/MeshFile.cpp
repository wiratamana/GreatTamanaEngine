#include "MeshFile.h"

#include <cstring>

namespace gte {

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

// Reads `count` T's out of `bytes` starting at `*cursor`, advancing `*cursor`
// past them - returns false (leaving `*cursor`/`out` untouched) if that
// range would run past `bytes.size()`, so a truncated/corrupt blob is caught
// at the exact array that doesn't fit rather than reading past the buffer.
template <typename T>
bool ReadArray(const std::vector<std::uint8_t>& bytes, std::size_t* cursor, std::size_t count, std::vector<T>* out)
{
    const std::size_t byteSize = count * sizeof(T);
    if (*cursor + byteSize > bytes.size()) {
        return false;
    }
    out->resize(count);
    if (byteSize > 0) {
        std::memcpy(out->data(), bytes.data() + *cursor, byteSize);
    }
    *cursor += byteSize;
    return true;
}

} // namespace

std::vector<std::uint8_t> EncodeMeshDataToBytes(const MeshData& mesh)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(sizeof(kMeshFileMagic) + sizeof(std::uint32_t) * 2
        + mesh.positions.size() * sizeof(Vec3) + mesh.normals.size() * sizeof(Vec3)
        + mesh.uvs.size() * sizeof(Vec2) + mesh.indices.size() * sizeof(std::uint32_t));

    bytes.insert(bytes.end(), kMeshFileMagic, kMeshFileMagic + sizeof(kMeshFileMagic));
    AppendU32(bytes, static_cast<std::uint32_t>(mesh.positions.size()));
    AppendU32(bytes, static_cast<std::uint32_t>(mesh.indices.size()));

    AppendArray(bytes, mesh.positions);
    AppendArray(bytes, mesh.normals);
    AppendArray(bytes, mesh.uvs);
    AppendArray(bytes, mesh.indices);

    return bytes;
}

std::optional<MeshData> DecodeMeshDataFromBytes(const std::vector<std::uint8_t>& bytes)
{
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

    return mesh;
}

} // namespace gte
