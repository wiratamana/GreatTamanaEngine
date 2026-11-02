#pragma once
#include "../../ECS/Entity.h"
#include "../../Physics/PhysicsDiagnostics.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gte {

// Ring buffer of raw per-frame chain samples for entities with a DynamicChainRig.
// Everything is main thread only, except SlotFor(): a job may call it for its OWN chain index.
class PhysicsDebugRecorder {
public:
    // windowFrames 1..600. Changing the window clears all rings. Rings are allocated lazily in BeginFrame().
    void Configure(bool enabled, std::uint32_t windowFrames);
    bool Enabled() const noexcept { return m_enabled; }
    std::uint32_t RequestedWindowFrames() const noexcept { return m_windowFrames; }

    // Window after the memory cap (windowFrames * totalJoints <= 600000) for this entity.
    std::uint32_t EffectiveWindowFrames(Entity entity) const;

    // Picks the next ring slot of every chain of `entity`, sizes its vectors (capacity is reused) and stores the deltas.
    void BeginFrame(Entity entity, std::size_t chainCount, const std::vector<std::size_t>& jointCounts,
        float renderDeltaSeconds, int stepsThisFrame);

    // The slot BeginFrame() prepared. Null when disabled or unknown.
    ChainFrameSample* SlotFor(Entity entity, std::size_t chainIndex);

    // Recorded frames, oldest first.
    std::vector<const ChainFrameSample*> Frames(Entity entity, std::size_t chainIndex) const;
    std::uint32_t FramesRecorded(Entity entity) const;

    void Forget(Entity entity);
    void Clear();

private:
    struct ChainRing {
        std::vector<ChainFrameSample> slots;
        std::size_t head = 0;  // next slot to write.
        std::size_t count = 0; // valid frames.
        std::size_t current = 0;
    };
    struct EntityRings {
        std::vector<ChainRing> chains;
        std::uint32_t window = 0;
    };

    bool m_enabled = false;
    std::uint32_t m_windowFrames = 120;
    std::unordered_map<Entity, EntityRings> m_entities;
};

} // namespace gte
