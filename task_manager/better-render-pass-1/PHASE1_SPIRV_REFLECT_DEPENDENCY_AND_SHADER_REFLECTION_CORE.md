# PHASE1 — SPIRV-Reflect Dependency + Shader Reflection Core

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first).

---

## Step 1 — The Goal

SPIRV-Reflect (Decision D4) is already vendored, built, and linked into `gte_core` — see Step
2.1, a satisfied precondition this phase inherits rather than performs. This phase's own,
remaining job is to build one new, pure, Tier-1-testable module,
`src/Renderer/Vulkan/ShaderReflection.h/.cpp`, that reads an already-compiled `.comp.spv` file and
returns:

1. Every descriptor binding declared in the shader: `(set, binding, VkDescriptorType, count,
   name)`.
2. The push-constant block's `(offset, size)`, if the shader declares one.
3. The declared `layout(local_size_x = X, local_size_y = Y, local_size_z = Z) in;` work-group
   size.

Nothing calls this module from a real pipeline yet (that is PHASE2's job) — this phase is pure,
additive infrastructure, zero behavior change anywhere in the engine. This phase also closes out
the dependency work by committing it, together with the new module, in one coherent commit (see
Step 3) — nothing about the dependency itself is still "in flight" or needs further CMake/vendoring
work here.

---

## Step 2 — The Situation

### 2.1 Precondition already satisfied: SPIRV-Reflect is vendored, built, and linked

SPIRV-Reflect (`https://github.com/KhronosGroup/SPIRV-Reflect`) is a small, purpose-built library
that only extracts reflection metadata from an already-compiled SPIR-V binary — exactly R2's
requirement (SPIRV-Cross, by contrast, is a full shader cross-compiler, far more than this engine
needs). It is already staged on disk and already wired into the build, exactly as follows:

- `third_party/spirv_reflect/` holds four real files: `spirv_reflect.h` (public header),
  `spirv_reflect.c` (the plain-C implementation, compiled as a real static library, not merely
  staged), `include/spirv/unified1/spirv.h` (the official SPIR-V enum/opcode header
  `spirv_reflect.h` itself `#include`s via a relative path — easy to miss, but required for
  `spirv_reflect.h` to even parse), and `LICENSE`. A `.gte_fetched_ref` marker file records exactly
  which ref is currently staged, mirroring every other `Fetch*.cmake` dependency's own convention.
- `cmake/FetchSpirvReflect.cmake` already exists, fully written, mirroring `cmake/FetchVulkan.cmake`'s
  own `volk` target shape most closely (a small, plain-C dependency fetched straight from GitHub and
  compiled as a genuine `STATIC` library) — not `cmake/FetchJson.cmake`/`FetchVMA.cmake`/
  `FetchHttplib.cmake`, which are header-only `INTERFACE` targets with nothing compiled at all. It
  defines `fetch_spirv_reflect()`, which stages the four files above (fetching them from GitHub's raw
  content endpoint only if not already present) and then defines the `spirv_reflect` `STATIC` library
  target (`add_library(spirv_reflect STATIC .../spirv_reflect.c ...)`,
  `target_include_directories(spirv_reflect PUBLIC .../third_party/spirv_reflect)`).
- Its tunable `SPIRV_REFLECT_RELEASE_TAG` cache variable currently defaults to the literal marker
  string `"local-vendored-from-SPIRV-Reflect-main.zip"` — recording, honestly, that the currently
  staged copy was vendored BY HAND from a locally-downloaded `SPIRV-Reflect-main.zip` (an offline
  setup, no network access used), rather than resolved/fetched through
  `_spirv_reflect_download_and_stage()`'s own GitHub-raw-content codepath. **This is the correct,
  accepted, FINAL state for this whole campaign — do not change `SPIRV_REFLECT_RELEASE_TAG` to a
  real git ref/resolved tag, and do not attempt to re-fetch from GitHub.** The file does keep a real,
  working git-ref-based fetch codepath (override the tag to a real commit SHA/tag plus set
  `SPIRV_REFLECT_FORCE_REDOWNLOAD ON`) available for a possible future need, but it is inactive today
  and genuinely out of scope for this phase to exercise.
- The root `CMakeLists.txt` already has `include(cmake/FetchSpirvReflect.cmake)` +
  `fetch_spirv_reflect()`, placed alongside the other `Fetch*.cmake`/`fetch_*()` call pairs, AND
  already has `target_link_libraries(gte_core PRIVATE spirv_reflect)` appended to the existing
  `gte_core` link-library call.
- `.gitignore` already carries its own `/third_party/spirv_reflect/` entry, matching every other
  vendored dependency's own dedicated block (`/third_party/volk/`, `/third_party/json/`, ...).

None of the five bullets above need any further edit by this phase — they are already correct and
already present in the working tree, just not yet committed (see Step 3, item 3). This phase's own
new work is strictly the `ShaderReflection` module plus its test, described from Step 2.2 onward.

### 2.2 SPIRV-Reflect's real API surface this module needs (confirmed directly against the real,
already-vendored `third_party/spirv_reflect/spirv_reflect.h` — not merely assumed from upstream
documentation)

