#pragma once

#include <cstdint>

namespace gte::fx {

// Holds one value per (frame serial, owner). The producer runs once per key, even when it yields an invalid value.
template <class T>
class OncePerFrame {
public:
    template <class Produce>
    const T& Get(std::uint64_t serial, const void* owner, Produce&& produce)
    {
        if (!m_filled || m_serial != serial || m_owner != owner) {
            m_value = produce();
            m_serial = serial;
            m_owner = owner;
            m_filled = true;
        }
        return m_value;
    }

private:
    T m_value{};
    std::uint64_t m_serial = 0;
    const void* m_owner = nullptr;
    bool m_filled = false;
};

} // namespace gte::fx
