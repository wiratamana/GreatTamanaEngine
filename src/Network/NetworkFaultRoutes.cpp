#include "NetworkFaultRoutes.h"

#if GTE_ENABLE_NETWORK

#include "NetworkRoutes.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <system_error>
#include <utility>
#include <string_view>

namespace gte::Network {

namespace {

// Whole-string unsigned decimal parse. Rejects empty text, signs, spaces and trailing text.
bool TryParseCount(const std::string& text, std::uint32_t& out)
{
    std::uint64_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end || value > UINT32_MAX) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

std::string KindListText()
{
    std::string text;
    for (std::size_t i = 0; i < kFaultKindCount; ++i) {
        text += (i == 0 ? "" : ", ");
        text += FaultKindName(static_cast<FaultKind>(i));
    }
    return text;
}

} // namespace

ParsedFaultQuery ParseFaultQuery(const std::string& kindText, const std::string& countText)
{
    ParsedFaultQuery result;
    if (kindText.empty() && countText.empty()) {
        result.valid = true;
        result.listOnly = true;
        return result;
    }
    const std::optional<FaultKind> kind = ParseFaultKind(kindText);
    if (!kind) {
        result.errorMessage = "missing or invalid query parameter: kind - must be one of " + KindListText();
        return result;
    }
    std::uint32_t count = 1;
    if (!countText.empty() && !TryParseCount(countText, count)) {
        result.errorMessage = "invalid query parameter: count - must be a whole number";
        return result;
    }
    if (count < 1 || count > kMaxFaultCount) {
        result.errorMessage = "invalid query parameter: count - must be in 1.." + std::to_string(kMaxFaultCount);
        return result;
    }
    result.valid = true;
    result.kind = *kind;
    result.count = count;
    return result;
}

FaultReply HandleFaultRequest(const std::string& kindText, const std::string& countText)
{
    const ParsedFaultQuery parsed = ParseFaultQuery(kindText, countText);
    if (!parsed.valid) {
        return { 400, BuildGenericErrorResponseJson(parsed.errorMessage) };
    }
    FaultInjection& faults = FaultInjection::Instance();
    nlohmann::json body = nlohmann::json::object();
    body["success"] = true;
    if (parsed.listOnly) {
        const std::array<std::uint32_t, kFaultKindCount> armed = faults.Snapshot();
        nlohmann::json counters = nlohmann::json::object();
        for (std::size_t i = 0; i < kFaultKindCount; ++i) {
            counters[std::string(FaultKindName(static_cast<FaultKind>(i)))] = armed[i];
        }
        body["armed"] = std::move(counters);
        return { 200, body.dump() };
    }
    faults.Arm(parsed.kind, parsed.count);
    body["armed"] = std::string(FaultKindName(parsed.kind));
    body["count"] = parsed.count;
    return { 200, body.dump() };
}

} // namespace gte::Network

#endif
