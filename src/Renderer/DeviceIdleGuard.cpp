#include "DeviceIdleGuard.h"

#include "Renderer.h"

namespace gte {

DeviceIdleGuard::~DeviceIdleGuard()
{
    // Teardown continues whatever the outcome: a failed wait means the device is lost.
    static_cast<void>(m_renderer->WaitForGpuIdle(BlockSite::Shutdown, "DeviceIdleGuard"));
}

} // namespace gte
