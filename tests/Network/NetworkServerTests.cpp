#include "Network/NetworkServer.h"

#include <httplib.h>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

// Polls IsRunning() briefly rather than assuming Start() has already fully
// spun up its background thread by the time this line runs - Start() itself
// binds synchronously (so BoundPort() is already correct the instant
// Start() returns - see NetworkServer.cpp's own comment), but the
// background thread's own httplib::Server::listen_after_bind() call still
// needs a moment to actually reach its accept() loop before a client
// connection is guaranteed to succeed. A tight, short poll loop (never a
// fixed sleep) keeps this test fast on a healthy machine while still being
// robust under CI scheduling jitter.
void WaitUntilAcceptingConnections(gte::Network::NetworkServer& server, httplib::Client& client)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (auto res = client.Get("/http_hello_world"); res && res->status == 200) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FAIL() << "NetworkServer never started accepting connections on port " << server.BoundPort();
}

// RAII wrapper around a raw TCP listening socket bound EXCLUSIVELY to
// 127.0.0.1:port (Windows' SO_EXCLUSIVEADDRUSE), used solely by
// BindFailureIsNonFatalAndLeavesServerNotRunning below to deterministically
// reproduce a genuine "port already in use" bind failure for
// NetworkServer::Start() to hit.
//
// A second, independent gte::Network::NetworkServer was tried here FIRST (as
// PHASE4_AUTOMATED_TESTS_AND_REGRESSION_SAFETY.md's own strategy sketch
// assumed) but does NOT reproduce a bind failure on Windows: httplib's own
// default_socket_options() sets SO_REUSEADDR on every socket it creates (see
// third_party/httplib/httplib.h), and Windows' SO_REUSEADDR is notoriously
// permissive - a SECOND socket that also sets SO_REUSEADDR can successfully
// bind to an address:port ALREADY bound and actively listening by a first
// SO_REUSEADDR socket (a well-known Windows-specific "port hijacking" quirk;
// this does NOT reproduce the reliable EADDRINUSE failure the exact same
// scenario produces on Linux/macOS). Confirmed directly against this
// repository's real NetworkServer.cpp (whose bind_to_port() call goes
// through httplib's default socket options) - two real NetworkServer
// instances both ended up IsRunning() == true on the same port instead of
// the second one failing. SO_EXCLUSIVEADDRUSE is the one option strong
// enough to block ANY later bind to the same address:port regardless of the
// later socket's own SO_REUSEADDR setting, which is exactly what's needed to
// make this test deterministic.
class ExclusivePortOccupant {
public:
    explicit ExclusivePortOccupant(int port)
    {
#ifdef _WIN32
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_socket == INVALID_SOCKET) {
            return;
        }

        BOOL exclusive = TRUE;
        setsockopt(m_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return;
        }
        if (listen(m_socket, 1) != 0) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
#else
        (void)port;
#endif
    }

    ~ExclusivePortOccupant()
    {
#ifdef _WIN32
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
        }
#endif
    }

    ExclusivePortOccupant(const ExclusivePortOccupant&) = delete;
    ExclusivePortOccupant& operator=(const ExclusivePortOccupant&) = delete;

    bool IsValid() const noexcept
    {
#ifdef _WIN32
        return m_socket != INVALID_SOCKET;
#else
        return false;
#endif
    }

private:
#ifdef _WIN32
    SOCKET m_socket = INVALID_SOCKET;
#endif
};

TEST(NetworkServerTests, HelloWorldEndpointRespondsOverARealSocket)
{
    gte::Network::NetworkServer server;
    server.Start(0); // 0 = OS-assigned ephemeral port - see file header comment.
    ASSERT_TRUE(server.IsRunning());
    ASSERT_GT(server.BoundPort(), 0);

    httplib::Client client("127.0.0.1", server.BoundPort());
    WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Get("/http_hello_world");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "hello world");

    server.Stop();
    EXPECT_FALSE(server.IsRunning());
}

