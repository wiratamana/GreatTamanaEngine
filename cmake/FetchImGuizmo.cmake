# FetchImGuizmo.cmake
#
# Downloads ImGuizmo (https://github.com/CedricGuillemet/ImGuizmo) straight
# from its GitHub repo, the same way FetchSDL3.cmake/FetchVulkan.cmake/
# FetchImGui.cmake/FetchVMA.cmake fetch their dependencies - no submodule, no
# package manager, nothing pre-installed on the machine.
#
# ImGuizmo is a small immediate-mode 3D gizmo widget (translate/rotate/scale
# manipulators, view cubes, etc.) built directly on top of Dear ImGui's
# ImDrawList - this is what backs the Editor's Scene-view transform gizmo
# (see src/Editor/TransformGizmo.h/.cpp). Only the core widget is staged:
#
#   - ImGuizmo.h / ImGuizmo.cpp - the gizmo widget itself.
#
# The repo's other optional widgets (GraphEditor.h/.cpp, ImCurveEdit.h/.cpp,
# ImSequencer.h/.cpp - a node graph editor, curve editor, and sequencer/
# timeline widget) are deliberately NOT staged - this engine only needs the
# 3D transform gizmo, not those.
#
# Downloading works straight from GitHub's codeload archive URL, same
# approach as FetchImGui.cmake, resolved against BOTH possible archive
# layouts since IMGUIZMO_RELEASE_TAG may name a branch (master, the default)
# or a tag/"latest" (a real release):
#   - branch : https://github.com/<owner>/<repo>/archive/refs/heads/<ref>.zip
#   - tag    : https://github.com/<owner>/<repo>/archive/refs/tags/<ref>.zip
# The branch URL is tried first (since the default, "master", is a branch),
# falling back to the tag URL if that 404s - so an explicit release tag
# (e.g. "1.83") or "latest" (resolved via the GitHub releases API) also
# works unchanged.
#
# Staged into this repo (all gitignored, regenerated automatically on
# configure - see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.h
#   ${CMAKE_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.cpp
#   ${CMAKE_SOURCE_DIR}/third_party/imguizmo/.gte_fetched_ref - plain text
#       file recording exactly which resolved ref is currently staged, so
#       switching IMGUIZMO_RELEASE_TAG on a machine that already has a
#       previous fetch staged correctly triggers a fresh re-download instead
#       of silently reusing the wrong version.
#
# ImGuizmo.cpp is patched immediately after staging (see
# _imguizmo_apply_gte_patches() below) to fix a genuine upstream bug: an
# in-progress SCALE/ROTATE gizmo drag freezes solid the instant the mouse
# cursor leaves the SetRect() rectangle (e.g. dragging out past the Scene
# panel's edge), unlike TRANSLATE, which keeps tracking fine - see that
# function's own comment for the full root-cause explanation.
#
# Defines one target:
#   imguizmo   - STATIC library compiling ImGuizmo.cpp; publicly links the
#                `imgui` target (ImGuizmo.cpp/.h include imgui.h and
#                imgui_internal.h), so engine code just needs
#                target_link_libraries(... imguizmo) and can
#                `#include "ImGuizmo.h"`.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   IMGUIZMO_RELEASE_TAG      - Git ref to fetch from
#                                 CedricGuillemet/ImGuizmo: a branch (e.g.
#                                 "master"), a tag (e.g. "1.83"), or "latest"
#                                 (resolved to the newest tagged release via
#                                 the GitHub releases API). Defaults to
#                                 "master".
#   IMGUIZMO_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if
#                                 already present and already matching
#                                 IMGUIZMO_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchImGuizmo.cmake only supports Windows. Not supported on this platform.")
endif()

set(IMGUIZMO_RELEASE_TAG "master" CACHE STRING
    "ImGuizmo git ref to fetch: a branch (e.g. 'master'), a tag (e.g. '1.83'), or 'latest'.")
option(IMGUIZMO_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting ImGuizmo even if it already appears to be present and matching IMGUIZMO_RELEASE_TAG."
    OFF)

