#include "EditorHostServices.h"

#include "../Core/LogSink.h"

namespace gte {

void EditorHostServices::Log(LogLevel level, std::string_view message)
{
    LogToActiveSink(level, "Core", message);
}

} // namespace gte
