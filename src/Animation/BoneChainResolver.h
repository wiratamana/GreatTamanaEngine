#pragma once

#include "../Assets/SkeletonData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// Shared "walk a per-bone SINGLE-INDEX CHAIN (e.g. SkeletonData::Bone's own
// parentBoneIndex, or its appendBoneIndex), resolving each bone's value from
// its own chain-predecessor's ALREADY-RESOLVED value" primitive - the exact
// recursive, cycle-guarded shape SkeletonPose.cpp's ComputeSkinningMatrices()
// (walking Bone::parentBoneIndex, resolving a world Mat4) and
// AppendBoneSolver.cpp's ApplyAppendInheritance() (walking
// Bone::appendBoneIndex, resolving a BoneLocalOffset) each used to hand-roll
// independently, via two different styles (one via a memoized std::function
// lambda, one via a memoized plain recursive function) that could easily
// drift out of sync with each other - both now share this one implementation
// instead (see AGENTS.md/TODO.md history for this file's own origin).
//
// Two flavors, depending on whether the whole skeleton needs resolving at
// once (and can therefore be safely MEMOIZED - each bone computed at most
// once regardless of how many descendants need it) or only a single bone's
// value is needed FRESH every call (because the underlying pose data may
// have changed since the last query, which would make a memoized result
// stale):

// ResolveBoneChain() - resolves EVERY bone in `skeleton`, memoized, in one
// pass. `nextIndex(boneIndex)` returns the index this bone's own chain
// points to next (its parent, or its append source), or a negative/
// out-of-range/self value to mean "no predecessor - resolve against
// `rootValue` directly". `resolveNode(boneIndex, predecessorValue)` computes
// bone `boneIndex`'s own resolved value given its predecessor's
// (already-resolved) value. A cyclic chain (malformed data) degrades to
// `rootValue` for the node that closes the cycle, rather than recursing
// forever - the whole call still terminates and returns one value per bone.
//
// This is what SkeletonPose.cpp's ComputeSkinningMatrices() (a genuine
// "evaluate the whole skeleton's pose" operation, run once per frame) uses.
//
// IK solving (Animation/IkSolver.h) deliberately does NOT use this flavor -
// see ResolveSingleBoneChain() below for why.
template <typename T, typename NextIndexFn, typename ResolveFn>
std::vector<T> ResolveBoneChain(
    const SkeletonData& skeleton, const T& rootValue, NextIndexFn nextIndex, ResolveFn resolveNode)
{
    const std::size_t count = skeleton.bones.size();
    std::vector<T> resolved(count, rootValue);
    std::vector<std::uint8_t> state(count, 0); // 0 = unvisited, 1 = in-progress (cycle guard), 2 = done.

    // Local recursive helper (a small self-referencing struct, since a plain
    // lambda can't call itself without extra ceremony) - shared by every
    // bone's own top-level Resolve() call below via `resolved`/`state`
    // memoization, so each bone is ever actually computed once.
    struct Recur {
        const T& rootValue;
        NextIndexFn& nextIndex;
        ResolveFn& resolveNode;
        std::vector<T>& resolved;
        std::vector<std::uint8_t>& state;
        std::size_t count;

        T Resolve(std::size_t index)
        {
            if (state[index] == 2) {
                return resolved[index];
            }
            if (state[index] == 1) {
                // Cyclic chain (malformed data) - break the cycle instead of
                // recursing forever.
                return rootValue;
            }
            state[index] = 1;

            const std::int32_t next = nextIndex(index);
            T predecessor = rootValue;
            if (next >= 0 && static_cast<std::size_t>(next) < count && static_cast<std::size_t>(next) != index) {
                predecessor = Resolve(static_cast<std::size_t>(next));
            }

            T value = resolveNode(index, predecessor);
            resolved[index] = value;
            state[index] = 2;
            return value;
        }
    };

    Recur recur{ rootValue, nextIndex, resolveNode, resolved, state, count };
    for (std::size_t i = 0; i < count; ++i) {
        recur.Resolve(i);
    }
    return resolved;
}

// ResolveSingleBoneChain() - resolves exactly ONE bone's value, walking only
// its own chain up to the root, recomputed fresh EVERY call - deliberately
// NOT memoized/cached across calls (unlike ResolveBoneChain() above), which
// matters when the values `resolveNode`/`nextIndex` read can change BETWEEN
// successive calls (e.g. IkSolver.h's Cyclic-Coordinate-Descent loop mutates
// the very pose being queried, one link bone at a time, mid-solve - caching
// a bone's resolved world matrix across those calls would silently return a
// stale answer the moment an earlier link rotates). Still shares the exact
// same cycle-guard shape as ResolveBoneChain() above (a cyclic chain
// degrades to `rootValue` and the call still terminates), just scoped to one
// bone's own ancestry instead of the whole skeleton, and with a fresh
// per-call `visiting` marker instead of a persistent memo table.
template <typename T, typename NextIndexFn, typename ResolveFn>
T ResolveSingleBoneChain(const SkeletonData& skeleton, std::int32_t boneIndex, const T& rootValue,
    NextIndexFn nextIndex, ResolveFn resolveNode)
{
    const std::size_t count = skeleton.bones.size();
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= count) {
        return rootValue;
    }

    std::vector<std::uint8_t> visiting(count, 0);

    struct Recur {
        const T& rootValue;
        NextIndexFn& nextIndex;
        ResolveFn& resolveNode;
        std::vector<std::uint8_t>& visiting;
        std::size_t count;

        T Resolve(std::size_t index)
        {
            if (visiting[index] != 0) {
                return rootValue; // Cyclic chain - break the cycle instead of recursing forever.
            }
            visiting[index] = 1;

            const std::int32_t next = nextIndex(index);
            T predecessor = rootValue;
            if (next >= 0 && static_cast<std::size_t>(next) < count && static_cast<std::size_t>(next) != index) {
                predecessor = Resolve(static_cast<std::size_t>(next));
            }

            T value = resolveNode(index, predecessor);
            visiting[index] = 0; // Allow this bone to be visited again from a later, separate top-level query.
            return value;
        }
    };

    Recur recur{ rootValue, nextIndex, resolveNode, visiting, count };
    return recur.Resolve(static_cast<std::size_t>(boneIndex));
}

} // namespace gte
