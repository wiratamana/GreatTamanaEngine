# Vulkan without the Vulkan SDK. Volk loads vulkan-1.dll at runtime, so nothing links against Vulkan.
#
# fetch_vulkan()  Vulkan-Headers -> include/vulkan, include/vk_video
#                 volk           -> third_party/volk
#                 targets: Vulkan::Headers (INTERFACE), volk (STATIC C)
# fetch_vma()     Vulkan Memory Allocator header -> third_party/vma
#                 target: vma (INTERFACE, links Vulkan::Headers). Call after fetch_vulkan().
#
# Cache: GTE_VULKAN_HEADERS_REF, GTE_VOLK_REF, GTE_VMA_REF (tags).

include(GteFetch)

set(GTE_VULKAN_HEADERS_REF "vulkan-sdk-1.4.357.0" CACHE STRING "KhronosGroup/Vulkan-Headers tag")
set(GTE_VOLK_REF "1.4.350" CACHE STRING "zeux/volk tag")
set(GTE_VMA_REF "v3.4.0" CACHE STRING "GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator tag")

function(fetch_vulkan)
    set(_headers_dir "${CMAKE_SOURCE_DIR}/include/vulkan")
    gte_dep_present(_present NAME Vulkan-Headers REF "${GTE_VULKAN_HEADERS_REF}" DIR "${_headers_dir}"
        REQUIRES "${_headers_dir}/vulkan.h")
    if(NOT _present)
        gte_github_archive(KhronosGroup/Vulkan-Headers "${GTE_VULKAN_HEADERS_REF}" _root)
        gte_require_files("${_root}" include/vulkan/vulkan.h include/vk_video)
        file(REMOVE_RECURSE "${_headers_dir}" "${CMAKE_SOURCE_DIR}/include/vk_video")
        file(COPY "${_root}/include/vulkan" "${_root}/include/vk_video" DESTINATION "${CMAKE_SOURCE_DIR}/include")
        gte_dep_stamp(DIR "${_headers_dir}" REF "${GTE_VULKAN_HEADERS_REF}")
    endif()

    gte_fetch_raw(volk REPO zeux/volk REF "${GTE_VOLK_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/volk" FILES volk.h volk.c)

    if(NOT TARGET Vulkan::Headers)
        add_library(VulkanHeaders_interface INTERFACE)
        target_include_directories(VulkanHeaders_interface INTERFACE "${CMAKE_SOURCE_DIR}/include")
        add_library(Vulkan::Headers ALIAS VulkanHeaders_interface)
    endif()

    if(NOT TARGET volk)
        add_library(volk STATIC
            "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c"
            "${CMAKE_SOURCE_DIR}/third_party/volk/volk.h")
        target_include_directories(volk PUBLIC "${CMAKE_SOURCE_DIR}/third_party/volk")
        target_link_libraries(volk PUBLIC Vulkan::Headers)
    endif()
endfunction()

function(fetch_vma)
    if(NOT TARGET Vulkan::Headers)
        message(FATAL_ERROR "fetch_vma() needs fetch_vulkan() first.")
    endif()

    gte_fetch_raw(VMA REPO GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator REF "${GTE_VMA_REF}"
        DIR "${CMAKE_SOURCE_DIR}/third_party/vma" FILES include/vk_mem_alloc.h=vk_mem_alloc.h)

    if(NOT TARGET vma)
        add_library(vma INTERFACE)
        target_include_directories(vma INTERFACE "${CMAKE_SOURCE_DIR}/third_party/vma")
        target_link_libraries(vma INTERFACE Vulkan::Headers)
    endif()
endfunction()
