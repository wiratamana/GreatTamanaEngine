#include "OnceErrorLog.h"

#include "../../Core/Logging.h"

#include <string>

namespace gte::rg {

void OnceErrorLog::Log(bool warning, std::string_view category, std::string_view text)
{
    std::string key;
    key.reserve(category.size() + 2 + text.size());
    key.append(category).push_back('\x1f');
    key.push_back(warning ? 'W' : 'E');
    key.append(text);
    switch (m_seen.Note(key)) {
    case BoundedOnceSet<kCapacity>::Result::First:
        if (warning) {
            GTE_LOG_WARNING(category, text);
        } else {
            GTE_LOG_ERROR(category, text);
        }
        break;
    case BoundedOnceSet<kCapacity>::Result::Overflow:
        GTE_LOG_ERROR(category, "Further render pass errors are suppressed");
        break;
    default:
        break;
    }
}

} // namespace gte::rg
