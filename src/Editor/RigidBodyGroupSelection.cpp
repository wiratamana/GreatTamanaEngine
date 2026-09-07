#include "RigidBodyGroupSelection.h"

#include <algorithm>

namespace gte {

std::vector<std::vector<std::int32_t>> BuildRigidBodyAdjacency(
    const std::vector<RigidBodyJointEdge>& edges, std::size_t rigidBodyCount)
{
    std::vector<std::vector<std::int32_t>> adjacency(rigidBodyCount);

    for (const RigidBodyJointEdge& edge : edges) {
        if (edge.rigidBodyAIndex < 0 || edge.rigidBodyBIndex < 0) {
            continue;
        }
        if (static_cast<std::size_t>(edge.rigidBodyAIndex) >= rigidBodyCount
            || static_cast<std::size_t>(edge.rigidBodyBIndex) >= rigidBodyCount) {
            continue;
        }
        if (edge.rigidBodyAIndex == edge.rigidBodyBIndex) {
            continue; // Self-joint - never a real connection.
        }

        std::vector<std::int32_t>& neighborsA = adjacency[static_cast<std::size_t>(edge.rigidBodyAIndex)];
        if (std::find(neighborsA.begin(), neighborsA.end(), edge.rigidBodyBIndex) == neighborsA.end()) {
            neighborsA.push_back(edge.rigidBodyBIndex);
        }

        std::vector<std::int32_t>& neighborsB = adjacency[static_cast<std::size_t>(edge.rigidBodyBIndex)];
        if (std::find(neighborsB.begin(), neighborsB.end(), edge.rigidBodyAIndex) == neighborsB.end()) {
            neighborsB.push_back(edge.rigidBodyAIndex);
        }
    }

    return adjacency;
}

std::vector<std::int32_t> SelectRigidBodiesByGroup(const std::vector<std::uint8_t>& groups, std::int32_t seedIndex)
{
    if (seedIndex < 0 || static_cast<std::size_t>(seedIndex) >= groups.size()) {
        return {};
    }

    const std::uint8_t targetGroup = groups[static_cast<std::size_t>(seedIndex)];
    std::vector<std::int32_t> result;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (groups[i] == targetGroup) {
            result.push_back(static_cast<std::int32_t>(i));
        }
    }
    return result; // Already ascending - built by iterating i in order.
}

std::vector<std::int32_t> SelectRigidBodyBranch(
    const std::vector<std::vector<std::int32_t>>& adjacency, std::int32_t seedIndex)
{
    if (seedIndex < 0 || static_cast<std::size_t>(seedIndex) >= adjacency.size()) {
        return {};
    }

    constexpr std::size_t kBranchDegree = 3;
    const auto isBranch = [&adjacency](std::int32_t node) {
        return adjacency[static_cast<std::size_t>(node)].size() >= kBranchDegree;
    };

    if (isBranch(seedIndex)) {
        // No single unambiguous chain to walk from a junction - see this
        // function's own doc comment in RigidBodyGroupSelection.h.
        return { seedIndex };
    }

    std::vector<char> visited(adjacency.size(), 0);
    std::vector<std::int32_t> result;
    std::vector<std::int32_t> pending{ seedIndex };
    visited[static_cast<std::size_t>(seedIndex)] = 1;

    while (!pending.empty()) {
        const std::int32_t current = pending.back();
        pending.pop_back();
        result.push_back(current);

        for (const std::int32_t neighbor : adjacency[static_cast<std::size_t>(current)]) {
            if (neighbor < 0 || static_cast<std::size_t>(neighbor) >= adjacency.size()) {
                continue; // Defensive - BuildRigidBodyAdjacency() never emits these, but never trust blindly.
            }
            if (visited[static_cast<std::size_t>(neighbor)]) {
                continue;
            }
            visited[static_cast<std::size_t>(neighbor)] = 1;
            if (isBranch(neighbor)) {
                // A branch boundary: never added to `result`, and never
                // expanded through - marking it visited still prevents
                // re-examining it from a different direction, without ever
                // walking PAST it.
                continue;
            }
            pending.push_back(neighbor);
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

} // namespace gte