# _imguizmo_github_get_json(<label> <url> <out_json>)
#
# Same helper as FetchImGui.cmake's _imgui_github_get_json, duplicated
# locally so this module has no include-order dependency on any of the
# other Fetch*.cmake modules having been included first.
function(_imguizmo_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_imguizmo_fetch")
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

# _imguizmo_resolve_ref(<ref> <out_resolved_ref>)
#
# Resolves "latest" to a concrete release tag name via the GitHub releases
# API. Any other value (a branch name like "master", or an explicit tag) is
# passed through unchanged with no network call at all.
function(_imguizmo_resolve_ref ref out_resolved_ref)
    if(NOT ref STREQUAL "latest")
        set(${out_resolved_ref} "${ref}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "imguizmo: resolving 'latest' via releases API")
    _imguizmo_github_get_json("imguizmo_release_latest" "https://api.github.com/repos/CedricGuillemet/ImGuizmo/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "imguizmo: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_resolved_ref} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _imguizmo_download_and_extract_ref(<ref_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete
# CedricGuillemet/ImGuizmo ref and extracts it, returning the single
# top-level folder GitHub always wraps archive contents in. <ref_name> may
# be a branch (tried first, since the default "master" is a branch) or a
# tag (tried as a fallback) - this way both branch names and release
# tags/"latest" keep working through the same function.
function(_imguizmo_download_and_extract_ref ref_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_imguizmo_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    string(REPLACE "/" "-" _safe_ref_name "${ref_name}")
    set(_zip_path "${_work_dir}/imguizmo-${_safe_ref_name}.zip")

    set(_heads_url "https://github.com/CedricGuillemet/ImGuizmo/archive/refs/heads/${ref_name}.zip")
    set(_tags_url "https://github.com/CedricGuillemet/ImGuizmo/archive/refs/tags/${ref_name}.zip")

    message(STATUS "imguizmo: downloading ${_heads_url}")
    file(DOWNLOAD "${_heads_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(STATUS "imguizmo: '${ref_name}' is not a branch (refs/heads) - retrying as a tag (refs/tags)")
        file(DOWNLOAD "${_tags_url}" "${_zip_path}"
            HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
            STATUS _dl_status
            TLS_VERIFY ON
            SHOW_PROGRESS
        )
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            message(FATAL_ERROR "imguizmo: failed to download ref '${ref_name}' as either a branch or a tag: ${_dl_msg}")
        endif()
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "imguizmo: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

# _imguizmo_apply_gte_patches(<staged_cpp_path>)
#
# Patches a freshly-staged ImGuizmo.cpp to fix a genuine upstream bug found
# while integrating the Scene-view transform gizmo: while a manipulation is
# ALREADY in progress (gContext.mbUsing == true), HandleScale()/
# HandleRotation() unconditionally also require gContext.mbMouseOver (an
# ImGui window-hover flag) before doing anything at all - which silently
# FREEZES an in-progress scale/rotate drag the instant the mouse cursor
# leaves the ImGuizmo::SetRect() rectangle (e.g. dragging out past the
# Scene panel's edge). HandleTranslation() has no such bug: it only
# requires hover to START a brand-new manipulation (via its own
# GetMoveType(), which separately checks `gContext.mbUsing ||
# !gContext.mbMouseOver` and bails to MT_NONE - i.e. hover only gates
# picking a NEW handle, never continuing one already grabbed) - never to
# CONTINUE one already in progress. This patch makes HandleScale()/
# HandleRotation() match that same, correct, Translate-only-checks-hover-
# to-start pattern, by only requiring mbMouseOver when NOT already
# gContext.mbUsing.
#
# Applied here (rather than hand-editing third_party/imguizmo/ImGuizmo.cpp
# directly) so the fix survives IMGUIZMO_FORCE_REDOWNLOAD/a clean checkout/
# CI re-fetching a pristine copy from GitHub - third_party/ is gitignored,
# nothing under it is ever committed (see this file's own header comment).
# Each of the two expected snippets is searched for BEFORE being replaced,
# and a message(WARNING ...) is raised (never a silent no-op) if either is
# missing - e.g. IMGUIZMO_RELEASE_TAG points at a future revision where
# upstream rewords/fixes this differently - so a stale/ineffective patch
# never ships unnoticed.
function(_imguizmo_apply_gte_patches staged_cpp_path)
    file(READ "${staged_cpp_path}" _src)

    set(_scale_from "if((!Intersects(op, SCALE) && !Intersects(op, SCALEU)) || type != MT_NONE || !gContext.mbMouseOver)")
    set(_scale_to "if((!Intersects(op, SCALE) && !Intersects(op, SCALEU)) || type != MT_NONE || (!gContext.mbMouseOver && !gContext.mbUsing)) // GreatTamanaEngine patch - see cmake/FetchImGuizmo.cmake's _imguizmo_apply_gte_patches()")

    set(_rotate_from "if(!Intersects(op, ROTATE) || type != MT_NONE || !gContext.mbMouseOver)")
    set(_rotate_to "if(!Intersects(op, ROTATE) || type != MT_NONE || (!gContext.mbMouseOver && !gContext.mbUsing)) // GreatTamanaEngine patch - see cmake/FetchImGuizmo.cmake's _imguizmo_apply_gte_patches()")

    string(FIND "${_src}" "${_scale_from}" _scale_pos)
    if(_scale_pos EQUAL -1)
        message(WARNING "imguizmo: HandleScale()'s expected mbMouseOver gate text was not found - the scale-drag-freezes-outside-viewport patch was NOT applied. ImGuizmo's source may have changed upstream; re-check cmake/FetchImGuizmo.cmake's _imguizmo_apply_gte_patches().")
    else()
        string(REPLACE "${_scale_from}" "${_scale_to}" _src "${_src}")
    endif()

    string(FIND "${_src}" "${_rotate_from}" _rotate_pos)
    if(_rotate_pos EQUAL -1)
        message(WARNING "imguizmo: HandleRotation()'s expected mbMouseOver gate text was not found - the rotate-drag-freezes-outside-viewport patch was NOT applied. ImGuizmo's source may have changed upstream; re-check cmake/FetchImGuizmo.cmake's _imguizmo_apply_gte_patches().")
    else()
        string(REPLACE "${_rotate_from}" "${_rotate_to}" _src "${_src}")
    endif()

    file(WRITE "${staged_cpp_path}" "${_src}")
endfunction()

function(_imguizmo_download_and_stage resolved_ref)
    _imguizmo_download_and_extract_ref("${resolved_ref}" _root)

    # Newer ImGuizmo revisions moved all sources under src/ (older tags had
    # them directly at the repo root) - support both layouts transparently.
    set(_src_dir "${_root}/src")
    if(NOT EXISTS "${_src_dir}/ImGuizmo.h")
        set(_src_dir "${_root}")
    endif()

    set(_core_files
        ImGuizmo.h
        ImGuizmo.cpp
    )
    foreach(_f ${_core_files})
        if(NOT EXISTS "${_src_dir}/${_f}")
            message(FATAL_ERROR "imguizmo: extracted package is missing ${_f} (looked in ${_src_dir})")
        endif()
    endforeach()

    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/imguizmo")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/imguizmo")

    foreach(_f ${_core_files})
        file(COPY "${_src_dir}/${_f}" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/imguizmo")
    endforeach()

    _imguizmo_apply_gte_patches("${CMAKE_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.cpp")

    file(WRITE "${CMAKE_SOURCE_DIR}/third_party/imguizmo/.gte_fetched_ref" "${resolved_ref}")

    message(STATUS "imguizmo: staged '${resolved_ref}' -> ${CMAKE_SOURCE_DIR}/third_party/imguizmo")
endfunction()

# fetch_imguizmo()
#
# Ensures ImGuizmo is present in this project (downloading it from GitHub if
# needed), then defines the `imguizmo` STATIC library target described
# above. Must be called after fetch_imgui(), since the imguizmo target
# links the imgui target.
function(fetch_imguizmo)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_imguizmo() only supports Windows. Not supported on this platform.")
    endif()

    if(NOT TARGET imgui)
        message(FATAL_ERROR "fetch_imguizmo() requires fetch_imgui() to have been called first (imgui target not found).")
    endif()

    _imguizmo_resolve_ref("${IMGUIZMO_RELEASE_TAG}" _resolved_ref)

    set(_imguizmo_marker "${CMAKE_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.h")
    set(_imguizmo_src_marker "${CMAKE_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.cpp")
    set(_imguizmo_ref_marker "${CMAKE_SOURCE_DIR}/third_party/imguizmo/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_imguizmo_marker}" AND EXISTS "${_imguizmo_src_marker}" AND EXISTS "${_imguizmo_ref_marker}")
        file(READ "${_imguizmo_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL _resolved_ref)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT IMGUIZMO_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "imguizmo: already present and matching ref '${_resolved_ref}' - skipping download.")
    else()
        _imguizmo_download_and_stage("${_resolved_ref}")
    endif()

    if(NOT TARGET imguizmo)
        add_library(imguizmo STATIC
            "${CMAKE_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.cpp"
        )
        target_include_directories(imguizmo PUBLIC
            "${CMAKE_SOURCE_DIR}/third_party/imguizmo"
        )
        target_link_libraries(imguizmo PUBLIC imgui)
    endif()
endfunction()
