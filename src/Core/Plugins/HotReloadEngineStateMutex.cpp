#include "HotReloadEngineStateMutex.h"

namespace gte {

std::mutex& GetHotReloadEngineStateMutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace gte
