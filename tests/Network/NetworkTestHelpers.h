#pragma once

// Shared test-only helper - network-impl-2 campaign, Phase 6
// (PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md, Step 3.1). Extracted
// out of tests/Network/NetworkServerTests.cpp (which originated this exact
// pattern) so tests/Network/CaptureEndpointsEndToEndTests.cpp can reuse it
// verbatim instead of copy-pasting a second copy, per the phase document's
// own instruction.
//
// Deliberately a plain header-only `inline` function, not a .cpp - each
// including translation unit gets its own (identical) definition, which is
// perfectly legal for an `inline` function and avoids needing a new tiny
// static library/target just for one helper.

#include "Network/NetworkServer.h"

#include <httplib.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace gte::Network::TestHelpers {

// Polls IsRunning() briefly rather than assuming Start() has already fully
// spun up its background thread by the time this line runs - Start() itself
// binds synchronously (so BoundPort() is already correct the instant
// Start() returns - see NetworkServer.cpp's own comment), but the
// background thread's own httplib::Server::listen_after_bind() call still
// needs a moment to actually reach its accept() loop before a client
// connection is guaranteed to succeed. A tight, short poll loop (never a
// fixed sleep) keeps this test fast on a healthy machine while still being
// robust under CI scheduling jitter.
inline void WaitUntilAcceptingConnections(NetworkServer& server, httplib::Client& client)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (auto res = client.Get("/http_hello_world"); res && res->status == 200) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FAIL() << "NetworkServer never started accepting connections on port " << server.BoundPort();
}

} // namespace gte::Network::TestHelpers
