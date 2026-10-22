#include "MeshClustering.h"

#include "MeshClusterValidation.h"
#include "MeshMaterialPartitioner.h"
#include "MeshNormalRecompute.h"

#include "meshoptimizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace gte {

namespace {

static_assert(sizeof(Vec3) == 3 * sizeof(float));
static_assert(sizeof(Vec2) == 2 * sizeof(float));
static_assert(sizeof(Vec4) == 4 * sizeof(float));

constexpr double kPoorFillRatio = 0.5;
constexpr std::size_t kMaxCount32 = std::numeric_limits<std::uint32_t>::max();

// Per-vertex arrays that travel through the weld and the reorder together.
struct VertexSet {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Vec4> tangents;
};

struct SliceScratch {
    std::vector<unsigned int> cached;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<unsigned int> meshletVertices;
    std::vector<unsigned char> meshletTriangles;
};

bool IsCancelled(const std::atomic<bool>* cancel) noexcept
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

void Refuse(MeshClusterReport& report, std::string message)
{
    report.success = false;
    report.message = std::move(message);
}

bool Fail(std::string* why, const char* text)
{
    if (why != nullptr) {
        *why = text;
    }
    return false;
}

// Copy of a float stream where -0.0 becomes +0.0, so the remap compares equal bytes.
template <class T>
std::vector<float> CanonicalFloats(const std::vector<T>& source)
{
    std::vector<float> floats(source.size() * (sizeof(T) / sizeof(float)));
    if (!source.empty()) {
        std::memcpy(floats.data(), source.data(), floats.size() * sizeof(float));
    }
    for (float& v : floats) {
        if (v == 0.0f) {
            v = 0.0f;
        }
    }
    return floats;
}

template <class T>
std::vector<T> RemapVertices(const std::vector<T>& source, std::size_t newCount, const std::vector<unsigned int>& remap)
{
    std::vector<T> result;
    if (source.empty()) {
        return result;
    }
    result.resize(newCount);
    meshopt_remapVertexBuffer(result.data(), source.data(), source.size(), sizeof(T), remap.data());
    return result;
}

bool ValidateInput(const MeshData& mesh, const std::vector<Material>& materials, const MeshClusterOptions& options,
    std::string& why)
{
    if (!ValidateMeshClusterOptions(options, &why)) {
        return false;
    }
    if (mesh.positions.empty() || mesh.indices.empty()) {
        return Fail(&why, "mesh is empty");
    }
    if (!ClusterBuildCountsFit(mesh.positions.size(), mesh.indices.size())) {
        return Fail(&why, "vertex or index count does not fit 32 bits");
    }
    if (mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size()) {
        return Fail(&why, "normals or uvs differ in length from positions");
    }
    if (!mesh.tangents.empty() && mesh.tangents.size() != mesh.positions.size()) {
        return Fail(&why, "tangents differ in length from positions");
    }
    if (mesh.indices.size() % 3 != 0) {
        return Fail(&why, "index count is not a multiple of 3");
    }
    if (!mesh.skinWeights.empty()) {
        return Fail(&why, "skinned meshes cannot be clustered");
    }

    const std::size_t vertexCount = mesh.positions.size();
    for (const std::uint32_t index : mesh.indices) {
        if (index >= vertexCount) {
            return Fail(&why, "an index is out of range");
        }
    }
    for (const Vec3& p : mesh.positions) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            return Fail(&why, "a position is not finite");
        }
    }
    for (const MeshMaterialSlice& slice : PartitionMeshMaterials(mesh.indices.size(), materials)) {
        if (slice.start % 3 != 0 || slice.count % 3 != 0) {
            return Fail(&why, "a material slice is not a whole number of triangles");
        }
    }
    return true;
}

