# Shared libgcc/libstdc++ linkage for MinGW.
# The host exe and the project assembly DLLs must share one C++ runtime, or objects that cross
# the DLL boundary (std::string, std::vector) are freed by the wrong heap.
#
# GCC has no "-shared-libstdc++" option. Shared libstdc++ is the default when the toolchain was
# built --enable-shared, so only "-shared-libgcc" is added here.
#
#   mingw_copy_runtime_dll(<target>)                     stage the 3 runtime DLLs next to <target>
#   gte_apply_shared_crt_linkage(<target>)               host exe: shared link flag + staged DLLs
#   gte_apply_project_assembly_shared_crt_linkage(<t>)   project DLL: shared link flag only
#
# GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED is TRUE when libstdc++-6.dll sits next to the compiler.
# Without it every function below does nothing and targets stay statically linked.

get_filename_component(GTE_MINGW_TOOLCHAIN_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(EXISTS "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/libstdc++-6.dll")
    set(GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED TRUE CACHE INTERNAL
        "The active compiler ships a shared libstdc++")
else()
    set(GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED FALSE CACHE INTERNAL
        "The active compiler ships a shared libstdc++")
    message(STATUS "No libstdc++-6.dll next to ${CMAKE_CXX_COMPILER}: targets stay statically linked.")
endif()

function(mingw_copy_runtime_dll target_name)
    foreach(dll_name IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/${dll_name}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/${dll_name}"
                    "$<TARGET_FILE_DIR:${target_name}>/${dll_name}"
                COMMENT "Staging ${dll_name} next to ${target_name}")
        else()
            message(WARNING "mingw_copy_runtime_dll: ${dll_name} not found in ${GTE_MINGW_TOOLCHAIN_BIN_DIR}. "
                "${target_name} may not start on a PC without this MinGW toolchain.")
        endif()
    endforeach()
endfunction()

function(gte_apply_shared_crt_linkage target_name)
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
    mingw_copy_runtime_dll(${target_name})
endfunction()

# Project DLLs load next to the host exe, which already stages the runtime DLLs.
function(gte_apply_project_assembly_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PROJECT_ASSEMBLIES OR NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
endfunction()