- `spvReflectCreateShaderModule(size_t size, const void* code, SpvReflectShaderModule* module)` —
  the entry point; takes the raw SPIR-V binary bytes. Read the `.comp.spv` file into a plain
  `std::vector<char>` via a small, local, PRIVATE helper in `ShaderReflection.cpp` that MIRRORS
  (duplicates) `src/Renderer/Vulkan/ShaderModule.cpp`'s own anonymous-namespace `ReadFile()` — that
  function has internal linkage (declared nowhere in `ShaderModule.h`, so it cannot be called from a
  different translation unit), and `ShaderModule.h`'s only EXPORTED function, `LoadShaderModule(VkDevice,
  const std::string&)`, always takes a live `VkDevice` and always calls `vkCreateShaderModule` —
  incompatible with this module's own explicit design requirement (Step 2.3 below) of needing no
  `VkDevice` anywhere in this file at all. Write a second, small, private copy of that same
  `std::ifstream(..., std::ios::ate | std::ios::binary)` read-whole-file-into-a-vector idiom directly
  in `ShaderReflection.cpp` — do not attempt to export/reuse `ShaderModule.cpp`'s existing helper, and
  do not take on a `VkDevice` dependency this module must not have.
- `spvReflectEnumerateDescriptorBindings(module, &count, nullptr)` then again with a real
  `SpvReflectDescriptorBinding**` array (confirmed present at `spirv_reflect.h` line 771) — each
  entry has `.set`, `.binding`, `.descriptor_type` (an `SpvReflectDescriptorType` enum — map
  `SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER` → `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`,
  `..._STORAGE_IMAGE` → `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, `..._COMBINED_IMAGE_SAMPLER` →
  `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` — these three are the ONLY descriptor types this
  engine's own `ComputeDescriptorWrite`/`DescriptorSetLayoutBuilder` understand today per
  `ComputeDescriptorSet.h`'s own three static factories — a reflected binding of any OTHER type
  should be treated as an explicit, loud error (`throw std::runtime_error`), not silently ignored),
  `.count` (array size, almost always 1), `.name`.
- `spvReflectEnumerateEntryPoints`/`module.entry_points[0]` (or
  `spvReflectGetEntryPoint(module, "main")`) — carries `.local_size.x/.y/.z` (confirmed present at
  `spirv_reflect.h` line 584, the declared compute work-group size).
- `spvReflectEnumeratePushConstantBlocks(module, &count, nullptr)` then the real array (confirmed
  present at `spirv_reflect.h` line 1032) — each entry has `.offset`, `.size` (there should be at
  most one push-constant block per this engine's own established one-`layout(push_constant)`-block-
  per-shader convention — if a shader somehow declares more than one, throw `std::runtime_error`
  rather than silently picking the first).
- `spvReflectDestroyShaderModule(&module)` — MUST be called before returning, on every path
  (success or a thrown exception) — wrap in RAII (a small local guard/`std::unique_ptr` with a
  custom deleter, or a `try`/`catch`-and-rethrow block mirroring `ComputePipeline`'s own
  constructor shape) so a thrown "unsupported descriptor type" error never leaks the
  `SpvReflectShaderModule`'s internal allocations.

### 2.3 Where this file lives and its exact shape

New files: `src/Renderer/Vulkan/ShaderReflection.h` and `.cpp`. Deliberately lives under
`Vulkan/` (mirrors `DescriptorSetLayoutBuilder.h`'s own location — both are small, focused
Vulkan-adjacent helpers), and deliberately does NOT depend on `ComputePipeline.h`/
`GpuResourceFactory.h` (PHASE2's own job to wire this INTO those — keep this phase's own module
usable in complete isolation, with a live `VkDevice` needed nowhere in this file at all — pure
binary parsing only).