// Position-only weld merges uv seams, so it is only allowed for meshes that carry no uv data.
bool ValidatePositionOnlyWeld(const MeshData& mesh, const std::vector<Material>& materials, std::string& why)
{
    for (const Material& m : materials) {
        if (HasAnyTextureIndex(m)) {
            return Fail(&why, "position-only weld refused: a material uses a texture");
        }
    }
    for (const Vec2& uv : mesh.uvs) {
        if (uv.x != 0.0f || uv.y != 0.0f) {
            return Fail(&why, "position-only weld refused: the mesh has non-zero uvs");
        }
    }
    return true;
}

// Fills `remap` and returns the unique vertex count.
std::size_t BuildWeldRemap(const MeshData& mesh, ClusterWeldMode weld, std::vector<unsigned int>& remap)
{
    const std::vector<float> positions = CanonicalFloats(mesh.positions);
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;

    meshopt_Stream streams[4];
    std::size_t streamCount = 0;
    streams[streamCount++] = { positions.data(), sizeof(Vec3), sizeof(Vec3) };
    if (weld == ClusterWeldMode::Exact) {
        normals = CanonicalFloats(mesh.normals);
        uvs = CanonicalFloats(mesh.uvs);
        streams[streamCount++] = { normals.data(), sizeof(Vec3), sizeof(Vec3) };
        streams[streamCount++] = { uvs.data(), sizeof(Vec2), sizeof(Vec2) };
        if (!mesh.tangents.empty()) {
            tangents = CanonicalFloats(mesh.tangents);
            streams[streamCount++] = { tangents.data(), sizeof(Vec4), sizeof(Vec4) };
        }
    }

    remap.resize(mesh.positions.size());
    return meshopt_generateVertexRemapMulti(remap.data(), mesh.indices.data(), mesh.indices.size(),
        mesh.positions.size(), streams, streamCount);
}

void FillCone(const meshopt_Bounds& bounds, float (&cone)[4])
{
    const double length = std::sqrt(static_cast<double>(bounds.cone_axis[0]) * bounds.cone_axis[0]
        + static_cast<double>(bounds.cone_axis[1]) * bounds.cone_axis[1]
        + static_cast<double>(bounds.cone_axis[2]) * bounds.cone_axis[2]);
    const float cutoff = bounds.cone_cutoff;
    if (!std::isfinite(length) || !std::isfinite(cutoff) || length < 0.5 || cutoff < 0.0f || !(cutoff < 1.0f)) {
        cone[0] = cone[1] = cone[2] = 0.0f;
        cone[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 3; ++i) {
        cone[i] = static_cast<float>(bounds.cone_axis[i] / length) * kClusterConeWindingSign;
    }
    cone[3] = cutoff;
}

// Sphere from the box center that contains every vertex; used when the library sphere is unusable.
void FillSphereFromBox(const MeshCluster& box, std::span<const unsigned int> vertices, const Vec3* positions,
    float (&sphere)[4])
{
    const double cx = 0.5 * (static_cast<double>(box.aabbMin[0]) + box.aabbMax[0]);
    const double cy = 0.5 * (static_cast<double>(box.aabbMin[1]) + box.aabbMax[1]);
    const double cz = 0.5 * (static_cast<double>(box.aabbMin[2]) + box.aabbMax[2]);
    double maxDistSq = 0.0;
    for (const unsigned int v : vertices) {
        const double dx = positions[v].x - cx;
        const double dy = positions[v].y - cy;
        const double dz = positions[v].z - cz;
        maxDistSq = std::max(maxDistSq, dx * dx + dy * dy + dz * dz);
    }
    sphere[0] = static_cast<float>(cx);
    sphere[1] = static_cast<float>(cy);
    sphere[2] = static_cast<float>(cz);
    sphere[3] = static_cast<float>(std::sqrt(maxDistSq)) * 1.00001f + 1e-6f;
}

bool SphereContains(const float (&sphere)[4], std::span<const unsigned int> vertices, const Vec3* positions)
{
    if (!std::isfinite(sphere[0]) || !std::isfinite(sphere[1]) || !std::isfinite(sphere[2])
        || !std::isfinite(sphere[3]) || sphere[3] <= 0.0f) {
        return false;
    }
    const double limit = static_cast<double>(sphere[3]) * 1.0001 + 1e-6;
    for (const unsigned int v : vertices) {
        const double dx = positions[v].x - sphere[0];
        const double dy = positions[v].y - sphere[1];
        const double dz = positions[v].z - sphere[2];
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > limit) {
            return false;
        }
    }
    return true;
}

