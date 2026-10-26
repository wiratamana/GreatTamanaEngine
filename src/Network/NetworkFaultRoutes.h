#pragma once

#include "../Core/FaultInjection.h"

#if GTE_ENABLE_NETWORK

#include <cstdint>
#include <string>

namespace gte::Network {

// Parsed query of GET /debug/fault. No kind and no count means "list the armed counters".
struct ParsedFaultQuery {
    bool valid = false;
    bool listOnly = false;
    std::string errorMessage;
    FaultKind kind = FaultKind::OffscreenThrow;
    std::uint32_t count = 0;
};

// `count` defaults to 1 when only a kind is given. It must be a whole number in 1..kMaxFaultCount.
ParsedFaultQuery ParseFaultQuery(const std::string& kindText, const std::string& countText);

struct FaultReply {
    int status = 200;
    std::string body;
};

// Arms a fault or lists the armed counters. Only touches atomics, so it answers while the main thread is stalled.
FaultReply HandleFaultRequest(const std::string& kindText, const std::string& countText);

} // namespace gte::Network

#endif
