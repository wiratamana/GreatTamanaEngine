#include "DeviceIdleGuard.h"

#include "Renderer.h"

namespace gte {

DeviceIdleGuard::~DeviceIdleGuard()
{
    m_renderer->IdleForTeardown("DeviceIdleGuard");
}

} // namespace gte