MeshCluster MakeCluster(const meshopt_Meshlet& meshlet, const unsigned int* vertices, const unsigned char* triangles,
    const Vec3* positions, std::size_t vertexCount, std::uint32_t firstIndex)
{
    MeshCluster cluster{};
    cluster.firstIndex = firstIndex;
    cluster.indexCount = meshlet.triangle_count * 3;

    const std::span<const unsigned int> used(vertices, meshlet.vertex_count);
    Vec3 lo = positions[used.front()];
    Vec3 hi = lo;
    for (const unsigned int v : used) {
        const Vec3& p = positions[v];
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    cluster.aabbMin[0] = lo.x;
    cluster.aabbMin[1] = lo.y;
    cluster.aabbMin[2] = lo.z;
    cluster.aabbMax[0] = hi.x;
    cluster.aabbMax[1] = hi.y;
    cluster.aabbMax[2] = hi.z;

    const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
        vertices, triangles, meshlet.triangle_count, &positions[0].x, vertexCount, sizeof(Vec3));
    cluster.sphere[0] = bounds.center[0];
    cluster.sphere[1] = bounds.center[1];
    cluster.sphere[2] = bounds.center[2];
    cluster.sphere[3] = bounds.radius;
    if (!SphereContains(cluster.sphere, used, positions)) {
        FillSphereFromBox(cluster, used, positions, cluster.sphere);
    }
    FillCone(bounds, cluster.cone);
    return cluster;
}

// Clusters indices[start, start + count) in place and appends the records.
bool ClusterSlice(std::vector<std::uint32_t>& indices, std::size_t start, std::size_t count, const VertexSet& vertices,
    const MeshClusterOptions& options, SliceScratch& scratch, std::vector<MeshCluster>& clusters,
    std::size_t& vertexRefs, std::string& why)
{
    const std::size_t vertexCount = vertices.positions.size();
    const Vec3* positions = vertices.positions.data();

    scratch.cached.resize(count);
    meshopt_optimizeVertexCache(scratch.cached.data(), indices.data() + start, count, vertexCount);

    scratch.meshlets.resize(meshopt_buildMeshletsBound(count, options.maxVerticesPerCluster, options.maxTrianglesPerCluster));
    scratch.meshletVertices.resize(count);
    scratch.meshletTriangles.resize(count);
    const std::size_t meshletCount = meshopt_buildMeshlets(scratch.meshlets.data(), scratch.meshletVertices.data(),
        scratch.meshletTriangles.data(), scratch.cached.data(), count, &positions[0].x, vertexCount, sizeof(Vec3),
        options.maxVerticesPerCluster, options.maxTrianglesPerCluster, options.coneWeight);

    std::size_t cursor = start;
    for (std::size_t m = 0; m < meshletCount; ++m) {
        const meshopt_Meshlet& meshlet = scratch.meshlets[m];
        const std::size_t emitCount = static_cast<std::size_t>(meshlet.triangle_count) * 3;
        if (meshlet.triangle_count == 0 || meshlet.vertex_count == 0 || cursor + emitCount > start + count
            || meshlet.vertex_count > options.maxVerticesPerCluster
            || meshlet.triangle_count > options.maxTrianglesPerCluster) {
            return Fail(&why, "the meshlet builder returned an invalid cluster");
        }

        const unsigned int* meshletVertices = scratch.meshletVertices.data() + meshlet.vertex_offset;
        const unsigned char* meshletTriangles = scratch.meshletTriangles.data() + meshlet.triangle_offset;
        for (std::size_t i = 0; i < emitCount; ++i) {
            indices[cursor + i] = meshletVertices[meshletTriangles[i]];
        }
        clusters.push_back(MakeCluster(meshlet, meshletVertices, meshletTriangles, positions, vertexCount,
            static_cast<std::uint32_t>(cursor)));
        vertexRefs += meshlet.vertex_count;
        cursor += emitCount;
    }
    if (cursor != start + count) {
        return Fail(&why, "the meshlet builder dropped triangles");
    }
    return true;
}

