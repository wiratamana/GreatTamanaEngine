# PHASE1 — Network Module Foundation & Pure Route Logic

Parent: `PHASE0_MASTER_STRATEGY.md`. Read that file first — it locks the
design decisions (loopback-only, port 8080, `GTE_ENABLE_NETWORK` toggle,
`src/Network/` location, endpoint contract) this phase implements against.

## Step 1: The Goal

Create the `src/Network/` module skeleton and the ONE piece of genuinely
pure, engine-independent logic this whole campaign needs: a function that
computes the exact response body for the `http_hello_world` endpoint, with
**zero** dependency on `httplib.h`, sockets, or threads — so it is trivially
Tier-1-testable (see `TESTING.md`'s taxonomy) with no server ever started.
Also stand up the CMake plumbing (`GTE_ENABLE_NETWORK` option + compile
definition + `gte_core` source list) that every later phase builds on.

By the end of this phase, `gte_core` compiles a brand-new, always-compiled
module that does nothing observable yet (nothing calls it) — this is
intentional and mirrors how `src/Jobs/JobQueue.h/.cpp` (Job System Phase 1)
was introduced before anything used it.

## Step 2: The Situation

- `cmake/FetchHttplib.cmake` already defines the `httplib` INTERFACE target;
  the root `CMakeLists.txt` already `target_link_libraries(gte_core PUBLIC
  ... httplib)` (see that file's own comment block right above it). No
  further CMake fetch/link work is needed for the library itself.
- The root `CMakeLists.txt` already has three precedents for a toggleable
  module (`GTE_ENABLE_EDITOR`, `GTE_ENABLE_PROJECT_PANEL`,
  `GTE_ENABLE_PROFILER`, `GTE_ENABLE_JOB_SYSTEM` — search for
  `option(GTE_ENABLE_JOB_SYSTEM` around line 70-83) — each followed
  immediately by a `target_compile_definitions(gte_core PUBLIC
  GTE_ENABLE_XXX=$<BOOL:${GTE_ENABLE_XXX}>)` call later in the same file
  (around lines 553-591).
- `add_library(gte_core STATIC ...)`'s source list (root `CMakeLists.txt`,
  starting around line 200) ends its `src/Jobs/` block with
  `src/Jobs/JobContinuation.cpp` (line ~460) right before the closing `)` —
  this campaign's new files are appended there.
- No `src/Network/` folder exists yet.

## Step 3: The Plan

### 3.1 — Create `src/Network/NetworkRoutes.h`

```cpp
#pragma once

#include <string>

namespace gte::Network {

// Pure, httplib-independent route handler logic - Tier 1 testable (see
// tests/Network/NetworkRoutesTests.cpp), no live httplib::Server/socket/
// thread involved at all. NetworkServer.cpp (Phase 2) is only ever a thin
// wiring layer that calls these functions and forwards their result into
// httplib::Response::set_content() - it must never compose response text
// itself. Every future endpoint's own response-computation logic must be
// added here the same way, as its own small function, so it stays testable
// the same way.
//
// Returns the exact response body for GET /http_hello_world - see
// PHASE0_MASTER_STRATEGY.md's locked "Endpoint contract" for the exact
// expected bytes (no trailing newline).
std::string HandleHelloWorld();

} // namespace gte::Network
```

### 3.2 — Create `src/Network/NetworkRoutes.cpp`

```cpp
#include "NetworkRoutes.h"

namespace gte::Network {

std::string HandleHelloWorld()
{
    return "hello world";
}

} // namespace gte::Network
```

Deliberately trivial for this campaign's one endpoint — the point of this
file existing as its own translation unit (rather than inlined directly into
`NetworkServer.cpp`'s route registration lambda) is establishing the PATTERN
future endpoints follow, not because this one function is complex.

### 3.3 — Root `CMakeLists.txt` edits

**(a) Add the new option**, immediately after the existing
`option(GTE_ENABLE_JOB_SYSTEM ...)` block (the one ending around line 83,
right before `list(APPEND CMAKE_MODULE_PATH ...)`):

```cmake
# Master switch for the always-compiled Network module (src/Network/) - an
# embedded, loopback-only HTTP server (gte::Network::NetworkServer, built on
# the already-fetched cpp-httplib - see cmake/FetchHttplib.cmake) that lets an
# external tool/script send the engine a plain HTTP request without ever
# blocking Application::Run()'s own frame loop (see
# task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md). Same "class always
# compiles, only the production call site is gated" precedent as
# GTE_ENABLE_JOB_SYSTEM above: turning this OFF means Application never calls
# NetworkServer::Start() (so a release build never opens a socket at all, at
# zero cost), while src/Network/'s own classes/tests still compile and pass
# either way.
option(GTE_ENABLE_NETWORK "Compile in and auto-start the embedded, loopback-only HTTP server" ON)
```

**(b) Add the new source files** to `add_library(gte_core STATIC ...)`'s
source list, immediately after `src/Jobs/JobContinuation.cpp` (the last line
before the list's closing `)`):

```cmake
    src/Network/NetworkRoutes.h
    src/Network/NetworkRoutes.cpp
```

(Phase 2 appends `NetworkServer.h`/`.cpp` to this same block — don't forget
to re-open this exact spot then.)

**(c) Add the new compile definition**, immediately after the existing
`target_compile_definitions(gte_core PUBLIC
GTE_ENABLE_JOB_SYSTEM=$<BOOL:${GTE_ENABLE_JOB_SYSTEM}>)` line:

```cmake
# Same idea again - exposes GTE_ENABLE_NETWORK to C++ code as an actual 1/0
# preprocessor macro, needed by Application.cpp's own
# `#if GTE_ENABLE_NETWORK` guard around NetworkServer::Start() (see
# AGENTS.md, "Networking"). PUBLIC for the same reason as the three
# definitions above: GreatTamanaEngineTests must see the exact same macro
# value gte_core itself was built with.
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_NETWORK=$<BOOL:${GTE_ENABLE_NETWORK}>)
```

### 3.4 — Verify

- `cmake --build build` compiles `gte_core` with the two new files added
  (nothing includes them yet, so this is purely a "does it compile as its own
  translation unit" check — expect zero warnings, since the file has no
  external dependency at all).
- Re-run with `-DGTE_ENABLE_NETWORK=OFF` in a scratch/second build dir (or
  reconfigure) to confirm the option plumbing itself works (the macro is
  still defined as `0`, nothing yet reads it, so behavior is identical either
  way at this phase — Phase 3 is where the value first matters).
- Do **not** yet write `tests/Network/NetworkRoutesTests.cpp` in this phase —
  that's Phase 4's job, once the whole campaign's shape is settled — but it
  is fine (and expected) that `HandleHelloWorld()` is trivially callable and
  correct already; there is no reason to delay confirming it manually if
  useful during development.

### 3.5 — What this phase deliberately does NOT do

- Does not touch `httplib.h` at all (no `#include <httplib.h>` anywhere yet)
  — that only happens in Phase 2's `NetworkServer.cpp`, keeping this phase's
  files genuinely dependency-free.
- Does not touch `Application` — nothing is wired up to actually run yet.
- Does not add any test file yet (Phase 4).
