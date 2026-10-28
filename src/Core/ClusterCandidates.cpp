#include "ClusterCandidates.h"

#include "../Renderer/Culling/ClusterCullMath.h"

#include <cmath>
#include <unordered_map>

namespace gte {

bool IsFiniteMatrix(const Mat4& m) noexcept
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m(r, c))) {
                return false;
            }
        }
    }
    return true;
}

namespace {

enum class Verdict : std::uint8_t { Eligible, Skinned, NoIndexBuffer, UnsupportedLayout, NoClusterTable, NonFinite };

// Everything that does not depend on the other inputs. `kind` is set for every input with a supported layout.
Verdict Judge(const ClusterCandidateInput& in, ClusterMaterialKind& kind) noexcept
{
    const std::optional<ClusterMaterialKind> layoutKind = ClusterMaterialKindOf(in.layout);
    if (!layoutKind.has_value()) {
        return Verdict::UnsupportedLayout;
    }
    kind = *layoutKind;
    if (kind != ClusterMaterialKind::None && !in.material.IsValid()) {
        return Verdict::UnsupportedLayout;
    }
    if (in.gpuSkinned) {
        return Verdict::Skinned;
    }
    if (!in.meshHasIndexBuffer) {
        return Verdict::NoIndexBuffer;
    }
    if (!IsFiniteMatrix(in.world)) {
        return Verdict::NonFinite;
    }
    // Only an untextured mesh may get a synthesized table: a textured slice needs the material's own table.
    if (!in.meshHasClusters && (kind != ClusterMaterialKind::None || !in.meshSynthesizable)) {
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

std::vector<fx::ClusterInstance> SelectClusterCandidates(
    std::span<const ClusterCandidateInput> inputs, bool capsPresent, fx::ClusterRejectCounts* rejected)
{
    std::vector<fx::ClusterInstance> result;
    if (!capsPresent) {
        return result;
    }

    std::vector<Verdict> verdicts(inputs.size());
    std::vector<ClusterMaterialKind> kinds(inputs.size(), ClusterMaterialKind::None);

    // A mesh that needs a synthesized table is worth a chain only with enough instances.
    std::unordered_map<fx::MeshToken, std::size_t, fx::MeshTokenHash> synthesizedCounts;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        verdicts[i] = Judge(inputs[i], kinds[i]);
        if (verdicts[i] == Verdict::Eligible && !inputs[i].meshHasClusters) {
            ++synthesizedCounts[inputs[i].mesh];
        }
    }

    fx::ClusterRejectCounts counts;
    result.reserve(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const ClusterCandidateInput& in = inputs[i];
        if (verdicts[i] != Verdict::Eligible) {
            Count(counts, verdicts[i]);
            continue;
        }
        if (!in.meshHasClusters && synthesizedCounts[in.mesh] < kMinInstancesForSynthesizedClusters) {
            ++counts.tooFewInstances;
            continue;
        }
        fx::ClusterInstance instance;
        instance.entity = in.entity;
        instance.mesh = in.mesh;
        instance.material = kinds[i] != ClusterMaterialKind::None ? in.material : fx::MaterialToken{};
        instance.kind = kinds[i];
        instance.world = in.world;
        instance.flags = ClusterInstanceFlagsFor(in.world, &instance.maxScale);
        result.push_back(instance);
    }
    if (rejected != nullptr) {
        *rejected = counts;
    }
    return result;
}

} // namespace gte
