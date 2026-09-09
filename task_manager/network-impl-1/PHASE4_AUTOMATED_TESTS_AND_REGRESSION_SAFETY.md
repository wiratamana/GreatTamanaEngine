# PHASE4 — Automated Tests & Regression Safety

Parent: `PHASE0_MASTER_STRATEGY.md`. Prerequisite: Phases 1-3 done (the whole
feature builds and works manually per Phase 3's own verification step, and
`NetworkServer::Start()` takes ONLY an `int port` parameter — no `host` — per
Phase 2's own locked signature).

## Step 1: The Goal

Give this campaign real, automated, `ctest`-visible proof that:

1. The pure route logic (`HandleHelloWorld()`) returns the exact contracted
   string — a trivial Tier-1 test, but still a real regression guard against
   a future accidental edit (e.g. someone "fixing" a typo and silently
   changing the response body).
2. The FULL, real path — a real `NetworkServer`, a real background thread, a
   real bound TCP socket, a real HTTP client request over the loopback
   interface, a real cpp-httplib route dispatch — actually returns `hello
   world` for `GET /http_hello_world`, end to end, with no manual
   browser/curl step required ever again.
3. The documented "a bind failure is non-fatal, never crashes" contract
   (`AGENTS.md`, "Networking"; Phase 2's `NetworkServer.cpp`) is itself
   under regression coverage, not just a manually-checked claim — see 3.2's
   new `BindFailureIsNonFatalAndLeavesServerNotRunning` test below, which an
   earlier draft of this phase document left uncovered.

This closes the loop `PHASE0_MASTER_STRATEGY.md`'s Definition of Done
requires: `ctest` passing is what proves this feature keeps working as the
engine evolves, not a one-time manual check.

## Step 2: The Situation

- `tests/CMakeLists.txt` lists every test source file in one flat
  `GTE_TEST_SOURCES` variable (see its own extensive per-file comment
  block), with a small number of `if(GTE_ENABLE_EDITOR)`/
  `if(GTE_ENABLE_PROJECT_PANEL)` conditional appends for Editor-only files.
  There is no `if(GTE_ENABLE_NETWORK)` conditional needed for this
  campaign's own tests — per `PHASE0_MASTER_STRATEGY.md`'s locked decision
  #6/`AGENTS.md`'s new "Networking" section, `NetworkServer`/`NetworkRoutes`
  ALWAYS compile regardless of the switch (only `Application`'s own
  production call site is gated) — so both new test files below are always
  built and always run, exactly like `Jobs/JobSystemTests.cpp` already does
  for `GTE_ENABLE_JOB_SYSTEM`. The insertion point (immediately after
  `Jobs/JobSystemSdlClockThreadSafetyTests.cpp`) was confirmed against the
  actual current file during this campaign's 2nd-iteration strategy review —
  that line is still present and still the last `Jobs/` entry.
- cpp-httplib (the same vendored `third_party/httplib/httplib.h`, currently
  staged at tag `v0.54.1`) also provides `httplib::Client` — a small HTTP
  client class, usable directly from a test with no extra dependency:
  `httplib::Client cli("127.0.0.1", port); auto res = cli.Get("/http_hello_world");`
  returns a `httplib::Result` (pointer-like; check truthiness, then
  `res->status` / `res->body`). **Every method used below
  (`Client(host, port)`, `Get(path)`, `set_connection_timeout(sec, usec)`,
  `Server::Get`/`bind_to_port`/`bind_to_any_port`/`listen_after_bind`/
  `stop`/`is_running`) was directly verified against the actually-vendored
  header during this campaign's 2nd-iteration strategy review and matches
  exactly** — re-verify only if `HTTPLIB_RELEASE_TAG` is ever bumped away
  from `v0.54.1` on a fresh fetch.
- `GreatTamanaEngineTests` already links the `gte_core` target (which
  already links `httplib` `PUBLIC`), so `#include <httplib.h>` is already
  available to any new test file with zero additional CMake wiring beyond
  adding the `.cpp` to `GTE_TEST_SOURCES`.
- `NetworkServer::Start()` accepts `port == 0` for exactly this situation
  (Phase 2's own design) — the test must use this, NOT a hardcoded port
  like `8080`, so it can never collide with a real running instance of the
  engine (or a previous, still-shutting-down test run) on the same
  developer machine or CI runner. `Start()` takes ONLY this one `int port`
  parameter — there is no `host` argument to pass (see Phase 2's locked
  signature); every call below reflects that.

## Step 3: The Plan

### 3.1 — Create `tests/Network/NetworkRoutesTests.cpp`

```cpp
#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>

namespace {

TEST(NetworkRoutesTests, HandleHelloWorldReturnsExactContractedString)
{
    EXPECT_EQ(gte::Network::HandleHelloWorld(), "hello world");
}

} // namespace
```

(Match this repository's existing convention of a small, focused test file
per pure-logic header — see e.g. `tests/Renderer/DrawStatsTests.cpp` for the
house style on a similarly small module.)

### 3.2 — Create `tests/Network/NetworkServerTests.cpp`

```cpp
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
    // Occupy a real, known port first via an ordinary ephemeral-port Start()
    // - this can never collide with anything else on the machine, so this
    // test needs no hardcoded port number at all.
    gte::Network::NetworkServer occupant;
    occupant.Start(0);
    ASSERT_TRUE(occupant.IsRunning());
    const int occupiedPort = occupant.BoundPort();
    ASSERT_GT(occupiedPort, 0);

    // A second, independent NetworkServer deliberately tries to bind to the
    // EXACT SAME port `occupant` is already holding - this is the automated
    // regression proof for the "a bind failure is logged and non-fatal,
    // never crashes, IsRunning()/BoundPort() stay at their false/0 defaults"
    // contract documented in AGENTS.md ("Networking") and NetworkServer.cpp
    // - previously only checked by manual inspection/a code comment, never
    // by an actual test.
    gte::Network::NetworkServer collider;
    collider.Start(occupiedPort);
    EXPECT_FALSE(collider.IsRunning());
    EXPECT_EQ(collider.BoundPort(), 0);

    // Both must still shut down cleanly - `collider` never actually started
    // a background thread (its own Stop() must therefore be a safe no-op,
    // exactly like StopIsSafeToCallWithoutAPriorStart above), and `occupant`
    // must still be a perfectly healthy, independently-working server.
    collider.Stop();
    EXPECT_FALSE(collider.IsRunning());

    httplib::Client client("127.0.0.1", occupiedPort);
    WaitUntilAcceptingConnections(occupant, client);
    const httplib::Result res = client.Get("/http_hello_world");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    occupant.Stop();
}

} // namespace
```

Notes for whoever implements this:

- If `httplib::Client`'s exact connection-timeout setter name/signature
  differs from the sketch above once checked against the actually-vendored
  `third_party/httplib/httplib.h` (recall `HTTPLIB_RELEASE_TAG` defaults to
  `"latest"`, so the exact vendored version can drift over time from the
  `v0.54.1` this was verified against) — adjust to match the real API; the
  important behavior is "don't let a failed-connection test hang the whole
  suite waiting on OS-default TCP timeouts".
- `DestructorStopsTheServerWithoutHanging` is the single most important test
  in this file — it is the concrete, automated proof of Phase 2's own
  "no detached thread, no hang" RAII requirement, which is otherwise only
  checked by inspection.
- `BindFailureIsNonFatalAndLeavesServerNotRunning` is what closes this
  phase's own new coverage gap (see Step 1, point 3) — every other test in
  this file exercises the HAPPY path; this is the one that proves the
  documented failure path actually behaves as documented, automatically, on
  every future change to `NetworkServer.cpp`.
- These five tests together are NOT Tier 1 in the strict sense `tests/
  CMakeLists.txt`'s own taxonomy defines (they open a real OS socket and
  spawn a real thread) — but they need no GPU/live Vulkan device/SDL video
  subsystem at all, so they still belong in the main, always-run
  `GreatTamanaEngineTests` binary alongside e.g. `Jobs/JobSystemTests.cpp`
  (which is in the same boat: real `std::thread`s, no GPU) rather than a
  separate Tier-2 GPU-gated bucket. Add a short note to `tests/CMakeLists.txt`'s
  own file-level comment block (next to the `Jobs/` entries) describing
  this file the same way every other entry is described, for consistency —
  including a one-line mention of `BindFailureIsNonFatalAndLeavesServerNotRunning`
  alongside `DestructorStopsTheServerWithoutHanging` as the two
  correctness-critical (not merely happy-path) cases in this file.

### 3.3 — `tests/CMakeLists.txt` wiring

Add both new files to `GTE_TEST_SOURCES` (unconditionally — no
`if(GTE_ENABLE_NETWORK)` guard, per Step 2 above), e.g. right after the
existing `Jobs/JobSystemSdlClockThreadSafetyTests.cpp` line:

```cmake
    Network/NetworkRoutesTests.cpp
    Network/NetworkServerTests.cpp
```

Also add a short descriptive comment for these two files into the big
file-level "Test taxonomy" comment block at the top of `tests/CMakeLists.txt`,
matching the style/detail level of every existing entry (see e.g. the
`Jobs/JobSystemTests.cpp` entry immediately above where these two are
inserted, for the level of detail expected).

### 3.4 — Verify (this phase's — and the whole campaign's — Definition of Done)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

- Confirm both new test files' cases appear individually in `ctest`'s output
  (via `gtest_discover_tests()`, already wired at the bottom of `tests/
  CMakeLists.txt` — no changes needed there) and all pass.
- Re-run the full suite at least twice in a row (or with
  `--gtest_repeat=5` against just the new `NetworkServerTests` binary
  filter, e.g. `GreatTamanaEngineTests.exe --gtest_filter=NetworkServerTests.*
  --gtest_repeat=5`) to catch any port-reuse/timing flakiness before calling
  this done — this mirrors the Job System campaign's own explicit "a single
  green run is not sufficient evidence for genuinely concurrent code"
  discipline (see `AGENTS.md`, "Job System"). Pay particular attention to
  `BindFailureIsNonFatalAndLeavesServerNotRunning` under repeat — it is the
  one test in this file relying on two independent sockets/ports being
  coordinated correctly, so it is the most likely of the five to expose any
  latent timing assumption if one exists.
- Confirm a full clean build ALSO succeeds with `-DGTE_ENABLE_NETWORK=OFF`
  and that `ctest` STILL passes identically in that configuration (these
  tests construct `NetworkServer` directly — they never go through
  `Application` — so they must be completely unaffected by the switch,
  proving the "class always compiles/behaves identically regardless of the
  toggle" property this whole campaign's design depends on).
- Once green, this satisfies `PHASE0_MASTER_STRATEGY.md`'s full campaign
  Definition of Done — write this phase's own completion report (and, if
  the working agreement for this task manager folder calls for one, a
  campaign-level completion summary cross-referencing all four phases,
  mirroring `task_manager/scene-serialization-1/`'s own per-phase completion
  report convention) into this same `task_manager/network-impl-1/` folder.
