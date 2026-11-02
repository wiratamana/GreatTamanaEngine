# Single-header libraries, fetched from raw.githubusercontent.com into third_party/.
# Each function stages the header and defines an INTERFACE target.
#
#   fetch_stb()              stb_image.h        target stb_image
#   fetch_stb_image_write()  stb_image_write.h  target stb_image_write
#   fetch_httplib()          httplib.h          target httplib (links ws2_32 crypt32)
#   fetch_json()             nlohmann/json.hpp  target nlohmann_json
#   fetch_cgltf()            cgltf.h            target cgltf
#
# The *_IMPLEMENTATION macros are defined by the engine sources, not here.
# Cache: GTE_STB_REF, GTE_HTTPLIB_REF, GTE_JSON_REF, GTE_CGLTF_REF (tag or commit).

include(GteFetch)

set(GTE_STB_REF "2c980bb59875b0d32144a71867fbdebb2f77cd20" CACHE STRING "nothings/stb commit")
set(GTE_HTTPLIB_REF "v0.54.1" CACHE STRING "yhirose/cpp-httplib tag")
set(GTE_JSON_REF "v3.12.0" CACHE STRING "nlohmann/json tag")
set(GTE_CGLTF_REF "v1.15" CACHE STRING "jkuhlmann/cgltf tag")

function(fetch_stb)
    gte_fetch_raw(stb_image REPO nothings/stb REF "${GTE_STB_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/stb" FILES stb_image.h)
    if(NOT TARGET stb_image)
        add_library(stb_image INTERFACE)
        target_include_directories(stb_image INTERFACE "${CMAKE_SOURCE_DIR}/third_party/stb")
    endif()
endfunction()

function(fetch_stb_image_write)
    gte_fetch_raw(stb_image_write REPO nothings/stb REF "${GTE_STB_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/stb" REF_FILE ".gte_fetched_ref_write"
        FILES stb_image_write.h)
    if(NOT TARGET stb_image_write)
        add_library(stb_image_write INTERFACE)
        target_include_directories(stb_image_write INTERFACE "${CMAKE_SOURCE_DIR}/third_party/stb")
    endif()
endfunction()

function(fetch_httplib)
    gte_fetch_raw(httplib REPO yhirose/cpp-httplib REF "${GTE_HTTPLIB_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/httplib" FILES httplib.h)
    if(NOT TARGET httplib)
        add_library(httplib INTERFACE)
        target_include_directories(httplib INTERFACE "${CMAKE_SOURCE_DIR}/third_party/httplib")
        target_link_libraries(httplib INTERFACE ws2_32 crypt32)
    endif()
endfunction()

function(fetch_json)
    gte_fetch_raw(nlohmann_json REPO nlohmann/json REF "${GTE_JSON_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/json"
        FILES single_include/nlohmann/json.hpp=nlohmann/json.hpp)
    if(NOT TARGET nlohmann_json)
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE "${CMAKE_SOURCE_DIR}/third_party/json")
    endif()
endfunction()

function(fetch_cgltf)
    gte_fetch_raw(cgltf REPO jkuhlmann/cgltf REF "${GTE_CGLTF_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/cgltf" FILES cgltf.h)
    if(NOT TARGET cgltf)
        add_library(cgltf INTERFACE)
        target_include_directories(cgltf INTERFACE "${CMAKE_SOURCE_DIR}/third_party/cgltf")
    endif()
endfunction()
