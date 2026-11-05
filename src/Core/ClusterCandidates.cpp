#include "ClusterCandidates.h"

#include "../Renderer/Culling/ClusterCullMath.h"

#include <cstdint>
#include <cstring>

namespace gte {

bool IsFiniteMatrix(const Mat4& m) noexcept
{
    // A float is non-finite exactly when its exponent bits are all ones.
    std::uint32_t bits[16];
    std::memcpy(bits, m.Data(), sizeof(bits));
    std::uint32_t nonFinite = 0;
    for (const std::uint32_t word : bits) {
        nonFinite |= (word & 0x7F800000u) == 0x7F800000u ? 1u : 0u;
    }
    return nonFinite == 0;
}

bool ClusterCandidatePair::SameAs(const ClusterCandidatePair& other) const noexcept
{
    if (mesh != other.mesh || pipeline != other.pipeline || resolved != other.resolved || !(facts == other.facts)
        || clusterBounds.has_value() != other.clusterBounds.has_value()) {
        return false;
    }
    return !clusterBounds.has_value() || std::memcmp(&*clusterBounds, &*other.clusterBounds, sizeof(AABB)) == 0;
}

bool ClusterCandidateReuse::Note(const ClusterCandidatePair& pair)
{
    for (const ClusterCandidatePair& known : pairs) {
        if (known.mesh == pair.mesh && known.pipeline == pair.pipeline) {
            return true;
        }
    }
    if (pairs.size() >= kMaxPairs) {
        return false;
    }
    pairs.push_back(pair);
    return true;
}

namespace {

enum class Verdict : std::uint8_t { Eligible, Skinned, NoIndexBuffer, UnsupportedLayout, NoClusterTable, NonFinite };

// Everything that does not depend on the other inputs. `kind` is set for every input with a supported layout.
Verdict Judge(const ClusterCandidateFacts& facts, const fx::MaterialToken& material, const Mat4& world,
    ClusterMaterialKind& kind) noexcept
{
    const std::optional<ClusterMaterialKind> layoutKind = ClusterMaterialKindOf(facts.layout);
    if (!layoutKind.has_value()) {
        return Verdict::UnsupportedLayout;
    }
    kind = *layoutKind;
    if (kind != ClusterMaterialKind::None && !material.IsValid()) {
        return Verdict::UnsupportedLayout;
    }
    if (facts.gpuSkinned) {
        return Verdict::Skinned;
    }
    if (!facts.meshHasIndexBuffer) {
        return Verdict::NoIndexBuffer;
    }
    if (!IsFiniteMatrix(world)) {
        return Verdict::NonFinite;
    }
    // Only an untextured mesh may get a synthesized table: a textured slice needs the material's own table.
    if (!facts.meshHasClusters && (kind != ClusterMaterialKind::None || !facts.meshSynthesizable)) {
        return Verdict::NoClusterTable;
    }
    return Verdict::Eligible;
}

void Count(fx::ClusterRejectCounts& counts, Verdict verdict) noexcept
{
    switch (verdict) {
    case Verdict::Skinned: ++counts.skinned; break;
    case Verdict::NoIndexBuffer: ++counts.noIndexBuffer; break;
    case Verdict::UnsupportedLayout: ++counts.unsupportedLayout; break;
    case Verdict::NoClusterTable: ++counts.noClusterTable; break;
    case Verdict::NonFinite: ++counts.nonFinite; break;
    case Verdict::Eligible: break;
    }
}

} // namespace

ClusterCandidateBuilder::ClusterCandidateBuilder(std::vector<fx::ClusterInstance>& out, std::size_t expectedCount)
    : m_out(out)
{
    m_out.clear();
    m_out.reserve(expectedCount);
}

void ClusterCandidateBuilder::Add(const ClusterCandidateFacts& facts, Entity entity, fx::MeshToken mesh,
    fx::MaterialToken material, const Mat4& world)
{
    ClusterMaterialKind kind = ClusterMaterialKind::None;
    const Verdict verdict = Judge(facts, material, world, kind);
    if (verdict != Verdict::Eligible) {
        Count(m_counts, verdict);
        return;
    }
    if (!facts.meshHasClusters) {
        m_synthesized.push_back(static_cast<std::uint32_t>(m_out.size()));
        ++m_synthesizedCounts[mesh];
    }
    fx::ClusterInstance& instance = m_out.emplace_back();
    instance.entity = entity;
    instance.mesh = mesh;
    instance.material = kind != ClusterMaterialKind::None ? material : fx::MaterialToken{};
    instance.kind = kind;
    instance.world = world;
    // Flags depend only on the 3x3 basis (the matrix is finite here); reuse the last result while it repeats.
    const float* data = world.Data();
    float basis[9] = { data[0], data[1], data[2], data[4], data[5], data[6], data[8], data[9], data[10] };
    if (!m_haveBasis || std::memcmp(m_basis, basis, sizeof(basis)) != 0) {
        std::memcpy(m_basis, basis, sizeof(basis));
        m_haveBasis = true;
        m_basisFlags = ClusterInstanceFlagsFor(world, &m_basisMaxScale);
    }
    instance.flags = m_basisFlags;
    instance.maxScale = m_basisMaxScale;
}

void ClusterCandidateBuilder::Finish(fx::ClusterRejectCounts* rejected)
{
    // A mesh that needs a synthesized table is worth a chain only with enough instances.
    bool anyTooFew = false;
    for (const auto& entry : m_synthesizedCounts) {
        anyTooFew = anyTooFew || entry.second < kMinInstancesForSynthesizedClusters;
    }
    if (anyTooFew) {
        std::size_t kept = 0;
        std::size_t next = 0; // Position in m_synthesized of the next synthesized instance.
        for (std::size_t i = 0; i < m_out.size(); ++i) {
            const bool synthesized = next < m_synthesized.size() && m_synthesized[next] == i;
            next += synthesized ? 1 : 0;
            if (synthesized && m_synthesizedCounts[m_out[i].mesh] < kMinInstancesForSynthesizedClusters) {
                ++m_counts.tooFewInstances;
                continue;
            }
            if (kept != i) {
                m_out[kept] = m_out[i];
            }
            ++kept;
        }
        m_out.resize(kept);
    }
    if (rejected != nullptr) {
        *rejected = m_counts;
    }
}

std::vector<fx::ClusterInstance> SelectClusterCandidates(
    std::span<const ClusterCandidateInput> inputs, bool capsPresent, fx::ClusterRejectCounts* rejected)
{
    std::vector<fx::ClusterInstance> result;
    if (!capsPresent) {
        return result;
    }
    ClusterCandidateBuilder builder(result, inputs.size());
    for (const ClusterCandidateInput& in : inputs) {
        const ClusterCandidateFacts facts{ in.meshHasClusters, in.meshSynthesizable, in.meshHasIndexBuffer, in.layout,
            in.gpuSkinned };
        builder.Add(facts, in.entity, in.mesh, in.material, in.world);
    }
    builder.Finish(rejected);
    return result;
}

} // namespace gte
