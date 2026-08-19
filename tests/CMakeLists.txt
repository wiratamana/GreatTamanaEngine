# tests/CMakeLists.txt
#
# GreatTamanaEngineTests - GoogleTest-based unit test suite for gte_core (see
# the root CMakeLists.txt, which only add_subdirectory()'s this when
# GTE_BUILD_TESTS is ON). GoogleTest itself is fetched/built by
# cmake/FetchGTest.cmake, following the exact same "no submodule, no package
# manager, download+stage on first configure" convention as
# SDL3/Vulkan/VMA/ImGui (see cmake/Fetch*.cmake).
#
# --- Test taxonomy ---------------------------------------------------------
#
# Every test file below is "Tier 1": pure logic, with NO live Vulkan device,
# GPU, or SDL window/video subsystem required to run - safe to execute on
# every commit/PR, including a CI runner with no GPU at all:
#
#   - Memory/GpuResourceHandleTests.cpp   - GpuResourceHandle value semantics.
#   - Memory/GpuMemoryTrackerTests.cpp    - GpuMemoryTracker's Track()/
#                                            Untrack()/totals/snapshot
#                                            bookkeeping (takes plain enums +
#                                            a byte count - never touches a
#                                            real VmaAllocation).
#   - Input/InputStateTests.cpp           - InputState's held/pressed/
#                                            released/delta semantics, fed
#                                            entirely with hand-built
#                                            gte::Event values.
#   - Application/EventTranslatorTests.cpp - SDL_Event -> gte::Event mapping,
#                                            using hand-built SDL_Event
#                                            values (SDL_Event is a plain
#                                            struct/union - no SDL_Init() or
#                                            video subsystem needed to
#                                            construct or translate one).
#   - Renderer/VertexTests.cpp             - Vertex's Vulkan binding/
#                                            attribute description metadata.
#   - Math/Vec2Tests.cpp, Vec3Tests.cpp    - Vec2/Vec3 arithmetic and
#                                            geometric ops (dot/cross/
#                                            length/normalize/lerp), using
#                                            exact hand-computed values.
#   - Math/Mat4Tests.cpp                   - Mat4 multiply/transpose/
#                                            inverse/LookAtLH/
#                                            PerspectiveFovLH_ZO, using
#                                            hand-verified exact values.
#   - Math/QuatTests.cpp                   - Quat multiply/slerp/nlerp/
#                                            axis-angle/matrix and Euler
#                                            round-trip conversions.
#
# Note that the test BINARY still dynamically depends on SDL3.dll at process
# load time regardless of the above (gte_core's Window.cpp references SDL
# symbols like SDL_CreateWindow, even though no Tier 1 test ever calls
# SDL_Init() or opens a window) - sdl3_copy_runtime_dll() below handles that,
# same as for the real GreatTamanaEngine.exe.
#
# --- Tier 2 (GPU-dependent) tests: intentionally not implemented yet -------
#
# Buffer/RenderTexture/Pipeline/GpuResourceFactory all need a real
# VkDevice + VmaAllocator to construct at all, and today's
# VulkanDevice::PickPhysicalDevice() additionally requires a real
# VkSurfaceKHR (to query present support), which normally comes from an
# actual OS window (Window + VulkanSurface). A future GpuTestFixture could
# build a headless VkSurfaceKHR via VK_EXT_headless_surface (supported by
# most desktop drivers, and by software implementations like Mesa
# lavapipe/SwiftShader) instead of a real Window, then GTEST_SKIP() the
# whole fixture at runtime if instance/device creation fails - i.e. those
# tests would still PASS (skipped, not failed) on a GPU-less CI runner, and
# only actually exercise the GPU on a developer machine or a self-hosted
# runner that has one. Left as a documented follow-up rather than
# implemented here, since it needs its own fixture/CMake wiring
# (GTE_BUILD_GPU_TESTS or similar) rather than being bolted onto Tier 1.

add_executable(GreatTamanaEngineTests
    Memory/GpuResourceHandleTests.cpp
    Memory/GpuMemoryTrackerTests.cpp
    Input/InputStateTests.cpp
    Application/EventTranslatorTests.cpp
    Renderer/VertexTests.cpp
    Math/Vec2Tests.cpp
    Math/Vec3Tests.cpp
    Math/Mat4Tests.cpp
    Math/QuatTests.cpp
)

target_link_libraries(GreatTamanaEngineTests PRIVATE
    gte_core
    GTest::gtest
    GTest::gtest_main
)

# See the file-level comment above for why this is still needed even though
# no test here ever calls SDL_Init().
sdl3_copy_runtime_dll(GreatTamanaEngineTests)

# gtest_discover_tests() (from CMake's own bundled GoogleTest.cmake module,
# distinct from cmake/FetchGTest.cmake which only fetches GoogleTest's
# SOURCE) runs the built test binary with --gtest_list_tests as a post-build
# step to register every individual TEST()/TEST_F() as its own CTest test,
# so `ctest` output shows real test names instead of one opaque
# "GreatTamanaEngineTests passed/failed" line. This is registered AFTER
# sdl3_copy_runtime_dll() above so SDL3.dll is already staged next to the
# .exe (POST_BUILD steps run in the order they were added) by the time
# discovery tries to run it.
include(GoogleTest)
gtest_discover_tests(GreatTamanaEngineTests)