```cpp
// src/Renderer/Vulkan/ShaderReflection.h
#pragma once

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

struct ReflectedDescriptorBinding {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::uint32_t count = 1;
    std::string name;
};

struct ReflectedPushConstantRange {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct ReflectedLocalSize {
    std::uint32_t x = 1;
    std::uint32_t y = 1;
    std::uint32_t z = 1;
};

struct ShaderReflectionResult {
    std::vector<ReflectedDescriptorBinding> descriptorBindings;
    // std::nullopt-equivalent: emptiness is expressed via a bool, mirroring this codebase's
    // general preference for plain fields over std::optional where the file is meant to be a
    // simple aggregate — confirm against ShaderReflection.cpp's own real implementation which
    // style reads more naturally once written; std::optional<ReflectedPushConstantRange> is also
    // acceptable and arguably clearer (this engine already uses std::optional extensively
    // elsewhere in Renderer/ code) - pick ONE, document the choice in the completion report.
    bool hasPushConstantRange = false;
    ReflectedPushConstantRange pushConstantRange;
    ReflectedLocalSize localSize;
};

// Reads the compiled SPIR-V binary at `shaderSpirvPath` and returns its reflected descriptor
// bindings / push-constant range / declared compute local work-group size. Throws
// std::runtime_error if the file cannot be read, is not valid SPIR-V, or declares a descriptor
// type this engine's own DescriptorSetLayoutBuilder/ComputeDescriptorSet cannot express
// (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER / STORAGE_IMAGE / COMBINED_IMAGE_SAMPLER only - see
// ComputeDescriptorSet.h's own three static factories).
ShaderReflectionResult ReflectComputeShader(const std::string& shaderSpirvPath);

} // namespace gte
```

Implement `ReflectComputeShader()` in the matching `.cpp`, using the real vendored
`spirv_reflect.h`/`.c` per Step 2.2 above.

---

## Step 3 — The Plan

1. **Write `ShaderReflection.h/.cpp`** exactly as scoped in Step 2.3, mirroring (duplicating, NOT
   calling) `Vulkan/ShaderModule.cpp`'s own private `ReadFile()` helper to read the `.spv` bytes off
   disk as a small, local, self-contained copy in `ShaderReflection.cpp` — see Step 2.2 above for
   exactly why this must be a duplicate, not a literal reuse.
