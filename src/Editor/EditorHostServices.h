#pragma once

#include "../Core/IHostServices.h"

namespace gte {

// EditorHost's IHostServices adapter for Core. Log() forwards into the global log sink, which
// main() installs before anything else is built.
class EditorHostServices : public IHostServices {
public:
    void Log(LogLevel level, std::string_view message) override;
};

} // namespace gte