void FillStats(MeshClusterReport& report, std::size_t vertexRefs, const MeshClusterOptions& options)
{
    if (report.clusterCount == 0) {
        return;
    }
    const double clusters = static_cast<double>(report.clusterCount);
    report.avgTrianglesPerCluster = static_cast<float>(static_cast<double>(report.triangleCount) / clusters);
    report.avgVerticesPerCluster = static_cast<float>(static_cast<double>(vertexRefs) / clusters);
    report.poorFill = report.avgTrianglesPerCluster < kPoorFillRatio * options.maxTrianglesPerCluster;
}

void Run(MeshData& mesh, const std::vector<Material>& materials, const MeshClusterOptions& options,
    const std::atomic<bool>* cancel, bool earlyOutOnPoorFill, MeshClusterReport& report)
{
    std::string why;
    report.lastStep = "validate";
    if (!ValidateInput(mesh, materials, options, why)) {
        Refuse(report, std::move(why));
        return;
    }
    if (options.weld == ClusterWeldMode::PositionOnly && !ValidatePositionOnlyWeld(mesh, materials, why)) {
        Refuse(report, std::move(why));
        return;
    }
    report.vertexCountBefore = mesh.positions.size();
    report.triangleCount = mesh.indices.size() / 3;

    if (IsCancelled(cancel)) {
        Refuse(report, "cancelled");
        return;
    }

    // Weld: new arrays only, `mesh` stays untouched until the end.
    VertexSet vertices;
    std::vector<std::uint32_t> indices;
    {
        std::vector<unsigned int> remap;
        const std::size_t uniqueCount = BuildWeldRemap(mesh, options.weld, remap);

        indices.resize(mesh.indices.size());
        meshopt_remapIndexBuffer(indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());
        vertices.positions = RemapVertices(mesh.positions, uniqueCount, remap);
        vertices.uvs = RemapVertices(mesh.uvs, uniqueCount, remap);
        if (options.weld == ClusterWeldMode::Exact) {
            vertices.normals = RemapVertices(mesh.normals, uniqueCount, remap);
            vertices.tangents = RemapVertices(mesh.tangents, uniqueCount, remap);
        } else {
            MeshData scratch;
            scratch.positions = std::move(vertices.positions);
            scratch.indices = std::move(indices);
            RecomputeMeshNormalsFromGeometry(scratch);
            vertices.positions = std::move(scratch.positions);
            vertices.normals = std::move(scratch.normals);
            indices = std::move(scratch.indices);
        }
    }
    report.lastStep = "weld";

    // Cheap early out for meshes that cannot fill clusters (unshared triangle soup).
    if (earlyOutOnPoorFill) {
        const double expected = std::min(static_cast<double>(options.maxTrianglesPerCluster),
            static_cast<double>(options.maxVerticesPerCluster) * static_cast<double>(report.triangleCount)
                / static_cast<double>(vertices.positions.size()));
        if (expected < kPoorFillRatio * options.maxTrianglesPerCluster) {
            report.poorFill = true;
            report.vertexCountAfter = vertices.positions.size();
            Refuse(report, "skipped: expected cluster fill is poor");
            return;
        }
    }
    if (IsCancelled(cancel)) {
        Refuse(report, "cancelled");
        return;
    }

    // Cluster each material slice inside its own index range.
    std::vector<MeshCluster> clusters;
    std::size_t vertexRefs = 0;
    {
        SliceScratch scratch;
        for (const MeshMaterialSlice& slice : PartitionMeshMaterials(indices.size(), materials)) {
            if (IsCancelled(cancel)) {
                Refuse(report, "cancelled");
                return;
            }
            if (!ClusterSlice(indices, slice.start, slice.count, vertices, options, scratch, clusters, vertexRefs, why)) {
                Refuse(report, std::move(why));
                return;
            }
        }
    }
    report.lastStep = "cluster";
    if (IsCancelled(cancel)) {
        Refuse(report, "cancelled");
        return;
    }

    // Reorder vertices for fetch locality; index values change, cluster ranges do not.
    {
        std::vector<unsigned int> remap(vertices.positions.size());
        const std::size_t finalCount = meshopt_optimizeVertexFetchRemap(
            remap.data(), indices.data(), indices.size(), vertices.positions.size());
        std::vector<std::uint32_t> remappedIndices(indices.size());
        meshopt_remapIndexBuffer(remappedIndices.data(), indices.data(), indices.size(), remap.data());
        indices = std::move(remappedIndices);
        vertices.positions = RemapVertices(vertices.positions, finalCount, remap);
        vertices.normals = RemapVertices(vertices.normals, finalCount, remap);
        vertices.uvs = RemapVertices(vertices.uvs, finalCount, remap);
        vertices.tangents = RemapVertices(vertices.tangents, finalCount, remap);
    }
    report.lastStep = "fetch";
    if (IsCancelled(cancel)) {
        Refuse(report, "cancelled");
        return;
    }

    if (!ValidateClusterTable(clusters, indices.size(), &why)) {
        Refuse(report, "cluster table is invalid: " + why);
        return;
    }

    report.clusterCount = clusters.size();
    report.vertexCountAfter = vertices.positions.size();
    FillStats(report, vertexRefs, options);

    // Commit: moves only, nothing below can throw.
    mesh.positions = std::move(vertices.positions);
    mesh.normals = std::move(vertices.normals);
    mesh.uvs = std::move(vertices.uvs);
    mesh.tangents = std::move(vertices.tangents);
    mesh.indices = std::move(indices);
    mesh.clusters = std::move(clusters);

    report.success = true;
    report.lastStep = "done";
    report.message = "ok";
}

} // namespace

