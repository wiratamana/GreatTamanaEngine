#include "TerrainAsset.h"

#include "TerrainHeightField.h"
#include "TerrainMaterial.h"
#include "TerrainNode.h"

#include "../Jobs/JobSystem.h"
#include "../Profiling/JobScopeTimer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace gte::terrain {

namespace {

struct BakeState {
    const TerrainHeightField* field = nullptr;
    BakedTerrain* out = nullptr;
    const std::atomic<bool>* cancel = nullptr;
    std::atomic<std::uint32_t> next{ 0 };
    std::atomic<std::uint32_t> done{ 0 };
    std::atomic<bool> failed{ false };
    std::uint32_t total = 0;
};

BakedChunk BakeChunk(const TerrainHeightField& field, const NodeKey& key)
{
    const TerrainSettings& s = field.Settings();
    const NodeMesh mesh = BuildNode(field, key);
    const std::size_t verts = static_cast<std::size_t>(GridVertexCount(field.Layout().quads));

    BakedChunk chunk;
    chunk.heightMin = 1.0e30f;
    chunk.heightMax = -1.0e30f;
    for (std::size_t i = 0; i < verts; ++i) {
        chunk.heightMin = std::min(chunk.heightMin, mesh.vertices[i].position[1]);
        chunk.heightMax = std::max(chunk.heightMax, mesh.vertices[i].position[1]);
    }
    chunk.heights.resize(verts);
    chunk.normals.resize(verts);
    chunk.splat.resize(verts);
    for (std::size_t i = 0; i < verts; ++i) {
        const TerrainVertex& v = mesh.vertices[i];
        chunk.heights[i] = QuantizeHeight(v.position[1], chunk.heightMin, chunk.heightMax);
        chunk.normals[i] = EncodeOctNormal(v.normal[0], v.normal[1], v.normal[2]);
        chunk.splat[i] = SurfaceSplat(v.position[1], std::acos(std::min(1.0f, v.normal[1])), s);
    }
    chunk.bounds = ComputeChunkBounds(chunk, s, key.x, key.z);
    return chunk;
}

// Bakes one chunk; false when nothing is left to do or the bake was cancelled.
bool BakeNext(BakeState& st)
{
    if (st.cancel != nullptr && st.cancel->load(std::memory_order_relaxed)) {
        return false;
    }
    const std::uint32_t i = st.next.fetch_add(1, std::memory_order_relaxed);
    if (i >= st.total) {
        return false;
    }
    try {
        const NodeKey key{ st.out->firstX + static_cast<std::int32_t>(i % st.out->countX),
            st.out->firstZ + static_cast<std::int32_t>(i / st.out->countX), 0 };
        st.out->chunks[i] = BakeChunk(*st.field, key);
        st.done.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        st.failed.store(true, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void BakeWorker(void* payload)
{
    GTE_PROFILE_JOB_SCOPE("Terrain.Bake");
    auto& st = *static_cast<BakeState*>(payload);
    while (BakeNext(st)) {
    }
}

void StoreVertex(NodeMesh& mesh, const TerrainSettings& color, int verts, int ix, int iz, float spacing, float h,
    float nx, float ny, float nz)
{
    const float slope = std::acos(std::min(1.0f, ny));
    const std::size_t vi = static_cast<std::size_t>(iz) * static_cast<std::size_t>(verts) + static_cast<std::size_t>(ix);
    TerrainVertex& v = mesh.vertices[vi];
    v.position[0] = static_cast<float>(ix) * spacing;
    v.position[1] = h;
    v.position[2] = static_cast<float>(iz) * spacing;
    v.normal[0] = nx;
    v.normal[1] = ny;
    v.normal[2] = nz;
    v.uv[0] = Height01(h, color);
    v.uv[1] = Slope01(slope);
    mesh.colors[vi] = PackRgba8(SurfaceColor(h, slope, color));
}

} // namespace

std::optional<BakedTerrain> BakeRegion(const TerrainSettings& requested, std::int32_t firstX, std::int32_t firstZ,
    std::uint32_t countX, std::uint32_t countZ, const BakeProgress& progress, const std::atomic<bool>* cancel,
    ITerrainExecutor* executor)
{
    if (countX == 0 || countZ == 0 || countX > kTerrainAssetMaxAxisChunks || countZ > kTerrainAssetMaxAxisChunks ||
        static_cast<std::uint64_t>(countX) * countZ > kTerrainAssetMaxChunks ||
        static_cast<std::int64_t>(firstX) + countX > INT32_MAX || static_cast<std::int64_t>(firstZ) + countZ > INT32_MAX) {
        return std::nullopt;
    }
    const TerrainSettings settings = Sanitize(requested);
    const TerrainHeightField field(settings);

    BakedTerrain baked;
    baked.settings = settings;
    baked.firstX = firstX;
    baked.firstZ = firstZ;
    baked.countX = countX;
    baked.countZ = countZ;
    baked.chunks.resize(static_cast<std::size_t>(countX) * countZ);

    BakeState state;
    state.field = &field;
    state.out = &baked;
    state.cancel = cancel;
    state.total = static_cast<std::uint32_t>(baked.chunks.size());

    // The state outlives every job: this function does not return before the handle is complete.
    Jobs::JobHandle handle;
    const std::size_t workers = executor != nullptr ? 1 : Jobs::JobSystem::Instance().WorkerCount();
    for (std::size_t w = 0; w < workers; ++w) {
        if (executor != nullptr) {
            executor->Run(&BakeWorker, &state, handle);
        } else {
            Jobs::JobSystem::Instance().Schedule(&BakeWorker, &state, handle);
        }
    }

    std::uint32_t lastReported = UINT32_MAX;
    const auto report = [&]() {
        const std::uint32_t done = state.done.load(std::memory_order_relaxed);
        if (progress && done != lastReported) {
            lastReported = done;
            progress(done, state.total);
        }
    };
    while (BakeNext(state)) {
        report();
    }
    while (!handle.IsComplete()) {
        report();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    report();

    const bool cancelled = cancel != nullptr && cancel->load(std::memory_order_relaxed);
    if (cancelled || state.failed.load() || state.done.load() != state.total) {
        return std::nullopt;
    }
    return baked;
}

bool BakedCoversNode(const BakedTerrain& baked, const NodeKey& key) noexcept
{
    if (key.level > 20 || key.level >= baked.settings.lodLevelCount ||
        baked.chunks.size() != static_cast<std::size_t>(baked.countX) * baked.countZ || baked.chunks.empty()) {
        return false;
    }
    const std::int64_t stride = std::int64_t{ 1 } << key.level;
    const std::int64_t x0 = static_cast<std::int64_t>(key.x) * stride;
    const std::int64_t z0 = static_cast<std::int64_t>(key.z) * stride;
    return x0 >= baked.firstX && z0 >= baked.firstZ && x0 + stride <= static_cast<std::int64_t>(baked.firstX) + baked.countX &&
        z0 + stride <= static_cast<std::int64_t>(baked.firstZ) + baked.countZ;
}

std::optional<NodeMesh> BuildNodeFromBaked(const BakedTerrain& baked, const NodeKey& key, const TerrainSettings* colorSettings)
{
    if (!BakedCoversNode(baked, key)) {
        return std::nullopt;
    }
    const TerrainSettings& color = colorSettings != nullptr ? *colorSettings : baked.settings;
    const TerrainLayout layout = MakeLayout(baked.settings);
    const int q = layout.quads;
    const int verts = q + 1;
    const NodeGeometry g = NodeGeometryOf(layout, key);

    NodeMesh mesh;
    mesh.key = key;
    mesh.originX = g.originX;
    mesh.originZ = g.originZ;
    mesh.spacing = static_cast<float>(g.spacing);
    mesh.size = static_cast<float>(g.size);
    mesh.vertices.resize(NodeVertexCount(q));
    mesh.colors.resize(mesh.vertices.size());
    const float sp = mesh.spacing;

    if (key.level == 0) {
        const BakedChunk* chunk = baked.FindChunk(key.x, key.z);
        if (chunk == nullptr || chunk->heights.size() != GridVertexCount(q)) {
            return std::nullopt;
        }
        for (int iz = 0; iz < verts; ++iz) {
            for (int ix = 0; ix < verts; ++ix) {
                const std::size_t i = static_cast<std::size_t>(iz) * static_cast<std::size_t>(verts) + static_cast<std::size_t>(ix);
                float nx = 0.0f;
                float ny = 1.0f;
                float nz = 0.0f;
                DecodeOctNormal(chunk->normals[i], nx, ny, nz);
                StoreVertex(mesh, color, verts, ix, iz, sp, DequantizeHeight(chunk->heights[i], chunk->heightMin, chunk->heightMax), nx, ny, nz);
            }
        }
    } else {
        // Coarser level: take every 2^level-th baked sample; normals come from the decimated heights.
        const std::int64_t stride = std::int64_t{ 1 } << key.level;
        const std::int64_t regionW = static_cast<std::int64_t>(baked.countX) * q;
        const std::int64_t regionH = static_cast<std::int64_t>(baked.countZ) * q;
        const std::int64_t gx0 = (static_cast<std::int64_t>(key.x) * stride - baked.firstX) * q;
        const std::int64_t gz0 = (static_cast<std::int64_t>(key.z) * stride - baked.firstZ) * q;
        const auto heightAt = [&](std::int64_t gx, std::int64_t gz) {
            gx = std::clamp<std::int64_t>(gx, 0, regionW);
            gz = std::clamp<std::int64_t>(gz, 0, regionH);
            const std::int64_t cx = std::min<std::int64_t>(gx / q, baked.countX - 1);
            const std::int64_t cz = std::min<std::int64_t>(gz / q, baked.countZ - 1);
            const BakedChunk& c = baked.chunks[static_cast<std::size_t>(cz) * baked.countX + static_cast<std::size_t>(cx)];
            const std::size_t lx = static_cast<std::size_t>(gx - cx * q);
            const std::size_t lz = static_cast<std::size_t>(gz - cz * q);
            return DequantizeHeight(c.heights[lz * static_cast<std::size_t>(verts) + lx], c.heightMin, c.heightMax);
        };
        const float sp0 = static_cast<float>(layout.spacingLevel0);
        for (int iz = 0; iz < verts; ++iz) {
            for (int ix = 0; ix < verts; ++ix) {
                const std::int64_t gx = gx0 + static_cast<std::int64_t>(ix) * stride;
                const std::int64_t gz = gz0 + static_cast<std::int64_t>(iz) * stride;
                const std::int64_t loX = std::max<std::int64_t>(gx - stride, 0);
                const std::int64_t hiX = std::min<std::int64_t>(gx + stride, regionW);
                const std::int64_t loZ = std::max<std::int64_t>(gz - stride, 0);
                const std::int64_t hiZ = std::min<std::int64_t>(gz + stride, regionH);
                const float scaleX = 2.0f * sp / (static_cast<float>(hiX - loX) * sp0);
                const float scaleZ = 2.0f * sp / (static_cast<float>(hiZ - loZ) * sp0);
                const float nx = (heightAt(loX, gz) - heightAt(hiX, gz)) * scaleX;
                const float nz = (heightAt(gx, loZ) - heightAt(gx, hiZ)) * scaleZ;
                const float ny = 2.0f * sp;
                const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                StoreVertex(mesh, color, verts, ix, iz, sp, heightAt(gx, gz), nx * inv, ny * inv, nz * inv);
            }
        }
    }
    FinishNodeMesh(mesh, q);
    return mesh;
}

NodeProvider MakeBakedNodeProvider(std::shared_ptr<const BakedTerrain> baked)
{
    return [baked = std::move(baked)](const TerrainSettings& current, const NodeKey& key) -> std::optional<NodeMesh> {
        if (!baked) {
            return std::nullopt;
        }
        const SettingsChange change = ClassifyChange(baked->settings, current);
        if (HasChange(change, SettingsChange::Height) || HasChange(change, SettingsChange::Geometry)) {
            return std::nullopt;
        }
        return BuildNodeFromBaked(*baked, key, &current);
    };
}

} // namespace gte::terrain
