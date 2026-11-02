#include "PhysicsDebugRecorder.h"

#include <algorithm>

namespace gte {

namespace {

constexpr std::uint32_t kMaxWindowFrames = 600;
// Cap on window * joints so a long window on a big model stays bounded.
constexpr std::size_t kMaxRecordedJointFrames = 600000;

std::uint32_t WindowForJoints(std::uint32_t requestedWindow, std::size_t totalJoints) noexcept
{
    const std::size_t cap = kMaxRecordedJointFrames / std::max<std::size_t>(1, totalJoints);
    return static_cast<std::uint32_t>(std::clamp<std::size_t>(cap, 1, requestedWindow));
}

} // namespace

void PhysicsDebugRecorder::Configure(bool enabled, std::uint32_t windowFrames)
{
    const std::uint32_t window = std::clamp<std::uint32_t>(windowFrames, 1, kMaxWindowFrames);
    if (window != m_windowFrames || !enabled) {
        m_entities.clear();
    }
    m_windowFrames = window;
    m_enabled = enabled;
}

std::uint32_t PhysicsDebugRecorder::EffectiveWindowFrames(Entity entity) const
{
    const auto found = m_entities.find(entity);
    if (found == m_entities.end()) {
        return m_windowFrames;
    }
    std::size_t totalJoints = 0;
    for (const ChainRing& ring : found->second.chains) {
        if (!ring.slots.empty()) {
            totalJoints += ring.slots[ring.current].animated.size();
        }
    }
    return WindowForJoints(m_windowFrames, totalJoints);
}

void PhysicsDebugRecorder::BeginFrame(Entity entity, std::size_t chainCount, const std::vector<std::size_t>& jointCounts,
    float renderDeltaSeconds, int stepsThisFrame)
{
    if (!m_enabled || chainCount == 0) {
        return;
    }

    std::size_t totalJoints = 0;
    for (std::size_t c = 0; c < chainCount && c < jointCounts.size(); ++c) {
        totalJoints += jointCounts[c];
    }
    const std::uint32_t window = WindowForJoints(m_windowFrames, totalJoints);

    EntityRings& rings = m_entities[entity];
    if (rings.chains.size() != chainCount || rings.window != window) {
        rings.chains.assign(chainCount, ChainRing{});
        for (ChainRing& ring : rings.chains) {
            ring.slots.resize(window);
        }
        rings.window = window;
    }

    const std::uint16_t steps = static_cast<std::uint16_t>(std::clamp(stepsThisFrame, 0, 65535));
    for (std::size_t c = 0; c < chainCount; ++c) {
        ChainRing& ring = rings.chains[c];
        ring.current = ring.head;
        ring.head = (ring.head + 1) % ring.slots.size();
        ring.count = std::min(ring.count + 1, ring.slots.size());

        const std::size_t joints = c < jointCounts.size() ? jointCounts[c] : 0;
        ChainFrameSample& slot = ring.slots[ring.current];
        slot.renderDeltaSeconds = renderDeltaSeconds;
        slot.rootAnimWorld = Vec3::Zero();
        slot.anchorWorld = Mat4::Identity();
        slot.animated.resize(joints);
        slot.display.resize(joints);
        slot.fk.resize(joints);
        slot.stepsThisFrame = steps;
        slot.collisionPushedParticles = 0;
        slot.collisionPushDistanceSum = 0.0f;
        slot.bendClampedJoints = 0;
        slot.reseedCount = 0;
        slot.nanGuardHits = 0;
    }
}

ChainFrameSample* PhysicsDebugRecorder::SlotFor(Entity entity, std::size_t chainIndex)
{
    if (!m_enabled) {
        return nullptr;
    }
    // Lookup only: workers call this for their own chain while nothing mutates the map.
    const auto found = m_entities.find(entity);
    if (found == m_entities.end() || chainIndex >= found->second.chains.size()) {
        return nullptr;
    }
    ChainRing& ring = found->second.chains[chainIndex];
    return ring.slots.empty() ? nullptr : &ring.slots[ring.current];
}

std::vector<const ChainFrameSample*> PhysicsDebugRecorder::Frames(Entity entity, std::size_t chainIndex) const
{
    std::vector<const ChainFrameSample*> frames;
    const auto found = m_entities.find(entity);
    if (found == m_entities.end() || chainIndex >= found->second.chains.size()) {
        return frames;
    }
    const ChainRing& ring = found->second.chains[chainIndex];
    const std::size_t size = ring.slots.size();
    if (size == 0) {
        return frames;
    }
    frames.reserve(ring.count);
    const std::size_t oldest = (ring.head + size - ring.count) % size;
    for (std::size_t k = 0; k < ring.count; ++k) {
        frames.push_back(&ring.slots[(oldest + k) % size]);
    }
    return frames;
}

std::uint32_t PhysicsDebugRecorder::FramesRecorded(Entity entity) const
{
    const auto found = m_entities.find(entity);
    if (found == m_entities.end() || found->second.chains.empty()) {
        return 0;
    }
    return static_cast<std::uint32_t>(found->second.chains.front().count);
}

void PhysicsDebugRecorder::Forget(Entity entity)
{
    m_entities.erase(entity);
}

void PhysicsDebugRecorder::Clear()
{
    m_entities.clear();
}

} // namespace gte
