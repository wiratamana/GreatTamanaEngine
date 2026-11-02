# Saba (PMX/VMD file readers) and glm, staged from GitHub archives.
#
#   fetch_glm()   g-truc/glm -> third_party/glm      target glm (INTERFACE, headers only)
#   fetch_saba()  benikabocha/saba -> third_party/saba
#                 target saba_pmx (STATIC): the file-reader subset only, no Bullet, no spdlog.
#                 PUBLIC include third_party/saba/src, PUBLIC links glm. Calls fetch_glm().
#
# Saba/Base/Log.h is replaced by a small stdio header that keeps the SABA_* macro names.
# Cache: GTE_SABA_REF (commit), GTE_GLM_REF (tag).

include(GteFetch)

set(GTE_SABA_REF "29b8efa8b31c8e746f9a88020fb0ad9dcdcf3332" CACHE STRING "benikabocha/saba commit")
set(GTE_GLM_REF "1.0.1" CACHE STRING "g-truc/glm tag")

function(fetch_glm)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/glm")
    gte_dep_present(_present NAME glm REF "${GTE_GLM_REF}" DIR "${_dir}" REQUIRES "${_dir}/glm/vec3.hpp")
    if(NOT _present)
        gte_github_archive(g-truc/glm "${GTE_GLM_REF}" _root)
        gte_require_files("${_root}" glm/vec3.hpp copying.txt)
        file(REMOVE_RECURSE "${_dir}")
        file(MAKE_DIRECTORY "${_dir}")
        file(COPY "${_root}/glm" "${_root}/copying.txt" DESTINATION "${_dir}")
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_GLM_REF}")
    endif()

    if(NOT TARGET glm)
        add_library(glm INTERFACE)
        target_include_directories(glm INTERFACE "${_dir}")
    endif()
endfunction()

function(_gte_saba_patch_log_h src_dir)
    gte_require_files("${src_dir}" Saba/Base/Log.h)
    file(WRITE "${src_dir}/Saba/Base/Log.h" [=[
//
// GreatTamanaEngine patch - see cmake/FetchSaba.cmake.
//
// Original file: Copyright(c) 2016-2017 benikabocha, MIT License.
//
// Rewritten to drop upstream's spdlog dependency entirely - this engine
// only vendors saba's raw PMX file-reading layer and never reads saba's own
// log output. Keeps the exact same macro names/call shape PMXFile.cpp
// (and any future curated saba source) already uses.

#ifndef SABA_BASE_LOG_H_
#define SABA_BASE_LOG_H_

#include <cstdio>
#include <cassert>

#define SABA_INFO(message, ...) ((void)std::fprintf(stdout, "[saba] " message "\n"))
#define SABA_WARN(message, ...) ((void)std::fprintf(stderr, "[saba][warn] " message "\n"))
#define SABA_ERROR(message, ...) ((void)std::fprintf(stderr, "[saba][error] " message "\n"))
#define SABA_ASSERT(expr) assert(expr)

#endif // !SABA_BASE_LOG_H_
]=])
endfunction()

function(fetch_saba)
    fetch_glm()

    set(_dir "${CMAKE_SOURCE_DIR}/third_party/saba")
    set(_src "${_dir}/src/Saba")
    gte_dep_present(_present NAME saba REF "${GTE_SABA_REF}" DIR "${_dir}"
        REQUIRES "${_src}/Model/MMD/PMXFile.h")
    if(NOT _present)
        gte_github_archive(benikabocha/saba "${GTE_SABA_REF}" _root)
        gte_require_files("${_root}" src/Saba/Model/MMD/PMXFile.h src/Saba/Model/MMD/PMXFile.cpp
            src/Saba/Base/File.h LICENCE)
        file(REMOVE_RECURSE "${_dir}")
        file(MAKE_DIRECTORY "${_dir}")
        file(COPY "${_root}/src" "${_root}/LICENCE" DESTINATION "${_dir}")
        _gte_saba_patch_log_h("${_dir}/src")
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_SABA_REF}")
    endif()

    if(NOT TARGET saba_pmx)
        add_library(saba_pmx STATIC
            "${_src}/Base/File.cpp"
            "${_src}/Base/File.h"
            "${_src}/Base/UnicodeUtil.cpp"
            "${_src}/Base/UnicodeUtil.h"
            "${_src}/Base/Log.h"
            "${_src}/Model/MMD/SjisToUnicode.cpp"
            "${_src}/Model/MMD/SjisToUnicode.h"
            "${_src}/Model/MMD/MMDFileString.h"
            "${_src}/Model/MMD/PMXFile.cpp"
            "${_src}/Model/MMD/PMXFile.h"
            "${_src}/Model/MMD/VMDFile.cpp"
            "${_src}/Model/MMD/VMDFile.h")
        target_include_directories(saba_pmx PUBLIC "${_dir}/src")
        target_link_libraries(saba_pmx PUBLIC glm)
    endif()
endfunction()
