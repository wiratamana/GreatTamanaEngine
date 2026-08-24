# FetchSaba.cmake
#
# Downloads saba (https://github.com/benikabocha/saba) straight from its
# GitHub repo, the same "no submodule, no package manager, nothing
# pre-installed on the machine" philosophy as every other Fetch*.cmake module
# in this project (see FetchKTX.cmake/FetchImGuizmo.cmake for the closest
# siblings - a from-source, curated-subset build, not a prebuilt binary).
#
# saba is a PMD/PMX (MikuMikuDance model) + VMD (motion) file format reader,
# with an OPTIONAL higher-level skinning/physics runtime (PMXModel/MMDPhysics)
# built on top of Bullet. This project only vendors and compiles the raw FILE
# READER layer - exactly enough to parse a .pmx file's header/vertex/face/
# material/bone/morph sections into plain structs - NOT the Bullet-dependent
# skinning/physics runtime, since the current integration goal is purely
# extracting vertex positions/normals/UVs (see src/Assets/PmxLoader.h/.cpp),
# not real-time bone deformation. This keeps the dependency footprint tiny:
# no Bullet, no vendored viewer (GLFW/gl3w/ImGui-for-the-viewer/sol2/lua/...)
# ever needs to be fetched or built.
#
# Curated subset actually compiled (see fetch_saba() below), traced by hand
# from saba's own src/CMakeLists.txt dependency graph:
#   Saba/Base/File.cpp/.h             - buffered binary file reader (fread_s
#                                        based), UTF-8 aware Open() on Windows.
#   Saba/Base/UnicodeUtil.cpp/.h      - UTF-8/16/32 conversion helpers, used by
#                                        File.cpp's UTF-8 path handling and by
#                                        PMXFile.cpp's UTF-16 model/bone/
#                                        material name decoding.
#   Saba/Model/MMD/SjisToUnicode.cpp/.h - Shift-JIS -> UTF-16/32 tables, only
#                                        actually exercised by the legacy PMD
#                                        (not PMX) string type, but pulled in
#                                        transitively by MMDFileString.h below.
#   Saba/Model/MMD/MMDFileString.h    - fixed-size on-disk string helper PMX's
#                                        own header type aliases (unused by
#                                        the vertex/face data itself, only by
#                                        PMXHeader's magic bytes).
#   Saba/Model/MMD/PMXFile.h/.cpp     - the actual .pmx binary parser
#                                        (ReadPMXFile()) - PMXVertex's
#                                        m_position/m_normal/m_uv (all glm
#                                        types) are exactly what
#                                        src/Assets/PmxLoader.cpp reads to
#                                        build this engine's own plain
#                                        Vec3/Vec2 arrays.
#   Saba/Model/MMD/VMDFile.h/.cpp     - the actual .vmd (motion) binary
#                                        parser (ReadVMDFile()) - VMDMotion/
#                                        VMDMorph/VMDCamera/VMDLight/
#                                        VMDShadow/VMDIk are exactly what
#                                        src/Assets/VmdLoader.cpp reads to
#                                        build this engine's own plain
#                                        MotionData (src/Assets/MotionData.h)
#                                        bone/morph/camera/light/shadow/IK
#                                        keyframe tracks. Needs no extra
#                                        dependency beyond what PMXFile.h/.cpp
#                                        above already pulls in (MMDFileString.h
#                                        for its fixed-size on-disk bone/morph/
#                                        model-name strings, Saba/Base/File.h
#                                        for its buffered reader, Saba/Base/
#                                        Log.h - patched, see below - for its
#                                        SABA_WARN() diagnostics).
#   Saba/Base/Log.h                   - PATCHED (see _saba_apply_gte_patches()
#                                        below): upstream's version pulls in
#                                        spdlog (a whole extra vendored
#                                        dependency, external/spdlog/, fetched
#                                        via git submodule upstream) purely to
#                                        back a handful of SABA_INFO/SABA_WARN/
#                                        SABA_ERROR log macros. Since this
#                                        integration never needs saba's actual
#                                        logging output, the staged copy is
#                                        rewritten into a small self-contained
#                                        header (plain stdio, no spdlog/
#                                        Singleton dependency at all) that
#                                        keeps the exact same macro names
#                                        PMXFile.cpp already calls, so
#                                        PMXFile.cpp/.h need zero source
#                                        changes of their own. Base/Log.cpp and
#                                        Base/Singleton.h/.cpp are therefore
#                                        NOT compiled here (nothing left needs
#                                        them once Log.h no longer routes
#                                        through saba::Singleton<Logger>).
#
# NOT vendored/compiled at all: Model/MMD/{MMDModel,MMDNode,MMDIkSolver,
# MMDMorph,MMDMaterial,MMDPhysics,MMDCamera,PMDFile,PMDModel,VMDAnimation,
# VMDCameraAnimation,VPDFile}.*, Model/OBJ/*, Model/XFile/* - these make up
# saba's skeletal-animation/physics runtime and its other model format
# readers, none of which this integration needs yet (VMDFile.h/.cpp - the
# raw .vmd FILE READER, as opposed to VMDAnimation.h/.cpp's higher-level
# playback/interpolation runtime built on top of it - IS vendored/compiled,
# see the curated list above; this line's "VMDFile" entry from the original
# integration was removed once VmdLoader.h/.cpp started needing it). A
# future session adding real bone-deformed skinning/animation PLAYBACK
# (rather than just parsing a .vmd's raw keyframe data, which this
# integration already does) would extend this curated list further (and, at
# that point, also need to fetch+build Bullet, exactly as saba's own
# CMakeLists.txt does).
#
# saba has ONE actual third-party header dependency for the subset above:
# glm (https://github.com/g-truc/glm, header-only) - PMXFile.h #includes
# <glm/vec2.hpp>/<glm/vec3.hpp>/<glm/vec4.hpp>/<glm/gtc/quaternion.hpp>
# directly for PMXVertex/PMXBone/PMXMorph/etc.'s field types. This project
# does not otherwise use glm anywhere (its own math library,
# src/Math/MathTypes.h, was written from scratch on purpose - see AGENTS.md,
# "Entity-Component-System") - glm is fetched here purely as saba's own
# vendored dependency, confined to this one integration boundary
# (src/Assets/PmxLoader.cpp converts every glm::vec3/vec2 to this engine's own
# gte::Vec3/Vec2 immediately after ReadPMXFile() returns - see that file - so
# no other engine code ever needs to include a glm header).
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/saba/   - saba's full source tree (kept
#       whole, for its LICENCE file and so future sessions extending the
#       curated subset above don't need to re-fetch), only the files listed
#       above are actually added to the saba_pmx target's sources.
#   ${CMAKE_SOURCE_DIR}/third_party/saba/.gte_fetched_ref - plain text file
#       recording exactly which resolved ref is currently staged (same
#       convention as every other Fetch*.cmake module).
#   ${CMAKE_SOURCE_DIR}/third_party/glm/    - glm's header-only include tree.
#   ${CMAKE_SOURCE_DIR}/third_party/glm/.gte_fetched_ref
#
# Defines two targets:
#   glm         - INTERFACE library exposing third_party/glm as an include
#                 directory. Header-only, nothing to compile.
#   saba_pmx    - STATIC library compiling exactly the curated subset listed
#                 above; PUBLIC include directory third_party/saba/src (so
#                 consumers `#include <Saba/Model/MMD/PMXFile.h>`, matching
#                 saba's own upstream include convention), PUBLIC-links glm
#                 (PMXFile.h is a public header of this target and itself
#                 includes glm headers, so glm's include dir must propagate
#                 to any consumer of saba_pmx - same "propagate what a public
#                 header needs" reasoning as gte_core's own PUBLIC
#                 target_include_directories() call in the root
#                 CMakeLists.txt).
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   SABA_RELEASE_TAG      - saba git ref to fetch: a branch (e.g. "master") or
#                            a full/abbreviated commit SHA. Defaults to a
#                            PINNED commit SHA (see the `set()` call below)
#                            rather than "master" - saba publishes no GitHub
#                            Releases/tags at all, so a moving branch name is
#                            the only alternative, and pinning removes the
#                            risk of a future upstream commit silently
#                            changing PMXFile's binary-format parsing (or
#                            removing/renaming the curated files this module
#                            lists above) underneath this engine with zero
#                            warning - matches this project's
#                            STB_IMAGE_RELEASE_TAG/IMGUIZMO_RELEASE_TAG
#                            convention (see FetchSTB.cmake/FetchImGuizmo.cmake).
#                            Bump this deliberately to move to a newer commit.
#   SABA_FORCE_REDOWNLOAD  - Set to ON to force re-fetching saba even if
#                            already present and already matching
#                            SABA_RELEASE_TAG.
#   GLM_RELEASE_TAG        - glm git tag to fetch, e.g. "1.0.1". Defaults to
#                            "1.0.1" - a real, concrete GitHub Release tag
#                            (glm DOES publish proper tags, unlike saba
#                            itself), deliberately pinned for the same reason
#                            as SABA_RELEASE_TAG above.
#   GLM_FORCE_REDOWNLOAD   - Set to ON to force re-fetching glm even if
#                            already present and already matching
#                            GLM_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchSaba.cmake only supports Windows. Not supported on this platform.")
endif()

