# FetchImGui.cmake
#
# Downloads Dear ImGui (https://github.com/ocornut/imgui) straight from its
# GitHub repo, the same way FetchSDL3.cmake/FetchVulkan.cmake fetch their
# dependencies - no submodule, no package manager, nothing pre-installed on
# the machine. Only the pieces this engine actually needs are staged:
#
#   - Core library sources (imgui.h/.cpp, imgui_draw.cpp, imgui_tables.cpp,
#     imgui_widgets.cpp, imgui_demo.cpp, imgui_internal.h, imstb_*.h,
#     imconfig.h).
#   - backends/imgui_impl_sdl3.h/.cpp     - SDL3 platform backend (this
#     engine's Window/Application layer is SDL3-based).
#   - backends/imgui_impl_vulkan.h/.cpp   - Vulkan renderer backend, built
#     with IMGUI_IMPL_VULKAN_USE_VOLK so it resolves Vulkan calls through
#     this project's existing volk loader (see FetchVulkan.cmake) instead of
#     linking a Vulkan loader import lib directly. This engine already calls
#     volkInitialize()/volkLoadInstance()/volkLoadDevice() itself (see
#     VulkanInstance/VulkanDevice) before any ImGui Vulkan init would run, so
#     the backend just needs volk's function pointers to already be resolved
#     - it does not load them itself.
#
# Downloading works by tag, straight from GitHub's codeload archive URL
# (https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip), same
# approach as FetchVulkan.cmake. "latest" is resolved via the GitHub releases
# API (Dear ImGui publishes proper GitHub Releases for its main/master
# branch, e.g. "v1.91.9b").
#
# Staged into this repo (all gitignored, regenerated automatically on
# configure - see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/*.h, *.cpp
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl3.h/.cpp
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.h/.cpp
#
# Defines one target:
#   imgui   - STATIC library compiling the core + SDL3 + Vulkan backend
#             sources; links SDL3::SDL3 and volk publicly, so engine code
#             just needs target_link_libraries(... imgui) and can
#             `#include "imgui.h"` / `#include "backends/imgui_impl_sdl3.h"`
#             / `#include "backends/imgui_impl_vulkan.h"`.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   IMGUI_RELEASE_TAG      - Git tag to fetch from ocornut/imgui, e.g.
#                             "v1.91.9b". Defaults to "latest".
#   IMGUI_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if already
#                             present.

if(NOT WIN32)
    message(FATAL_ERROR "FetchImGui.cmake only supports Windows. Not supported on this platform.")
endif()

set(IMGUI_RELEASE_TAG "latest" CACHE STRING
    "Dear ImGui git tag to fetch (e.g. v1.91.9b), or 'latest'.")
option(IMGUI_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting Dear ImGui even if it already appears to be present."
    OFF)

# _imgui_github_get_json(<label> <url> <out_json>)
#
# Same helper as FetchVulkan.cmake's _github_get_json, duplicated locally so
# this module has no include-order dependency on FetchVulkan.cmake having
# been included first.
function(_imgui_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_imgui_fetch")
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

# _imgui_resolve_tag(<tag> <out_tag_name>)
#
# Resolves "latest" to a concrete tag name via the GitHub releases API. If
# <tag> isn't "latest", this is a no-op (no network call at all).
function(_imgui_resolve_tag tag out_tag_name)
    if(NOT tag STREQUAL "latest")
        set(${out_tag_name} "${tag}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "imgui: resolving 'latest' via releases API")
    _imgui_github_get_json("imgui_release_latest" "https://api.github.com/repos/ocornut/imgui/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "imgui: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _imgui_download_and_extract_tag(<tag_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete ocornut/imgui tag
# and extracts it, returning the single top-level folder GitHub always wraps
# archive contents in.
function(_imgui_download_and_extract_tag tag_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_imgui_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_zip_path "${_work_dir}/imgui-${tag_name}.zip")
    set(_zip_url "https://github.com/ocornut/imgui/archive/refs/tags/${tag_name}.zip")

    message(STATUS "imgui: downloading ${_zip_url}")
    file(DOWNLOAD "${_zip_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "imgui: failed to download ${_zip_url}: ${_dl_msg}")
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "imgui: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

function(_imgui_download_and_stage)
    _imgui_resolve_tag("${IMGUI_RELEASE_TAG}" _resolved_tag)
    message(STATUS "imgui: resolved tag '${_resolved_tag}'")

    _imgui_download_and_extract_tag("${_resolved_tag}" _root)

    set(_core_files
        imgui.h
        imgui.cpp
        imgui_draw.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
        imgui_demo.cpp
        imgui_internal.h
        imstb_rectpack.h
        imstb_textedit.h
        imstb_truetype.h
        imconfig.h
    )
    foreach(_f ${_core_files})
        if(NOT EXISTS "${_root}/${_f}")
            message(FATAL_ERROR "imgui: extracted package is missing ${_f}")
        endif()
    endforeach()

    set(_backend_files
        backends/imgui_impl_sdl3.h
        backends/imgui_impl_sdl3.cpp
        backends/imgui_impl_vulkan.h
        backends/imgui_impl_vulkan.cpp
    )
    foreach(_f ${_backend_files})
        if(NOT EXISTS "${_root}/${_f}")
            message(FATAL_ERROR "imgui: extracted package is missing ${_f}")
        endif()
    endforeach()

    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/imgui")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/imgui")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/imgui/backends")

    foreach(_f ${_core_files})
        file(COPY "${_root}/${_f}" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/imgui")
    endforeach()
    foreach(_f ${_backend_files})
        file(COPY "${_root}/${_f}" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/imgui/backends")
    endforeach()

    message(STATUS "imgui: staged -> ${CMAKE_SOURCE_DIR}/third_party/imgui")
endfunction()

# fetch_imgui()
#
# Ensures Dear ImGui (core + SDL3/Vulkan backends) is present in this
# project (downloading it from GitHub if needed), then defines the `imgui`
# STATIC library target described above. Must be called after fetch_sdl3()
# and fetch_vulkan(), since the imgui target links SDL3::SDL3 and volk.
function(fetch_imgui)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_imgui() only supports Windows. Not supported on this platform.")
    endif()

    set(_imgui_marker "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.h")
    set(_imgui_backend_marker "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.h")
    if(NOT IMGUI_FORCE_REDOWNLOAD AND EXISTS "${_imgui_marker}" AND EXISTS "${_imgui_backend_marker}")
        message(STATUS "imgui: already present (third_party/imgui/imgui.h found) - skipping download.")
    else()
        _imgui_download_and_stage()
    endif()

    if(NOT TARGET imgui)
        add_library(imgui STATIC
            "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl3.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp"
        )
        target_include_directories(imgui PUBLIC
            "${CMAKE_SOURCE_DIR}/third_party/imgui"
            "${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
        )
        # Route the Vulkan backend through this project's existing volk
        # loader instead of a classic Vulkan loader import lib - see
        # FetchVulkan.cmake. The engine itself already calls
        # volkInitialize()/volkLoadInstance()/volkLoadDevice() before any
        # ImGui Vulkan init code runs, so the backend only needs volk's
        # function pointers, never loads them itself.
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
        target_link_libraries(imgui PUBLIC SDL3::SDL3 volk)
    endif()
endfunction()
