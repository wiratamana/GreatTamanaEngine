# plugins/gte_plugin_abi/CMakeLists.txt
#
# gte_plugin_abi - the frozen, versioned, minimal contract a host process and
# a plugin .dll both compile against for anything that crosses the
# process/binary boundary (PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md). Header-only,
# so this defines a plain INTERFACE library target - both gte_core and every
# plugin project link against it, so its include path is never
# hand-duplicated.
#
# NOTE: this file is add_subdirectory()'d from the real root CMakeLists.txt
# starting in PHASE2 (PHASE1 only creates these files - see
# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md, Step 3.1's own note that PHASE2 is the
# phase that actually wires add_subdirectory(plugins/gte_plugin_abi) in).

# CMAKE_BUILD_TYPE can be empty (a single-config generator with no explicit
# type set) - default it to "Unspecified" rather than leaving the generated
# header's buildConfig field blank, so an empty string never silently
# "matches" a differently-empty string for the wrong reason.
if(NOT CMAKE_BUILD_TYPE)
    set(GTE_PLUGIN_ABI_BUILD_TYPE "Unspecified")
else()
    set(GTE_PLUGIN_ABI_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

string(REPLACE "." ";" GTE_CXX_COMPILER_VERSION_LIST "${CMAKE_CXX_COMPILER_VERSION}")
list(GET GTE_CXX_COMPILER_VERSION_LIST 0 CMAKE_CXX_COMPILER_VERSION_MAJOR)
list(GET GTE_CXX_COMPILER_VERSION_LIST 1 CMAKE_CXX_COMPILER_VERSION_MINOR)
list(LENGTH GTE_CXX_COMPILER_VERSION_LIST GTE_CXX_COMPILER_VERSION_PARTS)
if(GTE_CXX_COMPILER_VERSION_PARTS GREATER 2)
    list(GET GTE_CXX_COMPILER_VERSION_LIST 2 CMAKE_CXX_COMPILER_VERSION_PATCH)
else()
    set(CMAKE_CXX_COMPILER_VERSION_PATCH 0)
endif()
set(CMAKE_BUILD_TYPE "${GTE_PLUGIN_ABI_BUILD_TYPE}") # for the @CMAKE_BUILD_TYPE@ substitution only, scoped to this file

# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md, Step 3.4 point 5 - read the REAL
# GTE_ENABLE_PLUGINS value (never hardcode 1) so a GTE_ENABLE_PLUGINS=OFF
# configuration produces a fingerprint whose sharedRuntimeLinkage field
# honestly reflects 0 - never claim shared linkage a given configuration
# didn't actually request. FURTHER real-world correction discovered during
# PHASE1's own implementation (see cmake/MingwRuntime.cmake's own top-of-file
# comment for the full evidence): GTE_ENABLE_PLUGINS=ON alone does NOT mean
# this build actually achieved shared CRT linkage - the ACTIVE toolchain
# might have no shared libstdc++ variant at all (true for this repo's
# default configure as of PHASE1), in which case
# gte_apply_plugin_shared_crt_linkage() is a real, honest no-op and this
# field must correctly read 0, never 1.
if(GTE_ENABLE_PLUGINS AND GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
    set(GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT 1)
else()
    set(GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT 0)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/GtePluginAbiFingerprintGenerated.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
    @ONLY
)

add_library(gte_plugin_abi INTERFACE)
target_include_directories(gte_plugin_abi INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(gte_plugin_abi INTERFACE cxx_std_20)
