# PHASE1 — Third-Party Dependency + Pure Encoding Utilities

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first — locked design decisions
and non-goals apply here too).

## Step 1: The Goal (Where are we going?)

Give the engine three small, always-compiled, **Vulkan-free**, Tier-1-
testable utility modules that every later phase builds on, plus the one new
vendored dependency they need:

1. `cmake/FetchSTBImageWrite.cmake` — fetches `stb_image_write.h` (the PNG
   *encoder*, single-header, public domain, from the same `nothings/stb`
   repo `stb_image.h` already comes from) and defines an INTERFACE CMake
   target for it.
2. `src/Encoding/Base64.h/.cpp` — `EncodeBase64(bytes)`: turns an arbitrary
   byte buffer into standard base64 text.
3. `src/Encoding/PixelConversion.h/.cpp` — `ConvertBgraToRgbaInPlace(pixels,
   width, height)`: swaps the R/B channels of a tightly-packed 8-bit-per-
   channel pixel buffer.
4. `src/Encoding/PngEncoder.h/.cpp` — `EncodeRgba8ToPng(rgba, width,
   height)`: turns a tightly-packed RGBA8 pixel buffer into an in-memory PNG
   file (a `std::vector<std::uint8_t>`), using `stb_image_write.h`.

None of these four pieces touch a `VkDevice`, a live `Renderer`, or any
engine subsystem at all — by design, so they can be fully covered by
ordinary GoogleTest unit tests with no live GPU/window needed (see
`TESTING.md`'s Tier 1/Tier 2 split, and `AGENTS.md`'s "Testability &
Regression Safety" section). This mirrors this codebase's existing,
repeated pattern of building the pure data/math half of a Vulkan-adjacent
feature FIRST, fully independent of the live-device half that consumes it
later (`DrawStats.h` before `FrameRecorder`, `GpuTiming.h` before
`GpuTimingService`, `RenderGraphTypes.h` before the render graph's own
executor).

## Step 2: The Situation (Where are we now?)

- `third_party/stb/stb_image.h` already exists, fetched by
  `cmake/FetchSTB.cmake`, exposed as the `stb_image` INTERFACE CMake target
  (`third_party/stb` as an include directory), linked `PUBLIC` into
  `gte_core` in the root `CMakeLists.txt` (~line 627:
  `target_link_libraries(gte_core PUBLIC SDL3::SDL3 volk vma stb_image KTX::ktx httplib)`).
  Nothing in the engine `#define`s `STB_IMAGE_IMPLEMENTATION` anywhere yet
  either (that's a pre-existing gap, unrelated to this phase — the decoder
  simply isn't used by any real call site today; leave it alone).
- `stb_image_write.h` follows the **exact same** single-header, "you own the
  implementation .cpp" convention as `stb_image.h`/VMA
  (`#define STB_IMAGE_WRITE_IMPLEMENTATION` above exactly one `#include`,
  in exactly one translation unit) — `src/Encoding/PngEncoder.cpp` is that
  one translation unit for this campaign.
- No `src/Encoding/` folder exists yet — this phase creates it, as a new,
  top-level, always-compiled module living beside `src/Math/`, `src/Memory/`,
  `src/Profiling/` (i.e. NOT nested under `src/Renderer/` or `src/Network/`
  — it has no dependency on either, and both later phases consume it).
- No base64 encoder exists anywhere in this engine's own code (the only
  `base64_encode` in the whole repo is `httplib::detail::base64_encode`,
  buried in `third_party/httplib/httplib.h`'s own `detail` namespace — an
  internal implementation detail of a vendored library, not something this
  engine's own code should reach into, matching this codebase's general
  "roll it ourselves" philosophy already established for math/ECS/JSON).

## Step 3: The Plan

### 3.1 — `cmake/FetchSTBImageWrite.cmake`

Copy `cmake/FetchSTB.cmake` almost verbatim, adjusting only what must
differ:

- Downloads `https://raw.githubusercontent.com/nothings/stb/<ref>/stb_image_write.h`
  into `third_party/stb/stb_image_write.h` (the SAME `third_party/stb/`
  directory `stb_image.h` already lives in — these are companion files from
  the same upstream repo, no reason to split them into two directories).
- Its own staleness marker file: `third_party/stb/.gte_fetched_ref_write`
  (a SEPARATE marker from `stb_image.h`'s own `.gte_fetched_ref` — the two
  files can, in principle, be pinned to different commits if ever needed;
  don't conflate their staleness tracking).
- Its sanity-check content probe should look for the substring
  `"stb_image_write"` (mirroring the existing `_stb_download_and_stage()`'s
  own `"stb_image"` substring check for the decoder).
- New cache variable: `STB_IMAGE_WRITE_RELEASE_TAG` (pin to the SAME commit
  SHA already pinned in `cmake/FetchSTB.cmake`'s `STB_IMAGE_RELEASE_TAG`
  — `2c980bb59875b0d32144a71867fbdebb2f77cd20` — since both files live at
  that exact commit in the upstream repo; keeping them pinned to the same
  commit avoids any risk of a version mismatch between the two, even though
  they're fetched via two separate scripts). New cache variable:
  `STB_IMAGE_WRITE_FORCE_REDOWNLOAD`.
- Defines a new INTERFACE target, `stb_image_write`, pointing at the same
  `third_party/stb` include directory (a second, separate target is
  deliberate — even though the include directory is identical, this keeps
  each vendored file's own CMake "ownership" explicit and greppable, mirroring
  how `stb_image` itself is its own dedicated target rather than being
  folded into, say, a generic "third_party" catch-all).
- Exposes one function, `fetch_stb_image_write()`, called from the root
  `CMakeLists.txt` right next to the EXISTING `fetch_stb()` call (search for
  where that's invoked today) — add `include(cmake/FetchSTBImageWrite.cmake)`
  and `fetch_stb_image_write()` immediately after the existing
  `include(cmake/FetchSTB.cmake)` / `fetch_stb()` pair.
- Root `CMakeLists.txt`'s `target_link_libraries(gte_core PUBLIC ...)` line
  gains `stb_image_write` in the same list (~line 627, alongside the existing
  `stb_image`).

### 3.2 — `src/Encoding/Base64.h/.cpp`

```cpp
// src/Encoding/Base64.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gte::Encoding {

// Encodes `size` bytes at `data` as standard base64 text (RFC 4648,
// '+'/'/' alphabet with '=' padding - the same alphabet every common base64
// consumer, e.g. a browser's `atob()`/an LLM tool's own base64 image
// decoder, expects by default). Returns an empty string for size == 0.
std::string EncodeBase64(const std::uint8_t* data, std::size_t size);

// Convenience overload for a std::vector<uint8_t> (e.g. PngEncoder.h's own
// output) - the shape every real call site in this campaign actually has.
std::string EncodeBase64(const std::vector<std::uint8_t>& bytes);

} // namespace gte::Encoding
```

Implementation: a plain, hand-written lookup-table encoder (64-char alphabet
`ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/`),
processing 3 input bytes → 4 output chars at a time, with the standard
`=`/`==` padding for a final 1- or 2-byte remainder. No third-party
dependency needed — this is genuinely ~30 lines of code with no subtlety
worth pulling in a library for (`httplib`'s own vendored version is proof
this exact algorithm is small enough to hand-roll safely).

### 3.3 — `src/Encoding/PixelConversion.h/.cpp`

```cpp
// src/Encoding/PixelConversion.h
#pragma once
#include <cstdint>

namespace gte::Encoding {

// Swaps the R and B channels of a tightly-packed, row-major, 8-bit-per-
// channel BGRA buffer (width*height*4 bytes, no row padding) IN PLACE,
// turning it into RGBA - Green and Alpha are untouched. This exists because
// this engine's swapchain/RenderTexture color format is commonly
// VK_FORMAT_B8G8R8A8_UNORM (see RenderTexture.h's own default format,
// VulkanSwapchain.cpp's ChooseSurfaceFormat), while stb_image_write.h's
// stbi_write_png (see PngEncoder.h) unconditionally assumes its input is
// already in RGBA channel order - there is no "treat this as BGRA" flag
// anywhere in stb_image_write's own API to lean on instead.
//
// A caller whose source pixels are ALREADY in RGBA order (should this
// engine's negotiated swapchain format ever not be a *_B8G8R8A8_* variant
// on some future GPU/driver - see VulkanSwapchain.cpp's ChooseSurfaceFormat
// for exactly which formats it's willing to negotiate) must NOT call this
// function at all - see PHASE3/PHASE4's own call sites for how the caller
// decides whether a conversion is actually needed, based on the real
// VkFormat the captured RenderTexture/swapchain actually reports.
//
// width/height must be >= 0; a 0-sized buffer is a safe no-op.
void ConvertBgraToRgbaInPlace(std::uint8_t* pixels, int width, int height);

} // namespace gte::Encoding
```

Implementation: iterate `width*height` pixels, `std::swap(pixels[i*4+0],
pixels[i*4+2])` for each. Trivial, but still deserves its own Tier-1 test
(a handful of known-input/known-output cases, including a 0x0/1x1 edge case)
per `AGENTS.md`'s "every change to Tier 1 code must come with a matching
test" rule.

### 3.4 — `src/Encoding/PngEncoder.h/.cpp`

```cpp
// src/Encoding/PngEncoder.h
#pragma once
#include <cstdint>
#include <vector>

namespace gte::Encoding {

// Encodes a tightly-packed, row-major RGBA8 pixel buffer (width*height*4
// bytes, no row padding, R/G/B/A channel order - see PixelConversion.h if
// your source data is BGRA instead) into an in-memory PNG file, returned as
// a byte vector ready to write to disk or hand back as an HTTP response
// body. Throws std::runtime_error if the underlying stb_image_write call
// reports failure (in practice this should never happen for well-formed,
// correctly-sized RGBA8 input - stb_image_write's own failure modes are
// essentially "invalid parameters", which this function's own signature
// already prevents by construction).
//
// width/height must both be > 0.
std::vector<std::uint8_t> EncodeRgba8ToPng(const std::uint8_t* rgba, int width, int height);

} // namespace gte::Encoding
```

Implementation notes:

- `PngEncoder.cpp` is the ONE translation unit that does
  `#define STB_IMAGE_WRITE_IMPLEMENTATION` immediately above
  `#include "stb_image_write.h"` (mirroring VMA's own
  `VMA_IMPLEMENTATION`/`stb_image`'s own `STB_IMAGE_IMPLEMENTATION`
  precedent in this codebase — search for either to see the exact shape to
  copy).
- Use `stbi_write_png_to_func(callback, context, width, height, 4 /* RGBA
  channels */, rgba, width * 4 /* stride_in_bytes */)` — NOT
  `stbi_write_png(filename, ...)` — since this needs an in-memory result,
  never a file on disk. The callback signature is
  `void callback(void* context, void* data, int size)`; have `context` be a
  `std::vector<std::uint8_t>*` and have the callback `memcpy`/`insert` the
  given `size` bytes from `data` onto the end of it. `stbi_write_png_to_func`
  calls its callback exactly once for the whole encoded PNG in this library's
  actual implementation (verify this directly against the fetched
  `third_party/stb/stb_image_write.h` source before assuming it, the same
  "verify the vendored header's own real API before writing code against
  it" discipline `NetworkServer.cpp`'s own file comment already models for
  `httplib.h`) — but write the callback to simply *append* whatever it's
  given regardless, so it is correct even if a future stb_image_write
  version ever chunks its output across multiple calls.
- Return `std::runtime_error` if `stbi_write_png_to_func` returns `0`
  (failure, per its documented contract) OR if the accumulated output buffer
  ends up empty when it shouldn't be.

### 3.5 — CMake wiring

Add all three new `.h`/`.cpp` pairs to `gte_core`'s source list
(unconditional — NOT wrapped in `#if GTE_ENABLE_NETWORK`, per Locked Design
Decision #6 in `PHASE0_MASTER_STRATEGY.md`) in the root `CMakeLists.txt`,
right alongside where `src/Memory/`'s or `src/Profiling/`'s own sources are
listed (find the exact `target_sources(gte_core PRIVATE ...)` block(s) and
follow the existing folder-grouping convention exactly).

### 3.6 — Tests (Tier 1, GoogleTest, no live GPU/window)

Add `tests/Encoding/Base64Tests.cpp`, `tests/Encoding/PixelConversionTests.cpp`,
`tests/Encoding/PngEncoderTests.cpp` to `tests/CMakeLists.txt`'s
unconditionally-built source list (mirroring
`tests/Profiling/FrameProfilerTests.cpp`'s own "always built" bucket — see
`AGENTS.md`'s "Job System" section for the precedent quote: "Never gate a new
... test file behind `GTE_ENABLE_EDITOR`/`GTE_ENABLE_JOB_SYSTEM`... unless the
source it tests is itself gated" — none of this phase's three modules are
gated behind anything).

- `Base64Tests.cpp`: known-vector tests (empty input → `""`; `"f"` →
  `"Zg=="`; `"fo"` → `"Zm8="`; `"foo"` → `"Zm9v"`; a longer, non-ASCII-safe
  binary buffer round-tripped through a reference decoder if one is easy to
  hand-write, or at minimum checked byte-length/padding-character rules).
- `PixelConversionTests.cpp`: a small, hand-built 2x2 BGRA buffer with known
  distinct R/G/B/A values per pixel, asserting the exact expected RGBA output
  byte-for-byte; a 0x0/degenerate case that must not crash.
- `PngEncoderTests.cpp`: **round-trip via the ALREADY-vendored `stb_image.h`
  decoder** — encode a small, hand-built RGBA8 buffer (e.g. 4x4, a few
  distinct solid-color pixels) via `EncodeRgba8ToPng()`, then decode the
  resulting bytes back via `stbi_load_from_memory()` (`stb_image.h`,
  `desired_channels=4`), and assert the decoded width/height/pixel bytes
  are IDENTICAL to the original input. This is the single strongest
  regression test this phase can have — it proves the whole encode pipeline
  produces genuinely valid, correctly-shaped PNG data without needing any
  external tool or golden file. (Note: this test needs
  `#define STB_IMAGE_IMPLEMENTATION` somewhere reachable by the test binary —
  check whether `tests/` already has a translation unit doing this for
  `stb_image.h`'s decode side; if not, add one, e.g.
  `tests/Encoding/StbImageImplementation.cpp`, and link `stb_image` into the
  test target the same way `gte_core` already does.)

### What NOT to do in this phase

- Do not touch `src/Renderer/`, `src/Application/`, or `src/Network/` at all
  — this phase is 100% new, self-contained files plus CMake wiring.
- Do not add a JSON library — Phase 3/5's own hand-formatted JSON envelope
  needs nothing from this phase beyond `EncodeBase64()`.
- Do not attempt to wire any of these three modules into a real capture
  path yet — that starts in Phase 3.
