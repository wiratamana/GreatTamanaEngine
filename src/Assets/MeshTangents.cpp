#include "MeshTangents.h"

#include <mikktspace.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gte {

namespace {

// Corners whose tangents differ by less than ~0.8 degrees share one vertex.
constexpr float kMergeDot = 0.9999f;
constexpr float kMinLengthSquared = 1e-12f;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Unit copy of `n`, or zero when `n` is zero or not finite.
Vec3 UnitOrZero(const Vec3& n) noexcept
{
    if (!IsFinite(n)) {
        return Vec3::Zero();
    }
    const float lengthSquared = LengthSquared(n);
    return lengthSquared > kMinLengthSquared ? n / std::sqrt(lengthSquared) : Vec3::Zero();
}

// `t` made perpendicular to the unit (or zero) `n` and normalized; false when nothing usable is left.
bool PerpendicularUnit(const Vec3& t, const Vec3& n, Vec3* out) noexcept
{
    if (!IsFinite(t)) {
        return false;
    }
    const Vec3 r = t - n * Dot(n, t);
    const float lengthSquared = LengthSquared(r);
    if (!(lengthSquared > kMinLengthSquared) || !std::isfinite(lengthSquared)) {
        return false;
    }
    *out = r / std::sqrt(lengthSquared);
    return true;
}

Vec3 Finite(const Vec3& v) noexcept
{
    return IsFinite(v) ? v : Vec3::Zero();
}

// One triangle of the face list: its three vertex indices and where its corners sit in MeshData::indices.
struct Face {
    std::array<std::uint32_t, 3> vertex{};
    std::size_t firstIndex = 0;
};

struct MikkInput {
    const MeshData* mesh = nullptr;
    const std::vector<Face>* faces = nullptr;
    std::vector<Vec3>* cornerTangents = nullptr; // 3 per face
};

const MikkInput& Input(const SMikkTSpaceContext* context) noexcept
{
    return *static_cast<const MikkInput*>(context->m_pUserData);
}

std::uint32_t CornerVertex(const SMikkTSpaceContext* context, int face, int corner) noexcept
{
    return (*Input(context).faces)[static_cast<std::size_t>(face)].vertex[static_cast<std::size_t>(corner)];
}

int MikkNumFaces(const SMikkTSpaceContext* context)
{
    return static_cast<int>(Input(context).faces->size());
}

int MikkNumVerticesOfFace(const SMikkTSpaceContext*, int)
{
    return 3;
}

void MikkPosition(const SMikkTSpaceContext* context, float out[], int face, int corner)
{
    const Vec3 p = Finite(Input(context).mesh->positions[CornerVertex(context, face, corner)]);
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void MikkNormal(const SMikkTSpaceContext* context, float out[], int face, int corner)
{
    Vec3 n = UnitOrZero(Input(context).mesh->normals[CornerVertex(context, face, corner)]);
    if (n == Vec3::Zero()) {
        n = Vec3::Up();
    }
    out[0] = n.x;
    out[1] = n.y;
    out[2] = n.z;
}

// MikkTSpace expects v pointing up; the mesh stores v pointing down.
void MikkTexCoord(const SMikkTSpaceContext* context, float out[], int face, int corner)
{
    const Vec2 uv = Input(context).mesh->uvs[CornerVertex(context, face, corner)];
    out[0] = std::isfinite(uv.x) ? uv.x : 0.0f;
    out[1] = std::isfinite(uv.y) ? 1.0f - uv.y : 0.0f;
}

void MikkSetTangent(const SMikkTSpaceContext* context, const float tangent[], float, int face, int corner)
{
    (*Input(context).cornerTangents)[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner)]
        = Vec3(tangent[0], tangent[1], tangent[2]);
}

// "Up on the image" direction of a triangle: -dP/dv. False when the uv mapping has no usable gradient.
bool FaceUpDirection(const MeshData& mesh, const Face& face, Vec3* out) noexcept
{
    const Vec3 p0 = mesh.positions[face.vertex[0]];
    const Vec3 e1 = mesh.positions[face.vertex[1]] - p0;
    const Vec3 e2 = mesh.positions[face.vertex[2]] - p0;
    const Vec2 uv0 = mesh.uvs[face.vertex[0]];
    const double du1 = static_cast<double>(mesh.uvs[face.vertex[1]].x) - uv0.x;
    const double dv1 = static_cast<double>(mesh.uvs[face.vertex[1]].y) - uv0.y;
    const double du2 = static_cast<double>(mesh.uvs[face.vertex[2]].x) - uv0.x;
    const double dv2 = static_cast<double>(mesh.uvs[face.vertex[2]].y) - uv0.y;
    const double det = du1 * dv2 - du2 * dv1;
    if (!(std::abs(det) >= 1e-12)) {
        return false; // Also catches a NaN determinant.
    }
    const Vec3 dPdv(static_cast<float>((e2.x * du1 - e1.x * du2) / det),
        static_cast<float>((e2.y * du1 - e1.y * du2) / det),
        static_cast<float>((e2.z * du1 - e1.z * du2) / det));
    if (!IsFinite(dPdv)) {
        return false;
    }
    *out = -dPdv;
    return true;
}

// Tangents of one original vertex that share a frame; group 0 keeps the vertex, the others get a copy.
struct Group {
    Vec3 sum;
    Vec3 direction;
    float w = 1.0f;
    std::uint32_t vertex = 0;
    std::int32_t next = -1;
};

} // namespace

