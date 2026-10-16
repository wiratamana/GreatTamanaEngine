#pragma once

namespace gte {

class Renderer;

// Idles the GPU when destroyed. Declare it as the LAST member of an owner whose earlier members free GPU objects
// without waiting: members die in reverse order, so the guard runs first. The Renderer must outlive it.
class DeviceIdleGuard {
public:
    explicit DeviceIdleGuard(const Renderer& renderer) noexcept
        : m_renderer(&renderer)
    {
    }
    ~DeviceIdleGuard();

    DeviceIdleGuard(const DeviceIdleGuard&) = delete;
    DeviceIdleGuard& operator=(const DeviceIdleGuard&) = delete;

private:
    const Renderer* m_renderer;
};

} // namespace gte