TEST(NetworkServerTests, UnknownRouteFallsThroughToDefault404)
{
    gte::Network::NetworkServer server;
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());

    httplib::Client client("127.0.0.1", server.BoundPort());
    WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Get("/this_route_does_not_exist");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);

    server.Stop();
}

TEST(NetworkServerTests, StopIsSafeToCallWithoutAPriorStart)
{
    gte::Network::NetworkServer server;
    EXPECT_NO_FATAL_FAILURE(server.Stop());
    EXPECT_FALSE(server.IsRunning());
}

TEST(NetworkServerTests, DestructorStopsTheServerWithoutHanging)
{
    int boundPort = 0;
    {
        gte::Network::NetworkServer server;
        server.Start(0);
        ASSERT_TRUE(server.IsRunning());
        boundPort = server.BoundPort();
    } // NetworkServer destructor runs here - must join its thread and return
      // promptly, never hang (this is the actual regression this test
      // exists to catch - a destructor that hangs would hang this whole
      // test binary, which is itself a strong enough signal without needing
      // an explicit timeout assertion).

    httplib::Client client("127.0.0.1", boundPort);
    client.set_connection_timeout(1, 0); // 1 second
    const httplib::Result res = client.Get("/http_hello_world");
    // A closed server should refuse the connection outright (res is empty/
    // null) rather than still answering - proves Stop() actually released
    // the port, not just flipped an internal flag.
    EXPECT_TRUE(res == nullptr || res->status != 200);
}

TEST(NetworkServerTests, BindFailureIsNonFatalAndLeavesServerNotRunning)
{
    // Discover a real, currently-free ephemeral port via an ordinary,
    // throwaway NetworkServer - this can never collide with anything else on
    // the machine, so this test needs no hardcoded port number at all.
    // Stopped (and thus released) immediately afterward so
    // ExclusivePortOccupant below can rebind that exact port deterministically.
    int freePort = 0;
    {
        gte::Network::NetworkServer probe;
        probe.Start(0);
        ASSERT_TRUE(probe.IsRunning());
        freePort = probe.BoundPort();
        ASSERT_GT(freePort, 0);
    }

    {
        // Occupy that exact port EXCLUSIVELY - see ExclusivePortOccupant's
        // own comment for why this, rather than a second httplib-backed
        // NetworkServer, is what's actually needed to reproduce a genuine
        // bind failure on Windows.
        ExclusivePortOccupant occupant(freePort);
        ASSERT_TRUE(occupant.IsValid());

        // A real NetworkServer deliberately tries to bind to the EXACT SAME
        // port `occupant` is already holding exclusively - this is the
        // automated regression proof for the "a bind failure is logged and
        // non-fatal, never crashes, IsRunning()/BoundPort() stay at their
        // false/0 defaults" contract documented in AGENTS.md ("Networking")
        // and NetworkServer.cpp - previously only checked by manual
        // inspection/a code comment, never by an actual test.
        gte::Network::NetworkServer collider;
        collider.Start(freePort);
        EXPECT_FALSE(collider.IsRunning());
        EXPECT_EQ(collider.BoundPort(), 0);

        // Must still shut down cleanly - `collider` never actually started a
        // background thread (its own Stop() must therefore be a safe no-op,
        // exactly like StopIsSafeToCallWithoutAPriorStart above).
        collider.Stop();
        EXPECT_FALSE(collider.IsRunning());
    } // occupant released here - the port is free again.

    // A perfectly healthy, independent NetworkServer can still bind and
    // actually serve a request on that exact same port now that the occupant
    // is gone - proves the collider's failed bind attempt above left nothing
    // wedged/corrupted behind it.
    gte::Network::NetworkServer healthy;
    healthy.Start(freePort);
    ASSERT_TRUE(healthy.IsRunning());

    httplib::Client client("127.0.0.1", freePort);
    WaitUntilAcceptingConnections(healthy, client);
    const httplib::Result res = client.Get("/http_hello_world");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    healthy.Stop();
}

} // namespace