# Pinned to a specific commit (not "master") deliberately - see
# SABA_RELEASE_TAG's own doc comment above for why. This is the commit at the
# tip of master as of integrating the PMX vertex/normal/UV loader; override
# via -DSABA_RELEASE_TAG=... (a branch name or another commit SHA) if a
# deliberate upgrade is ever needed.
set(SABA_RELEASE_TAG "29b8efa8b31c8e746f9a88020fb0ad9dcdcf3332" CACHE STRING
    "saba git ref to fetch (a branch name, e.g. 'master', or a commit SHA - defaults to a pinned commit SHA, see this file's header comment).")
option(SABA_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting saba even if it already appears to be present and matching SABA_RELEASE_TAG."
    OFF)

set(GLM_RELEASE_TAG "1.0.1" CACHE STRING
    "glm git tag to fetch (e.g. '1.0.1').")
option(GLM_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting glm even if it already appears to be present and matching GLM_RELEASE_TAG."
    OFF)

# _saba_download_and_extract_ref(<label> <owner_repo> <ref_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete ref and extracts
# it, returning the single top-level folder GitHub always wraps archive
# contents in. Tries the ref as a branch (refs/heads), then a tag (refs/tags),
# then GitHub's bare "/archive/<ref>.zip" form (the only one that resolves a
# raw commit SHA) - same three-way fallback shape as
# FetchImGuizmo.cmake's _imguizmo_download_and_extract_ref, duplicated locally
# so this module has no include-order dependency on it.
function(_saba_download_and_extract_ref label owner_repo ref_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_${label}_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    string(REPLACE "/" "-" _safe_ref_name "${ref_name}")
    set(_zip_path "${_work_dir}/${label}-${_safe_ref_name}.zip")

    set(_heads_url "https://github.com/${owner_repo}/archive/refs/heads/${ref_name}.zip")
    set(_tags_url "https://github.com/${owner_repo}/archive/refs/tags/${ref_name}.zip")
    set(_commit_url "https://github.com/${owner_repo}/archive/${ref_name}.zip")

    message(STATUS "${label}: downloading ${_heads_url}")
    file(DOWNLOAD "${_heads_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(STATUS "${label}: '${ref_name}' is not a branch (refs/heads) - retrying as a tag (refs/tags)")
        file(DOWNLOAD "${_tags_url}" "${_zip_path}"
            HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
            STATUS _dl_status
            TLS_VERIFY ON
            SHOW_PROGRESS
        )
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            message(STATUS "${label}: '${ref_name}' is not a tag (refs/tags) either - retrying as a bare ref (commit SHA)")
            file(DOWNLOAD "${_commit_url}" "${_zip_path}"
                HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
                STATUS _dl_status
                TLS_VERIFY ON
                SHOW_PROGRESS
            )
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                list(GET _dl_status 1 _dl_msg)
                message(FATAL_ERROR "${label}: failed to download ref '${ref_name}' as a branch, a tag, or a bare/commit ref: ${_dl_msg}")
            endif()
        endif()
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "${label}: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

# _saba_apply_gte_patches(<staged_base_dir>)
#
# Rewrites a freshly-staged Saba/Base/Log.h into a small self-contained
# header with no spdlog/Singleton dependency, keeping the exact same
# SABA_INFO/SABA_WARN/SABA_ERROR/SABA_ASSERT macro names/call shapes
# PMXFile.cpp already uses (see this file's header comment for the full
# rationale). Applied here (rather than hand-editing
# third_party/saba/src/Saba/Base/Log.h directly) so the rewrite survives
# SABA_FORCE_REDOWNLOAD/a clean checkout/CI re-fetching a pristine copy from
# GitHub - third_party/ is gitignored, nothing under it is ever committed
# (see this file's own header comment). This is a full-file replacement
# (unlike FetchKTX.cmake's/FetchImGuizmo.cmake's targeted snippet patches)
# since the goal is to remove an entire dependency, not adjust one bug -
# still guarded the same way: warns (never silently no-ops) if the staged
# file doesn't exist where expected.
function(_saba_apply_gte_patches staged_base_dir)
    set(_log_h_path "${staged_base_dir}/Saba/Base/Log.h")
    if(NOT EXISTS "${_log_h_path}")
        message(WARNING "saba: expected ${_log_h_path} was not found - the spdlog-removal patch was NOT applied. saba's source layout may have changed upstream; re-check cmake/FetchSaba.cmake's _saba_apply_gte_patches().")
        return()
    endif()

    file(WRITE "${_log_h_path}" "//
// GreatTamanaEngine patch - see cmake/FetchSaba.cmake's _saba_apply_gte_patches().
//
// Original file: Copyright(c) 2016-2017 benikabocha, MIT License.
//
// Rewritten to drop upstream's spdlog dependency entirely - this engine
// only vendors saba's raw PMX file-reading layer (see FetchSaba.cmake's
// header comment) and never reads saba's own log output, so pulling in a
// whole extra vendored logging library just for a few diagnostic prints
// isn't worth it. Keeps the exact same macro names/call shape PMXFile.cpp
// (and any future curated saba source) already uses.

#ifndef SABA_BASE_LOG_H_
#define SABA_BASE_LOG_H_

#include <cstdio>
#include <cassert>

#define SABA_INFO(message, ...) ((void)std::fprintf(stdout, \"[saba] \" message \"\\n\"))
#define SABA_WARN(message, ...) ((void)std::fprintf(stderr, \"[saba][warn] \" message \"\\n\"))
#define SABA_ERROR(message, ...) ((void)std::fprintf(stderr, \"[saba][error] \" message \"\\n\"))
#define SABA_ASSERT(expr) assert(expr)

#endif // !SABA_BASE_LOG_H_
")
endfunction()

function(_saba_download_and_stage)
    _saba_download_and_extract_ref("saba" "benikabocha/saba" "${SABA_RELEASE_TAG}" _root)

    if(NOT EXISTS "${_root}/src/Saba/Model/MMD/PMXFile.h"
       OR NOT EXISTS "${_root}/src/Saba/Model/MMD/PMXFile.cpp"
       OR NOT EXISTS "${_root}/src/Saba/Base/File.h"
       OR NOT EXISTS "${_root}/LICENCE")
        message(FATAL_ERROR "saba: extracted package doesn't look like a complete saba source tree (missing src/Saba/Model/MMD/PMXFile.h(.cpp) / src/Saba/Base/File.h / LICENCE).")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/saba")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/saba")
    # Trailing slash on the source path is deliberate: it copies _root's
    # CONTENTS into third_party/saba (flattening away the ref-versioned
    # top-level folder name GitHub's archive wraps everything in), rather
    # than nesting one level deeper. Do not remove it. Only the src/ tree and
    # the LICENCE are kept - saba's own CMakeLists.txt/example/viewer/tools/
    # gtests/external/ (Bullet, GLFW, ImGui-for-the-viewer, lua, sol2, ...)
    # are never needed by this project's own curated build (see this file's
    # header comment) and would otherwise stage several hundred megabytes of
    # unused vendored source for nothing.
    file(COPY "${_root}/src" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/saba")
    file(COPY "${_root}/LICENCE" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/saba")

    _saba_apply_gte_patches("${CMAKE_SOURCE_DIR}/third_party/saba/src")

    file(WRITE "${CMAKE_SOURCE_DIR}/third_party/saba/.gte_fetched_ref" "${SABA_RELEASE_TAG}")

    message(STATUS "saba: staged '${SABA_RELEASE_TAG}' -> ${CMAKE_SOURCE_DIR}/third_party/saba")
endfunction()

function(_glm_download_and_stage)
    _saba_download_and_extract_ref("glm" "g-truc/glm" "${GLM_RELEASE_TAG}" _root)

    if(NOT EXISTS "${_root}/glm/vec3.hpp")
        message(FATAL_ERROR "glm: extracted package doesn't look like a complete glm source tree (missing glm/vec3.hpp).")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/glm")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/glm")
    # Only the glm/ header subfolder itself is staged (not glm's own
    # CMakeLists.txt/test/cmake/manual.md/... - this project only ever
    # needs the headers behind an INTERFACE target, same as
    # FetchVMA.cmake's single-header vk_mem_alloc.h).
    file(COPY "${_root}/glm" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/glm")
    file(COPY "${_root}/copying.txt" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/glm")

    file(WRITE "${CMAKE_SOURCE_DIR}/third_party/glm/.gte_fetched_ref" "${GLM_RELEASE_TAG}")

    message(STATUS "glm: staged '${GLM_RELEASE_TAG}' -> ${CMAKE_SOURCE_DIR}/third_party/glm")
endfunction()

# fetch_glm()
#
# Ensures glm's headers are present in this project (downloading them from
# GitHub if needed), then defines the `glm` INTERFACE target described above.
function(fetch_glm)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_glm() only supports Windows. Not supported on this platform.")
    endif()

    set(_glm_marker "${CMAKE_SOURCE_DIR}/third_party/glm/glm/vec3.hpp")
    set(_glm_ref_marker "${CMAKE_SOURCE_DIR}/third_party/glm/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_glm_marker}" AND EXISTS "${_glm_ref_marker}")
        file(READ "${_glm_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL GLM_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT GLM_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "glm: already present and matching ref '${GLM_RELEASE_TAG}' - skipping download.")
    else()
        _glm_download_and_stage()
    endif()

    if(NOT TARGET glm)
        add_library(glm INTERFACE)
        target_include_directories(glm INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/glm"
        )
    endif()
endfunction()

# fetch_saba()
#
# Ensures saba's source is present in this project (downloading it from
# GitHub if needed, and rewriting its Base/Log.h per
# _saba_apply_gte_patches()), then defines the `saba_pmx` STATIC library
# target described above. Calls fetch_glm() itself first - glm is purely an
# internal dependency of this integration (see this file's header comment),
# so callers only ever need `include(FetchSaba)` + `fetch_saba()`, matching
# every other Fetch*.cmake module's one-call convention.
function(fetch_saba)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_saba() only supports Windows. Not supported on this platform.")
    endif()

    fetch_glm()

    set(_saba_marker "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/PMXFile.h")
    set(_saba_ref_marker "${CMAKE_SOURCE_DIR}/third_party/saba/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_saba_marker}" AND EXISTS "${_saba_ref_marker}")
        file(READ "${_saba_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL SABA_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT SABA_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "saba: already present and matching ref '${SABA_RELEASE_TAG}' - skipping download.")
    else()
        _saba_download_and_stage()
    endif()

    if(NOT TARGET saba_pmx)
        add_library(saba_pmx STATIC
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Base/File.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Base/File.h"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Base/UnicodeUtil.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Base/UnicodeUtil.h"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Base/Log.h"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/SjisToUnicode.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/SjisToUnicode.h"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/MMDFileString.h"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/PMXFile.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/PMXFile.h"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/VMDFile.cpp"
            "${CMAKE_SOURCE_DIR}/third_party/saba/src/Saba/Model/MMD/VMDFile.h"
        )
        target_include_directories(saba_pmx PUBLIC
            "${CMAKE_SOURCE_DIR}/third_party/saba/src"
        )
        target_link_libraries(saba_pmx PUBLIC glm)
    endif()
endfunction()
