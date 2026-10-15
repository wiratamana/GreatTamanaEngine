#pragma once

#include <string_view>

#include "Logging.h"

namespace gte {

// Diagnostics hook injected into Core's constructor, so Core reports without depending on Editor or Network.
// Each host decides where the messages go (log sink, stdout, a file).
class IHostServices {
public:
    virtual ~IHostServices() = default;

    virtual void Log(LogLevel level, std::string_view message) = 0;
};

} // namespace gte