2. **Add a Tier-1 test**, `tests/Renderer/Vulkan/ShaderReflectionTests.cpp` (a new file — CONFIRMED
   by direct inspection: `tests/Renderer/Vulkan/` does NOT exist yet today, and no
   `DescriptorSetLayoutBuilderTests.cpp` exists anywhere in `tests/` either — create the new
   `tests/Renderer/Vulkan/` folder), asserting `ReflectComputeShader()` against a REAL,
   already-compiled `.comp.spv` this
   engine already ships: `Shaders/BoxBlur.comp` is the best fixture — it is small, already
   documented (`ComputeBlurValidation.cpp`'s own header comment states its exact binding
   convention: binding 0 = combined-image-sampler, binding 1 = storage image, plus a 2×`uint32_t`
   push-constant block, plus `layout(local_size_x = 16, local_size_y = 16)`, all confirmed directly
   against the real `src/Shaders/BoxBlur.comp` source), and already compiled by the existing build
   (`cmake/CompileShaders.cmake`'s `gte_add_shader()` already stages
   `BoxBlur.comp.spv` next to every target that already builds it).
   - **Also add `Renderer/Vulkan/ShaderReflectionTests.cpp` to `tests/CMakeLists.txt`'s
     `GTE_TEST_SOURCES` list** (see `PHASE0`'s own cross-cutting rule on this — it is a plain,
     manually-maintained list, NOT a glob; a new test file not added here silently never compiles
     or runs).
   - `tests/CMakeLists.txt` does NOT call `gte_add_shader()` for any shader today (confirmed by
     direct inspection of the real file — no test target stages a compiled `.spv` next to itself
     yet), so this phase adds the first one: `gte_add_shader(GreatTamanaEngineTests
     src/Shaders/BoxBlur.comp)`, placed right after `add_executable(GreatTamanaEngineTests ...)` in
     `tests/CMakeLists.txt` (see `gte_add_shader()`'s own doc comment, `cmake/CompileShaders.cmake` —
     its `POST_BUILD` step automatically stages the compiled binary at
     `$<TARGET_FILE_DIR:GreatTamanaEngineTests>/shaders/BoxBlur.comp.spv`). Rather than relying on
     `ctest`'s/the built `.exe`'s own current working directory (which `gtest_discover_tests()` does
     not guarantee equals the executable's own directory), bake the real, absolute, build-resolved
     path in at compile time via a dedicated `target_compile_definitions(GreatTamanaEngineTests
     PRIVATE
     GTE_TEST_BOXBLUR_SPV_PATH="$<TARGET_FILE_DIR:GreatTamanaEngineTests>/shaders/BoxBlur.comp.spv")`
     call, placed alongside the existing `target_compile_definitions(GreatTamanaEngineTests PRIVATE
     GTE_CORE_ARCHIVE_PATH="$<TARGET_FILE:gte_core>")` call (confirmed present in the real
     `tests/CMakeLists.txt` today) — this mirrors this exact test suite's
     own already-established idiom for "a test needs a real, absolute, build-layout-dependent
     path known only at configure/generate time". The new test reads `GTE_TEST_BOXBLUR_SPV_PATH`
     directly (`#ifdef`-guarded the same way that file does) rather than computing anything relative
     to `argv[0]` or the current working directory.
   - Assert: exactly 2 reflected bindings (binding 0 = `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`,
     binding 1 = `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`), `hasPushConstantRange == true` with
     `pushConstantRange.size == sizeof(std::uint32_t) * 2`, `localSize == {16, 16, 1}`.
   - Add a second test asserting a clean `std::runtime_error` (never a crash) for a
     non-existent/garbage file path (e.g. `"shaders/DoesNotExist.comp.spv"`).
3. **Compile-check** (`cmake --build build`, incremental) — confirm the new
   `ShaderReflection.h/.cpp` + the new CMake target compile cleanly, and the new test target
   links/runs (`ctest -R ShaderReflection` or the equivalent `--gtest_filter`).
4. **Commit everything currently outstanding for this campaign, together, in one coherent
   commit.** `git status` shows the already-modified-but-uncommitted `CMakeLists.txt`/`.gitignore`
   (the SPIRV-Reflect vendoring/linking precondition, Step 2.1) and the already-written-but-untracked
   `cmake/FetchSpirvReflect.cmake` plus the whole `task_manager/better-render-pass-1/` folder —
   none of this has been committed yet for this campaign. This phase's own commit is the first one
   for this campaign, and it stages and commits ALL of the above TOGETHER with this phase's own new
   `src/Renderer/Vulkan/ShaderReflection.h/.cpp`, `tests/Renderer/Vulkan/ShaderReflectionTests.cpp`,
   and the `tests/CMakeLists.txt` edit — one coherent PHASE1 commit, not a split history where the
   dependency and the module that uses it land separately.
5. Write `PHASE1_COMPLETION_REPORT.md`, commit via `git_add`/`git_commit` (this may be the same
   commit as item 4, or a small follow-up commit — either is acceptable, as long as both land before
   PHASE2 starts).

### Acceptance bar for this phase

- `third_party/spirv_reflect/` is staged, `.gte_fetched_ref` recorded, the `spirv_reflect` CMake
  target configures and builds (already true today — re-confirm, don't re-do).
- `ReflectComputeShader()` correctly reflects `BoxBlur.comp.spv`'s real, already-documented
  binding/push-constant/local-size shape, proven by a passing new Tier-1 test.
- Zero existing file's BEHAVIOR changed by this phase — the SPIRV-Reflect vendoring/linking lines
  in `CMakeLists.txt`/`.gitignore` were already written before this phase began (Step 2.1); this
  phase adds capability (`ShaderReflection.h/.cpp` + its test), it changes no existing pipeline
  construction call site's behavior at all.
- Every file this campaign has touched or added so far — `CMakeLists.txt`, `.gitignore`,
  `cmake/FetchSpirvReflect.cmake`, the whole `task_manager/better-render-pass-1/` folder, plus this
  phase's own new `ShaderReflection`/test files — is committed by the end of this phase.
