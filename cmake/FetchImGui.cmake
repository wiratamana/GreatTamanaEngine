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
# Fetches the **docking branch** (https://github.com/ocornut/imgui/tree/docking)
# by default, not a tagged release - the Editor's planned Hierarchy/Inspector/
# Scene panels need real ImGui docking (ImGuiConfigFlags_DockingEnable,
# DockSpace/DockBuilder) to lay out as separate, rearrangeable/dockable
# panels, which does not exist on ocornut/imgui's mainline release tags.
#
# Downloading works straight from GitHub's codeload archive URL, same
# approach as FetchVulkan.cmake, but resolved against BOTH possible archive
# layouts since IMGUI_RELEASE_TAG may now name a branch (docking) or a
# tag/"latest" (a real release):
#   - branch : https://github.com/<owner>/<repo>/archive/refs/heads/<ref>.zip
#   - tag    : https://github.com/<owner>/<repo>/archive/refs/tags/<ref>.zip
# The branch URL is tried first (since the new default, "docking", is a
# branch), falling back to the tag URL if that 404s - so an explicit release
# tag (e.g. "v1.91.9b") or "latest" (resolved via the GitHub releases API,
# same as before) still works unchanged.
#
# Staged into this repo (all gitignored, regenerated automatically on
# configure - see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/*.h, *.cpp
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl3.h/.cpp
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.h/.cpp
#   ${CMAKE_SOURCE_DIR}/third_party/imgui/.gte_fetched_ref  - plain text file
#       recording exactly which resolved ref is currently staged, so
#       switching IMGUI_RELEASE_TAG (e.g. "docking" -> "latest") on a machine
#       that already has a previous fetch staged correctly triggers a fresh
#       re-download instead of silently reusing the wrong version.
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
#   IMGUI_RELEASE_TAG      - Git ref to fetch from ocornut/imgui: a branch
#                             name (e.g. "docking"), a tag (e.g. "v1.91.9b"),
#                             or "latest" (resolved to the newest tagged
#                             release via the GitHub releases API). Defaults
#                             to "docking".
#   IMGUI_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if already
#                             present and already matching IMGUI_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchImGui.cmake only supports Windows. Not supported on this platform.")
endif()

set(IMGUI_RELEASE_TAG "docking" CACHE STRING
    "Dear ImGui git ref to fetch: a branch (e.g. 'docking'), a tag (e.g. 'v1.91.9b'), or 'latest'.")
option(IMGUI_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting Dear ImGui even if it already appears to be present and matching IMGUI_RELEASE_TAG."
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

# _imgui_resolve_ref(<ref> <out_resolved_ref>)
#
# Resolves "latest" to a concrete release tag name via the GitHub releases
# API. Any other value (a branch name like "docking", or an explicit tag) is
# passed through unchanged with no network call at all.
function(_imgui_resolve_ref ref out_resolved_ref)
    if(NOT ref STREQUAL "latest")
        set(${out_resolved_ref} "${ref}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "imgui: resolving 'latest' via releases API")
    _imgui_github_get_json("imgui_release_latest" "https://api.github.com/repos/ocornut/imgui/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "imgui: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_resolved_ref} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _imgui_download_and_extract_ref(<ref_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete ocornut/imgui ref
# and extracts it, returning the single top-level folder GitHub always wraps
# archive contents in. <ref_name> may be a branch (tried first, since the
# default "docking" is a branch) or a tag (tried as a fallback) - this way
# both branch names and release tags/"latest" keep working through the same
# function.
function(_imgui_download_and_extract_ref ref_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_imgui_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    string(REPLACE "/" "-" _safe_ref_name "${ref_name}")
    set(_zip_path "${_work_dir}/imgui-${_safe_ref_name}.zip")

    set(_heads_url "https://github.com/ocornut/imgui/archive/refs/heads/${ref_name}.zip")
    set(_tags_url "https://github.com/ocornut/imgui/archive/refs/tags/${ref_name}.zip")

    message(STATUS "imgui: downloading ${_heads_url}")
    file(DOWNLOAD "${_heads_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(STATUS "imgui: '${ref_name}' is not a branch (refs/heads) - retrying as a tag (refs/tags)")
        file(DOWNLOAD "${_tags_url}" "${_zip_path}"
            HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
            STATUS _dl_status
            TLS_VERIFY ON
            SHOW_PROGRESS
        )
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            message(FATAL_ERROR "imgui: failed to download ref '${ref_name}' as either a branch or a tag: ${_dl_msg}")
        endif()
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

function(_imgui_download_and_stage resolved_ref)
    _imgui_download_and_extract_ref("${resolved_ref}" _root)

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

    file(WRITE "${CMAKE_SOURCE_DIR}/third_party/imgui/.gte_fetched_ref" "${resolved_ref}")

    message(STATUS "imgui: staged '${resolved_ref}' -> ${CMAKE_SOURCE_DIR}/third_party/imgui")
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

    _imgui_resolve_ref("${IMGUI_RELEASE_TAG}" _resolved_ref)

    set(_imgui_marker "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.h")
    set(_imgui_backend_marker "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.h")
    set(_imgui_ref_marker "${CMAKE_SOURCE_DIR}/third_party/imgui/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_imgui_marker}" AND EXISTS "${_imgui_backend_marker}" AND EXISTS "${_imgui_ref_marker}")
        file(READ "${_imgui_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL _resolved_ref)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT IMGUI_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "imgui: already present and matching ref '${_resolved_ref}' - skipping download.")
    else()
        _imgui_download_and_stage("${_resolved_ref}")
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
