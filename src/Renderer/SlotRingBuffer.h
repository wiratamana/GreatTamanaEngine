#pragma once

// A host-written buffer with one copy per frame slot, so the CPU never overwrites a copy a frame in flight reads.
// The writer keeps every copy current: Write() fills the copy of its slot, CatchUp() refills a stale one.

#include "Buffer.h"
#include "OffscreenRingGuard.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace gte {

// `Backing` is the copy type: Upload(data, size), Size() and MappedData(). Buffer is the real one.
template <class Backing>
class BasicSlotRingBuffer {
public:
    // At least one copy; every copy starts with the same content.
    explicit BasicSlotRingBuffer(std::vector<std::shared_ptr<Backing>> copies)
        : m_copies(std::move(copies))
        , m_state(m_copies.size())
    {
        assert(!m_copies.empty());
    }

    std::uint32_t Copies() const noexcept { return static_cast<std::uint32_t>(m_copies.size()); }

    // The copy of `slot`, folded into the copy count.
    Backing& Copy(std::uint32_t slot) const noexcept { return *m_copies[slot % m_copies.size()]; }

    // Replaces the whole content of the copy of `ticket`'s slot. False, nothing written, when a frame in flight
    // still reads that copy.
    bool Write(const OffscreenFrameTicket& ticket, const void* data, std::size_t size)
    {
        const std::uint32_t copy = RingCopyOf(ticket, Copies());
        if (!m_state[copy].use.Claim(ticket)) {
            return false;
        }
        Copy(copy).Upload(data, size);
        m_state[copy].version = ++m_latestVersion;
        m_latest = copy;
        return true;
    }

    // Brings the copy of `ticket`'s slot up to the newest content. A no-op when it already holds it. False when a
    // frame in flight still reads the copy. The writer calls this on frames that write nothing.
    bool CatchUp(const OffscreenFrameTicket& ticket)
    {
        const std::uint32_t copy = RingCopyOf(ticket, Copies());
        State& state = m_state[copy];
        if (state.version == m_latestVersion) {
            state.use.Note(ticket);
            return true;
        }
        if (!state.use.Claim(ticket)) {
            return false;
        }
        const Backing& newest = Copy(m_latest);
        Copy(copy).Upload(newest.MappedData(), static_cast<std::size_t>(newest.Size()));
        state.version = m_latestVersion;
        return true;
    }

    std::uint64_t LatestVersion() const noexcept { return m_latestVersion; }
    std::uint32_t LatestCopy() const noexcept { return m_latest; }
    std::uint64_t VersionOf(std::uint32_t slot) const noexcept { return m_state[slot % m_state.size()].version; }

private:
    struct State {
        std::uint64_t version = 0; // The write this copy holds; 0 = the content it was created with.
        RingCopyUse use;
    };

    std::vector<std::shared_ptr<Backing>> m_copies;
    std::vector<State> m_state;
    std::uint64_t m_latestVersion = 0;
    std::uint32_t m_latest = 0;
};

using SlotRingBuffer = BasicSlotRingBuffer<Buffer>;

} // namespace gte