Vec4 FallbackTangent(const Vec3& normal)
{
    const Vec3 n = UnitOrZero(normal);
    if (n == Vec3::Zero()) {
        return Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    const float ax = std::abs(n.x);
    const float ay = std::abs(n.y);
    const float az = std::abs(n.z);
    const Vec3 axis = (ax <= ay && ax <= az) ? Vec3::Right() : (ay <= az ? Vec3::Up() : Vec3::Forward());
    return Vec4(Normalize(Cross(n, axis)), 1.0f);
}

void OrthonormalizeMeshTangents(MeshData& mesh)
{
    for (std::size_t i = 0; i < mesh.tangents.size(); ++i) {
        const Vec3 n = i < mesh.normals.size() ? UnitOrZero(mesh.normals[i]) : Vec3::Zero();
        Vec3 t;
        if (PerpendicularUnit(mesh.tangents[i].XYZ(), n, &t)) {
            mesh.tangents[i] = Vec4(t, mesh.tangents[i].w < 0.0f ? -1.0f : 1.0f);
        } else {
            mesh.tangents[i] = FallbackTangent(n);
        }
    }
}

TangentGenerationResult GenerateMeshTangents(MeshData& mesh)
{
    const std::size_t vertexCount = mesh.positions.size();
    const bool hasAttributes = mesh.uvs.size() == vertexCount && mesh.normals.size() == vertexCount;

    const auto fillFallback = [&mesh] {
        mesh.tangents.resize(mesh.positions.size());
        for (std::size_t i = 0; i < mesh.tangents.size(); ++i) {
            mesh.tangents[i] = FallbackTangent(i < mesh.normals.size() ? mesh.normals[i] : Vec3::Up());
        }
    };
    if (!hasAttributes) {
        fillFallback();
        return {};
    }

    std::vector<Face> faces;
    faces.reserve(mesh.indices.size() / 3);
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::array<std::uint32_t, 3> corners = { mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
        if (std::all_of(corners.begin(), corners.end(), [vertexCount](std::uint32_t v) { return v < vertexCount; })) {
            faces.push_back({ corners, i });
        }
    }

    std::vector<Vec3> cornerTangents(faces.size() * 3);
    {
        MikkInput input{ &mesh, &faces, &cornerTangents };
        SMikkTSpaceInterface callbacks{};
        callbacks.m_getNumFaces = &MikkNumFaces;
        callbacks.m_getNumVerticesOfFace = &MikkNumVerticesOfFace;
        callbacks.m_getPosition = &MikkPosition;
        callbacks.m_getNormal = &MikkNormal;
        callbacks.m_getTexCoord = &MikkTexCoord;
        callbacks.m_setTSpaceBasic = &MikkSetTangent;
        SMikkTSpaceContext context{ &callbacks, &input };
        if (!genTangSpaceDefault(&context)) {
            fillFallback();
            return {};
        }
    }

    // Weld the corners of every original vertex into frames (corner order keeps the result deterministic).
    std::vector<Group> groups;
    groups.reserve(vertexCount);
    std::vector<std::int32_t> firstGroup(vertexCount, -1);
    std::vector<std::int32_t> cornerGroup(faces.size() * 3, -1);
    const bool hasSkin = mesh.skinWeights.size() == vertexCount;

    for (std::size_t f = 0; f < faces.size(); ++f) {
        Vec3 up;
        const bool hasGradient = FaceUpDirection(mesh, faces[f], &up);
        for (std::size_t c = 0; c < 3; ++c) {
            const std::uint32_t v = faces[f].vertex[c];
            const Vec3 n = UnitOrZero(mesh.normals[v]);

            Vec3 t;
            float w = 1.0f;
            if (!hasGradient || !PerpendicularUnit(cornerTangents[f * 3 + c], n, &t)) {
                t = FallbackTangent(n).XYZ();
            } else {
                w = Dot(Cross(n, t), up) < 0.0f ? -1.0f : 1.0f;
            }

            std::int32_t last = -1;
            std::int32_t found = -1;
            for (std::int32_t g = firstGroup[v]; g >= 0; g = groups[static_cast<std::size_t>(g)].next) {
                const Group& group = groups[static_cast<std::size_t>(g)];
                if (group.w == w && Dot(t, group.direction) >= kMergeDot) {
                    found = g;
                    break;
                }
                last = g;
            }

            if (found >= 0) {
                Group& group = groups[static_cast<std::size_t>(found)];
                group.sum += t;
                PerpendicularUnit(group.sum, n, &group.direction);
            } else {
                Group group;
                group.sum = t;
                group.direction = t;
                group.w = w;
                group.vertex = v;
                if (last >= 0) {
                    group.vertex = static_cast<std::uint32_t>(mesh.positions.size());
                    const Vec3 position = mesh.positions[v];
                    const Vec3 normal = mesh.normals[v];
                    const Vec2 uv = mesh.uvs[v];
                    mesh.positions.push_back(position);
                    mesh.normals.push_back(normal);
                    mesh.uvs.push_back(uv);
                    if (hasSkin) {
                        const VertexSkinWeights weights = mesh.skinWeights[v];
                        mesh.skinWeights.push_back(weights);
                    }
                }
                found = static_cast<std::int32_t>(groups.size());
                if (last >= 0) {
                    groups[static_cast<std::size_t>(last)].next = found;
                } else {
                    firstGroup[v] = found;
                }
                groups.push_back(group);
            }
            cornerGroup[f * 3 + c] = found;
        }
    }

    for (std::size_t f = 0; f < faces.size(); ++f) {
        for (std::size_t c = 0; c < 3; ++c) {
            mesh.indices[faces[f].firstIndex + c] = groups[static_cast<std::size_t>(cornerGroup[f * 3 + c])].vertex;
        }
    }

    fillFallback(); // Vertices no valid triangle uses keep the fallback.
    for (const Group& group : groups) {
        mesh.tangents[group.vertex] = Vec4(group.direction, group.w);
    }
    return { true, static_cast<std::uint32_t>(mesh.positions.size() - vertexCount) };
}

} // namespace gte
