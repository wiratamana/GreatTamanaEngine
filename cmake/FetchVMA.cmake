# FetchVMA.cmake
#
# Downloads the Vulkan Memory Allocator (VMA)
# (https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) straight
# from its GitHub repo, the same way FetchSDL3.cmake/FetchVulkan.cmake/
# FetchImGui.cmake fetch their dependencies - no submodule, no package
# manager, nothing pre-installed on the machine.
#
# VMA is a single public header (include/vk_mem_alloc.h) - the "implementation"
# only exists once some translation unit defines VMA_IMPLEMENTATION before
# including it. This module only stages the header and exposes an INTERFACE
# target; it deliberately does NOT compile an implementation .cpp anywhere -
# that's for whichever engine source file first needs a real VmaAllocator to
# add (with `#define VMA_IMPLEMENTATION` above a single `#include
# <vk_mem_alloc.h>`).
#
# Downloading works by tag, straight from GitHub's codeload archive URL
# (https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip), same
# approach as FetchVulkan.cmake/FetchImGui.cmake. VMA publishes proper GitHub
# Releases, so "latest" resolves via the releases API.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/vma/vk_mem_alloc.h
#
# Defines one target:
#   vma   - INTERFACE library exposing third_party/vma as an include
#           directory and publicly linking Vulkan::Headers (vk_mem_alloc.h
#           itself #includes <vulkan/vulkan.h>). Engine code should
#           `#include <vk_mem_alloc.h>`. Since this project resolves Vulkan
#           entry points dynamically through volk rather than linking a
#           classic Vulkan loader import lib, whichever translation unit adds
#           VMA_IMPLEMENTATION should make sure volk.h is included first and
#           volkLoadInstance()/volkLoadDevice() have already run before a
#           VmaAllocator is created - matching how this project already wires
#           volk into Dear ImGui's Vulkan backend (see FetchImGui.cmake).
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   VMA_RELEASE_TAG      - Git tag to fetch from
#                           GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator,
#                           e.g. "v3.3.0". Defaults to "latest".
#   VMA_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if already
#                           present.

if(NOT WIN32)
    message(FATAL_ERROR "FetchVMA.cmake only supports Windows. Not supported on this platform.")
endif()

set(VMA_RELEASE_TAG "latest" CACHE STRING
    "Vulkan Memory Allocator git tag to fetch (e.g. v3.3.0), or 'latest'.")
option(VMA_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting Vulkan Memory Allocator even if it already appears to be present."
    OFF)

# _vma_github_get_json(<label> <url> <out_json>)
#
# Same helper as FetchVulkan.cmake's _github_get_json / FetchImGui.cmake's
# _imgui_github_get_json, duplicated locally so this module has no
# include-order dependency on either having been included first.
function(_vma_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_vma_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_out_file "${_work_dir}/${label}.json")

    file(DOWNLOAD "${url}" "${_out_file}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        HTTPHEADER "Accept: application/vnd.github+json"
        STATUS _dl_status
        TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        set(${out_json} "" PARENT_SCOPE)
        return()
    endif()

    file(READ "${_out_file}" _json)
    set(${out_json} "${_json}" PARENT_SCOPE)
endfunction()

# _vma_resolve_tag(<tag> <out_tag_name>)
#
# Resolves "latest" to a concrete tag name via the GitHub releases API. If
# <tag> isn't "latest", this is a no-op (no network call at all).
function(_vma_resolve_tag tag out_tag_name)
    if(NOT tag STREQUAL "latest")
        set(${out_tag_name} "${tag}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "VulkanMemoryAllocator: resolving 'latest' via releases API")
    _vma_github_get_json("vma_release_latest"
        "https://api.github.com/repos/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "VulkanMemoryAllocator: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _vma_download_and_extract_tag(<tag_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete
# GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator tag (no GitHub API call
# involved) and extracts it, returning the single top-level folder GitHub
# always wraps archive contents in.
function(_vma_download_and_extract_tag tag_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_vma_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_zip_path "${_work_dir}/VulkanMemoryAllocator-${tag_name}.zip")
    set(_zip_url "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/${tag_name}.zip")

    message(STATUS "VulkanMemoryAllocator: downloading ${_zip_url}")
    file(DOWNLOAD "${_zip_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "VulkanMemoryAllocator: failed to download ${_zip_url}: ${_dl_msg}")
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "VulkanMemoryAllocator: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

function(_vma_download_and_stage)
    _vma_resolve_tag("${VMA_RELEASE_TAG}" _resolved_tag)
    message(STATUS "VulkanMemoryAllocator: resolved tag '${_resolved_tag}'")

    _vma_download_and_extract_tag("${_resolved_tag}" _root)

    if(NOT EXISTS "${_root}/include/vk_mem_alloc.h")
        message(FATAL_ERROR "VulkanMemoryAllocator: extracted package is missing include/vk_mem_alloc.h")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/vma")
    file(COPY "${_root}/include/vk_mem_alloc.h" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/vma")

    message(STATUS "VulkanMemoryAllocator: staged -> ${CMAKE_SOURCE_DIR}/third_party/vma")
endfunction()

# fetch_vma()
#
# Ensures the Vulkan Memory Allocator header is present in this project
# (downloading it from GitHub if needed), then defines the `vma` INTERFACE
# target described above. Must be called after fetch_vulkan(), since the vma
# target links Vulkan::Headers.
function(fetch_vma)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_vma() only supports Windows. Not supported on this platform.")
    endif()

    if(NOT TARGET Vulkan::Headers)
        message(FATAL_ERROR "fetch_vma() requires fetch_vulkan() to have been called first (Vulkan::Headers target not found).")
    endif()

    set(_vma_marker "${CMAKE_SOURCE_DIR}/third_party/vma/vk_mem_alloc.h")
    if(NOT VMA_FORCE_REDOWNLOAD AND EXISTS "${_vma_marker}")
        message(STATUS "VulkanMemoryAllocator: already present (third_party/vma/vk_mem_alloc.h found) - skipping download.")
    else()
        _vma_download_and_stage()
    endif()

    if(NOT TARGET vma)
        add_library(vma INTERFACE)
        target_include_directories(vma INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/vma"
        )
        target_link_libraries(vma INTERFACE Vulkan::Headers)
    endif()
endfunction()
