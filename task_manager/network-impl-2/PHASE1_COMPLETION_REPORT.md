# PHASE1 — Completion Report: Third-Party Dependency + Pure Encoding Utilities

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE1_THIRDPARTY_STB_IMAGE_WRITE_AND_PURE_ENCODING_UTILITIES.md`.

## Summary

Implemented exactly what the phase document specified: one new vendored
dependency (`stb_image_write.h`, the PNG encoder counterpart of the
already-vendored `stb_image.h` decoder) plus three new, always-compiled,
Vulkan-free, Tier-1-tested utility modules under a brand-new `src/Encoding/`
folder. No existing `src/Renderer/`, `src/Application/`, or `src/Network/`
file was touched, per the phase document's own "What NOT to do in this
phase" section.

## What was done

1. **`cmake/FetchSTBImageWrite.cmake`** (new file) — copied
   `cmake/FetchSTB.cmake`'s shape almost verbatim:
   - Downloads `stb_image_write.h` from
     `https://raw.githubusercontent.com/nothings/stb/<ref>/stb_image_write.h`
     into the SAME `third_party/stb/` directory `stb_image.h` already lives
     in (already gitignored via the existing `/third_party/stb/` entry in
     `.gitignore` — no `.gitignore` change needed).
   - Its own separate staleness marker, `third_party/stb/.gte_fetched_ref_write`.
   - New cache variables `STB_IMAGE_WRITE_RELEASE_TAG` (pinned to the exact
     same commit SHA already pinned for `stb_image.h`,
     `2c980bb59875b0d32144a71867fbdebb2f77cd20`) and
     `STB_IMAGE_WRITE_FORCE_REDOWNLOAD`.
   - Defines a new `stb_image_write` INTERFACE target (a deliberately
     separate target from `stb_image`, even though both point at the same
     include directory — keeps each vendored file's own CMake "ownership"
     explicit, per the phase document).
2. **Root `CMakeLists.txt` wiring**:
   - `include(FetchSTBImageWrite)` added right after the existing
     `include(FetchSTB)`.
   - `fetch_stb_image_write()` called right after the existing `fetch_stb()`
     call, with a matching header comment.
   - `stb_image_write` added to the existing
     `target_link_libraries(gte_core PUBLIC SDL3::SDL3 volk vma stb_image ...)`
     line, alongside `stb_image`.
   - The three new `src/Encoding/*.h/.cpp` pairs added to `gte_core`'s
     unconditional `target_sources(gte_core PRIVATE ...)` list (right after
     the existing `src/Network/` entries) — NOT wrapped in
     `#if GTE_ENABLE_NETWORK` or any other switch, per Locked Design
     Decision #6 in `PHASE0_MASTER_STRATEGY.md`.
3. **`src/Encoding/Base64.h/.cpp`** — `EncodeBase64(const std::uint8_t*, size_t)`
   plus a `std::vector<std::uint8_t>` convenience overload. Hand-rolled
   lookup-table encoder, standard RFC 4648 alphabet with `=`/`==` padding,
   exactly as specified.
4. **`src/Encoding/PixelConversion.h/.cpp`** —
   `ConvertBgraToRgbaInPlace(pixels, width, height)`, swapping R/B in place,
   safe no-op for a 0-sized/null buffer.
5. **`src/Encoding/PngEncoder.h/.cpp`** — `EncodeRgba8ToPng(rgba, width, height)`,
   using `stbi_write_png_to_func` (verified against the actually-fetched
   `third_party/stb/stb_image_write.h` that it calls its callback exactly
   once for the whole PNG in this version, but the callback appends
   regardless so it stays correct even if a future version chunks output
   differently) with an append-only callback into a
   `std::vector<std::uint8_t>*`. `PngEncoder.cpp` is the one translation
   unit defining `STB_IMAGE_WRITE_IMPLEMENTATION`, mirroring
   `src/Assets/StbImageImpl.cpp`'s existing `STB_IMAGE_IMPLEMENTATION`
   precedent. Throws `std::runtime_error` on non-positive
   width/height/null input or an underlying encode failure.
