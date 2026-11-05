#include "VertexSkinningSimd.h"

#include "../Math/MathTypes.h"

#include <emmintrin.h>

#include <algorithm>
#include <cassert>

namespace gte {

CompactSkinData BuildCompactSkinData(const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<Vec2>& uvs, const std::vector<VertexSkinWeights>& skinWeights, std::size_t boneCount)
{
    const std::size_t vertexCount = bindPositions.size();
    const bool hasNormals = bindNormals.size() == vertexCount;
    const bool hasUvs = uvs.size() == vertexCount;
    const bool hasWeights = skinWeights.size() == vertexCount;

    CompactSkinData data;
    data.boneCount = boneCount;
    data.bind.resize(vertexCount);
    data.skin.resize(vertexCount);

    for (std::size_t i = 0; i < vertexCount; ++i) {
        const Vec3 normal = hasNormals ? bindNormals[i] : Vec3::Up();
        const Vec2 uv = hasUvs ? uvs[i] : Vec2::Zero();
        CompactBindVertex& bind = data.bind[i];
        bind.position[0] = bindPositions[i].x;
        bind.position[1] = bindPositions[i].y;
        bind.position[2] = bindPositions[i].z;
        bind.normal[0] = normal.x;
        bind.normal[1] = normal.y;
        bind.normal[2] = normal.z;
        bind.uv[0] = uv.x;
        bind.uv[1] = uv.y;

        CompactSkinVertex& skin = data.skin[i];
        skin = CompactSkinVertex{ { kCompactNoInfluence, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        if (!hasWeights) {
            continue;
        }

        // Same slot rule and summation order as SkinVertexRange.
        std::uint32_t indices[4] = {};
        float weights[4] = {};
        int validCount = 0;
        float totalWeight = 0.0f;
        for (int slot = 0; slot < 4; ++slot) {
            const std::int32_t boneIndex = skinWeights[i].boneIndices[slot];
            const float weight = skinWeights[i].boneWeights[slot];
            if (boneIndex < 0 || weight == 0.0f || static_cast<std::size_t>(boneIndex) >= boneCount) {
                continue;
            }
            indices[validCount] = static_cast<std::uint32_t>(boneIndex);
            weights[validCount] = weight;
            totalWeight += weight;
            ++validCount;
        }

        if (validCount == 0 || !(totalWeight > 0.0f)) {
            continue; // Keeps the no-influence marker: bind position and unnormalised bind normal.
        }
        for (int k = 0; k < validCount; ++k) {
            skin.boneIndices[k] = indices[k];
            skin.weights[k] = weights[k] / totalWeight;
        }
        for (int k = validCount; k < 4; ++k) {
            skin.boneIndices[k] = 0;
            skin.weights[k] = 0.0f;
        }
    }
    return data;
}

namespace {

// Lane `lane` of `v` copied to all four lanes.
#define GTE_SPLAT(v, lane) _mm_shuffle_ps((v), (v), _MM_SHUFFLE(lane, lane, lane, lane))

struct MatrixColumns {
    __m128 c0, c1, c2, c3;
};

inline MatrixColumns LoadMatrix(const Mat4& m) noexcept
{
    const float* d = m.Data();
    return { _mm_load_ps(d), _mm_load_ps(d + 4), _mm_load_ps(d + 8), _mm_load_ps(d + 12) };
}

// One vertex: returns [px py pz nx] and [ny nz u v] ready to store.
inline void SkinOneVertex(const CompactSkinVertex& skin, const CompactBindVertex& bind, const Mat4* matrices,
    __m128& out0, __m128& out1) noexcept
{
    const float* bindFloats = reinterpret_cast<const float*>(&bind);
    const __m128 bind0 = _mm_load_ps(bindFloats); // px py pz nx
    const __m128 bind1 = _mm_load_ps(bindFloats + 4); // ny nz u v

    if (skin.boneIndices[0] == kCompactNoInfluence) {
        out0 = bind0;
        out1 = bind1;
        return;
    }

    MatrixColumns m = LoadMatrix(matrices[skin.boneIndices[0]]);
    if (skin.weights[1] != 0.0f) {
        // Blend the matrices first: one transform per vertex whatever the influence count.
        const __m128 w0 = _mm_set1_ps(skin.weights[0]);
        m.c0 = _mm_mul_ps(m.c0, w0);
        m.c1 = _mm_mul_ps(m.c1, w0);
        m.c2 = _mm_mul_ps(m.c2, w0);
        m.c3 = _mm_mul_ps(m.c3, w0);
        for (int slot = 1; slot < 4 && skin.weights[slot] != 0.0f; ++slot) {
            const MatrixColumns other = LoadMatrix(matrices[skin.boneIndices[slot]]);
            const __m128 w = _mm_set1_ps(skin.weights[slot]);
            m.c0 = _mm_add_ps(m.c0, _mm_mul_ps(other.c0, w));
            m.c1 = _mm_add_ps(m.c1, _mm_mul_ps(other.c1, w));
            m.c2 = _mm_add_ps(m.c2, _mm_mul_ps(other.c2, w));
            m.c3 = _mm_add_ps(m.c3, _mm_mul_ps(other.c3, w));
        }
    }

    const __m128 position = _mm_add_ps(_mm_add_ps(_mm_mul_ps(m.c0, GTE_SPLAT(bind0, 0)),
                                           _mm_mul_ps(m.c1, GTE_SPLAT(bind0, 1))),
        _mm_add_ps(_mm_mul_ps(m.c2, GTE_SPLAT(bind0, 2)), m.c3));
    __m128 normal = _mm_add_ps(_mm_add_ps(_mm_mul_ps(m.c0, GTE_SPLAT(bind0, 3)),
                                   _mm_mul_ps(m.c1, GTE_SPLAT(bind1, 0))),
        _mm_mul_ps(m.c2, GTE_SPLAT(bind1, 1)));

    // Same as Normalize(): a length <= kEpsilon (or NaN) gives the zero vector. Exact sqrt and divide on purpose.
    const __m128 squared = _mm_mul_ps(normal, normal);
    const __m128 lengthSquared = _mm_add_ss(_mm_add_ss(squared, GTE_SPLAT(squared, 1)), GTE_SPLAT(squared, 2));
    const __m128 length = _mm_sqrt_ss(lengthSquared);
    const __m128 keep = GTE_SPLAT(_mm_cmpgt_ss(length, _mm_set_ss(kEpsilon)), 0);
    normal = _mm_and_ps(_mm_div_ps(normal, GTE_SPLAT(length, 0)), keep);

    const __m128 positionZNormalX = _mm_shuffle_ps(position, normal, _MM_SHUFFLE(0, 0, 2, 2));
    out0 = _mm_shuffle_ps(position, positionZNormalX, _MM_SHUFFLE(2, 0, 1, 0)); // px py pz nx
    out1 = _mm_shuffle_ps(normal, bind1, _MM_SHUFFLE(3, 2, 2, 1)); // ny nz u v
}

template <bool kTextured>
void SkinPackRange(std::uint32_t beginIndex, std::uint32_t endIndex, const CompactSkinData& data,
    const Mat4* matrices, std::size_t matrixCount, float* outVertices) noexcept
{
    assert(data.bind.size() == data.skin.size());
    if (matrixCount != data.boneCount) {
        return;
    }

    constexpr std::size_t kFloatsPerVertex = kTextured ? 8 : 6;
    const std::size_t end = std::min(static_cast<std::size_t>(endIndex), data.skin.size());
    for (std::size_t i = beginIndex; i < end; ++i) {
        __m128 out0;
        __m128 out1;
        SkinOneVertex(data.skin[i], data.bind[i], matrices, out0, out1);

        float* out = outVertices + i * kFloatsPerVertex;
        _mm_storeu_ps(out, out0);
        if constexpr (kTextured) {
            _mm_storeu_ps(out + 4, out1);
        } else {
            _mm_storel_pi(reinterpret_cast<__m64*>(out + 4), out1);
        }
    }
}

#undef GTE_SPLAT

} // namespace

void SkinPackTexturedRange(std::uint32_t beginIndex, std::uint32_t endIndex, const CompactSkinData& data,
    const Mat4* skinningMatrices, std::size_t matrixCount, float* outVertices)
{
    SkinPackRange<true>(beginIndex, endIndex, data, skinningMatrices, matrixCount, outVertices);
}

void SkinPackUntexturedRange(std::uint32_t beginIndex, std::uint32_t endIndex, const CompactSkinData& data,
    const Mat4* skinningMatrices, std::size_t matrixCount, float* outVertices)
{
    SkinPackRange<false>(beginIndex, endIndex, data, skinningMatrices, matrixCount, outVertices);
}

} // namespace gte
