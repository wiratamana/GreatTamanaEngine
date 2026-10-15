#include "OnceErrorLog.h"

#include "../../Core/Logging.h"

#include <string>

namespace gte::rg {

void OnceErrorLog::Error(std::string_view category, std::string_view text)
{
    std::string key;
    key.reserve(category.size() + 1 + text.size());
    key.append(category).push_back('\x1f');
    key.append(text);

    switch (m_seen.Note(key)) {
    case BoundedOnceSet<kCapacity>::Result::First:
        GTE_LOG_ERROR(category, text);
        break;
    case BoundedOnceSet<kCapacity>::Result::Overflow:
        GTE_LOG_ERROR(category, "Further render pass errors are suppressed");
        break;
    default:
        break;
    }
}

} // namespace gte::rg