6. **Tests** (`tests/Encoding/Base64Tests.cpp`,
   `PixelConversionTests.cpp`, `PngEncoderTests.cpp`), added unconditionally
   to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` (not gated behind any
   switch, since none of these three modules are gated behind anything):
   - `Base64Tests.cpp`: empty input, the classic `"f"`/`"fo"`/`"foo"`/`"foobar"`
     known vectors, a 300-byte binary buffer round-tripped through a
     hand-written reference base64 *decoder* local to the test file, and an
     explicit padding-character-count check for the 0/1/2-remainder-byte
     cases.
   - `PixelConversionTests.cpp`: a hand-built 2x2 BGRA buffer with distinct
     R/G/B/A values per pixel asserting exact expected RGBA output, plus
     0x0/null-with-positive-dimensions degenerate cases and a 1x1 case.
   - `PngEncoderTests.cpp`: encodes a 4x4 four-quadrant solid-color RGBA8
     buffer and a 1x1 buffer, decodes each result back via the
     already-vendored `stb_image.h`'s `stbi_load_from_memory()` (no new
     `STB_IMAGE_IMPLEMENTATION` translation unit needed for tests — the one
     already compiled into `gte_core` via `src/Assets/StbImageImpl.cpp` is
     linked into `GreatTamanaEngineTests` already), and asserts
     width/height/pixel-bytes are identical to the original input; plus an
     invalid-dimensions-throws case.

## Deviations from the phase document

- **None of substance.** One micro-correction was needed in my own first
  draft of `Base64Tests.cpp`'s padding test (I had the "1 remainder byte"
  vs. "2 remainder byte" byte-counts swapped relative to which produces
  `"=="` vs. `"="` padding) — caught immediately by actually running the new
  tests before considering the phase done, and fixed in the same phase
  before this report was written. This was a test-authoring mistake on my
  part, not a gap in the phase document itself.
- The phase document's own "if not, add one, e.g.
  `tests/Encoding/StbImageImplementation.cpp`" contingency for
  `PngEncoderTests.cpp` needing its own `STB_IMAGE_IMPLEMENTATION` TU turned
  out to be unnecessary: `src/Assets/StbImageImpl.cpp` already compiles
  `STB_IMAGE_IMPLEMENTATION` unconditionally into `gte_core` (confirmed by
  reading the file directly), and `GreatTamanaEngineTests` already links
  `gte_core`, so `PngEncoderTests.cpp` only needed a plain
  `#include <stb_image.h>` with no implementation macro of its own. No new
  file was added for this.

## Compile/test verification performed

- `cmake -S . -B build` (with internet access, since `fetch_stb_image_write()`
  needed to download the new header for the first time) — succeeded,
  `stb_image_write.h` staged into `third_party/stb/` alongside `stb_image.h`.
- `cmake --build build --target gte_core` — succeeded, only the three new
  `Encoding/*.cpp` files needed recompiling.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (linked
  cleanly against the new `gte_core` + the three new test files).
- Ran the new tests directly:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=*Base64Test*:*PixelConversionTest*:*PngEncoderTest*`
  — **14/14 passed** after the padding-test fix above.
- Per this campaign's workflow rules, a full `ctest`/full rebuild was
  deliberately NOT run — only the targeted `gte_core`/`GreatTamanaEngineTests`
  builds above, plus the new tests specifically, as instructed for a
  non-final phase.

## What the next phase (PHASE2) should know

- `gte::Encoding::EncodeBase64()`, `ConvertBgraToRgbaInPlace()`, and
  `EncodeRgba8ToPng()` are ready to use from anywhere in the engine — all
  three are plain, Vulkan-free, always-compiled free functions in the
  `gte::Encoding` namespace, `#include "Encoding/Base64.h"` /
  `"Encoding/PixelConversion.h"` / `"Encoding/PngEncoder.h"` (paths are
  relative to `src/`, matching every other engine header include).
- No `GTE_ENABLE_NETWORK`/other CMake-switch dependency exists anywhere in
  this phase's new code — these three modules compile and are linked into
  `gte_core` unconditionally, in every build configuration.
- `stb_image_write` is now a real dependency string the build fetches on a
  clean checkout/clean `third_party/` — a fresh clone doing its very first
  configure will need internet access for this (same as every other
  `Fetch*.cmake` module already required).
- Nothing under `src/Application/`, `src/Renderer/`, or `src/Network/` was
  touched — Phase 2's `FrameCaptureBridge` work starts from a clean slate,
  exactly as the master strategy describes.
