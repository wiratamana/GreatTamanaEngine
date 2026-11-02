# Small C/C++ libraries compiled from source. No Vulkan SDK is needed for any of them.
#
#   fetch_spirv_reflect()  KhronosGroup/SPIRV-Reflect -> third_party/spirv_reflect  target spirv_reflect (STATIC C)
#   fetch_mikktspace()     mmikk/MikkTSpace           -> third_party/mikktspace     target mikktspace (STATIC C)
#   fetch_meshoptimizer()  zeux/meshoptimizer         -> third_party/meshoptimizer  target meshoptimizer (STATIC)
#
# Cache: GTE_SPIRV_REFLECT_REF (tag), GTE_MIKKTSPACE_REF (commit), GTE_MESHOPTIMIZER_REF (commit).
#
# meshoptimizer: every staged file is checked against cmake/MeshOptimizerHashes.cmake on each
# configure. After changing GTE_MESHOPTIMIZER_REF, configure once with
# -DGTE_MESHOPT_REGENERATE_HASHES=ON to rewrite that file.
# Saba and glm are in FetchSaba.cmake.

include(GteFetch)

set(GTE_SPIRV_REFLECT_REF "vulkan-sdk-1.4.363.0" CACHE STRING "KhronosGroup/SPIRV-Reflect tag")
set(GTE_MIKKTSPACE_REF "3e895b49d05ea07e4c2133156cfa94369e19e409" CACHE STRING "mmikk/MikkTSpace commit")
set(GTE_MESHOPTIMIZER_REF "9e1f07b159d3cb777f1c67ed31fc11fd117986f4" CACHE STRING "zeux/meshoptimizer commit")
option(GTE_MESHOPT_REGENERATE_HASHES "Rewrite cmake/MeshOptimizerHashes.cmake from the staged files" OFF)

set(_GTE_MESHOPT_HASH_FILE "${CMAKE_CURRENT_LIST_DIR}/MeshOptimizerHashes.cmake")

# Third-party code: no warnings from our compiler.
function(_gte_silence_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

function(fetch_spirv_reflect)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect")
    gte_fetch_raw(SPIRV-Reflect REPO KhronosGroup/SPIRV-Reflect REF "${GTE_SPIRV_REFLECT_REF}" DIR "${_dir}"
        FILES spirv_reflect.h spirv_reflect.c include/spirv/unified1/spirv.h LICENSE
        LEGACY_REFS "local-vendored-from-SPIRV-Reflect-main.zip")
    if(NOT TARGET spirv_reflect)
        add_library(spirv_reflect STATIC "${_dir}/spirv_reflect.c" "${_dir}/spirv_reflect.h")
        target_include_directories(spirv_reflect PUBLIC "${_dir}")
    endif()
endfunction()

function(fetch_mikktspace)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/mikktspace")
    gte_fetch_raw(MikkTSpace REPO mmikk/MikkTSpace REF "${GTE_MIKKTSPACE_REF}" DIR "${_dir}"
        FILES mikktspace.h mikktspace.c)
    if(NOT TARGET mikktspace)
        add_library(mikktspace STATIC "${_dir}/mikktspace.c" "${_dir}/mikktspace.h")
        set_target_properties(mikktspace PROPERTIES LINKER_LANGUAGE C)
        target_include_directories(mikktspace PUBLIC "${_dir}")
        _gte_silence_warnings(mikktspace)
    endif()
endfunction()

# Lists every staged meshoptimizer file (relative, sorted), without the stamp.
function(_gte_meshopt_list_staged dir out_var)
    file(GLOB_RECURSE _all RELATIVE "${dir}" "${dir}/*")
    list(FILTER _all EXCLUDE REGEX "(^|/)\\.gte_fetched_ref$")
    list(SORT _all)
    set(${out_var} "${_all}" PARENT_SCOPE)
endfunction()

function(_gte_meshopt_write_hashes dir)
    _gte_meshopt_list_staged("${dir}" _staged)
    set(_out "# SHA-256 of every staged meshoptimizer file at ${GTE_MESHOPTIMIZER_REF}.\n")
    string(APPEND _out "# Regenerate: configure with -DGTE_MESHOPT_REGENERATE_HASHES=ON\n")
    string(APPEND _out "set(GTE_MESHOPT_SOURCE_HASHES\n")
    foreach(_rel ${_staged})
        file(SHA256 "${dir}/${_rel}" _hash)
        string(APPEND _out "    \"${_rel}=${_hash}\"\n")
    endforeach()
    string(APPEND _out ")\n")
    file(WRITE "${_GTE_MESHOPT_HASH_FILE}" "${_out}")
    message(STATUS "meshoptimizer: wrote ${_GTE_MESHOPT_HASH_FILE}")
endfunction()

# Fatal when a staged file is missing, changed, or not listed in the hash file.
function(_gte_meshopt_verify dir)
    include("${_GTE_MESHOPT_HASH_FILE}")
    set(_expected "")
    foreach(_entry ${GTE_MESHOPT_SOURCE_HASHES})
        string(FIND "${_entry}" "=" _eq)
        string(SUBSTRING "${_entry}" 0 ${_eq} _rel)
        math(EXPR _at "${_eq} + 1")
        string(SUBSTRING "${_entry}" ${_at} -1 _want)
        list(APPEND _expected "${_rel}")
        if(NOT EXISTS "${dir}/${_rel}")
            message(FATAL_ERROR "meshoptimizer: listed file is missing: ${_rel}")
        endif()
        file(SHA256 "${dir}/${_rel}" _have)
        if(NOT _have STREQUAL _want)
            message(FATAL_ERROR "meshoptimizer: SHA-256 mismatch for ${_rel}\n  expected ${_want}\n  actual   ${_have}")
        endif()
    endforeach()
    _gte_meshopt_list_staged("${dir}" _staged)
    foreach(_rel ${_staged})
        if(NOT _rel IN_LIST _expected)
            message(FATAL_ERROR "meshoptimizer: staged file is not in the hash list: ${_rel}")
        endif()
    endforeach()
endfunction()

function(fetch_meshoptimizer)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/meshoptimizer")
    gte_dep_present(_present NAME meshoptimizer REF "${GTE_MESHOPTIMIZER_REF}" DIR "${_dir}"
        REQUIRES "${_dir}/meshoptimizer.h")
    if(NOT _present)
        gte_github_archive(zeux/meshoptimizer "${GTE_MESHOPTIMIZER_REF}" _root)
        gte_require_files("${_root}" src/meshoptimizer.h LICENSE.md)
        file(REMOVE_RECURSE "${_dir}")
        file(MAKE_DIRECTORY "${_dir}")
        file(GLOB _files "${_root}/src/*")
        foreach(_file ${_files})
            if(NOT IS_DIRECTORY "${_file}")
                file(COPY "${_file}" DESTINATION "${_dir}")
            endif()
        endforeach()
        file(COPY "${_root}/LICENSE.md" DESTINATION "${_dir}")
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_MESHOPTIMIZER_REF}")
    endif()

    if(GTE_MESHOPT_REGENERATE_HASHES)
        _gte_meshopt_write_hashes("${_dir}")
    endif()
    _gte_meshopt_verify("${_dir}")

    if(NOT TARGET meshoptimizer)
        file(GLOB _sources "${_dir}/*.cpp")
        list(SORT _sources)
        add_library(meshoptimizer STATIC ${_sources})
        target_include_directories(meshoptimizer PUBLIC "${_dir}")
        _gte_silence_warnings(meshoptimizer)
    endif()
endfunction()
