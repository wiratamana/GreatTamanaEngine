# SDL3: prebuilt "devel VC" release from github.com/libsdl-org/SDL, staged as
#   include/SDL3/*.h, lib/SDL3.lib, SDL3.dll   (all gitignored, in CMAKE_SOURCE_DIR)
#
# fetch_sdl3()                     stages SDL3 and defines SDL3::SDL3 (IMPORTED, shared)
# sdl3_copy_runtime_dll(<target>)  copies SDL3.dll next to <target> after each build
#
# Cache: GTE_SDL3_REF = release tag, e.g. "release-3.4.14".

include(GteFetch)

set(GTE_SDL3_REF "release-3.4.14" CACHE STRING "SDL3 release tag (libsdl-org/SDL)")

function(fetch_sdl3)
    set(_include_dir "${CMAKE_SOURCE_DIR}/include/SDL3")
    set(_dll "${CMAKE_SOURCE_DIR}/SDL3.dll")
    set(_lib "${CMAKE_SOURCE_DIR}/lib/SDL3.lib")

    gte_dep_present(_present NAME SDL3 REF "${GTE_SDL3_REF}" DIR "${_include_dir}"
        REQUIRES "${_include_dir}/SDL.h" "${_dll}" "${_lib}")
    if(NOT _present)
        string(REPLACE "release-" "" _version "${GTE_SDL3_REF}")
        set(_zip_name "SDL3-devel-${_version}-VC.zip")
        set(_work "${_GTE_FETCH_WORK_DIR}/SDL3")
        gte_download("https://github.com/libsdl-org/SDL/releases/download/${GTE_SDL3_REF}/${_zip_name}"
            "${_work}/${_zip_name}")
        gte_extract_zip("${_work}/${_zip_name}" "${_work}/extracted" _root)
        gte_require_files("${_root}" include/SDL3/SDL.h lib/x64/SDL3.dll lib/x64/SDL3.lib)

        file(REMOVE_RECURSE "${_include_dir}")
        file(COPY "${_root}/include/SDL3" DESTINATION "${CMAKE_SOURCE_DIR}/include")
        file(COPY "${_root}/lib/x64/SDL3.lib" DESTINATION "${CMAKE_SOURCE_DIR}/lib")
        file(COPY "${_root}/lib/x64/SDL3.dll" DESTINATION "${CMAKE_SOURCE_DIR}")
        gte_dep_stamp(DIR "${_include_dir}" REF "${GTE_SDL3_REF}")
    endif()

    if(NOT TARGET SDL3_prebuilt)
        add_library(SDL3_prebuilt SHARED IMPORTED GLOBAL)
        set_target_properties(SDL3_prebuilt PROPERTIES
            IMPORTED_LOCATION "${_dll}"
            IMPORTED_IMPLIB "${_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include")
        add_library(SDL3::SDL3 ALIAS SDL3_prebuilt)
    endif()

    set(SDL3_DLL_PATH "${_dll}" CACHE INTERNAL "Path to the staged SDL3.dll")
endfunction()

function(sdl3_copy_runtime_dll target)
    if(NOT DEFINED SDL3_DLL_PATH)
        message(FATAL_ERROR "sdl3_copy_runtime_dll(): call fetch_sdl3() first.")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL3_DLL_PATH}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying SDL3.dll next to ${target}")
endfunction()