bool ValidateMeshClusterOptions(const MeshClusterOptions& options, std::string* why)
{
    if (options.weld != ClusterWeldMode::Exact && options.weld != ClusterWeldMode::PositionOnly) {
        return Fail(why, "unknown weld mode");
    }
    if (options.maxVerticesPerCluster < 3 || options.maxVerticesPerCluster > 255) {
        return Fail(why, "max vertices per cluster must be 3..255");
    }
    if (options.maxTrianglesPerCluster < 4 || options.maxTrianglesPerCluster > 512
        || options.maxTrianglesPerCluster % 4 != 0) {
        return Fail(why, "max triangles per cluster must be 4..512 and a multiple of 4");
    }
    if (!std::isfinite(options.coneWeight) || options.coneWeight < 0.0f || options.coneWeight > 1.0f) {
        return Fail(why, "cone weight must be a finite value in 0..1");
    }
    return true;
}

bool ClusterBuildCountsFit(std::size_t vertexCount, std::size_t indexCount) noexcept
{
    return vertexCount <= kMaxCount32 && indexCount <= kMaxCount32;
}

MeshClusterReport BuildMeshClusters(MeshData& mesh, const std::vector<Material>& materials,
    const MeshClusterOptions& options, const std::atomic<bool>* cancel, bool earlyOutOnPoorFill)
{
    const auto begin = std::chrono::steady_clock::now();
    MeshClusterReport report;
    report.weld = options.weld;
    try {
        Run(mesh, materials, options, cancel, earlyOutOnPoorFill, report);
    } catch (const std::bad_alloc&) {
        report = MeshClusterReport{};
        report.weld = options.weld;
        Refuse(report, "out of memory");
    } catch (const std::exception& e) {
        Refuse(report, std::string("unexpected error: ") + e.what());
    } catch (...) {
        Refuse(report, "unexpected error");
    }
    report.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return report;
}

} // namespace gte
