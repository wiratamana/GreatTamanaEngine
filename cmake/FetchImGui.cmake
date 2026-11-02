# Dear ImGui (docking branch) and ImGuizmo, staged from GitHub archives into third_party/.
#
# fetch_imgui()      third_party/imgui     target: imgui (STATIC)
#                    core + SDL3 platform backend + Vulkan renderer backend.
#                    The Vulkan backend resolves calls through volk (IMGUI_IMPL_VULKAN_USE_VOLK).
#                    Call after fetch_sdl3() and fetch_vulkan().
# fetch_imguizmo()   third_party/imguizmo  target: imguizmo (STATIC, links imgui)
#                    ImGuizmo.h/.cpp only, with two patches (see below). Call after fetch_imgui().
#
# Cache: GTE_IMGUI_REF, GTE_IMGUIZMO_REF (branch, tag or commit SHA).

include(GteFetch)

# "docking" is a moving branch. The editor needs docking, which has no release tags.
# To get reproducible builds, pin it to a commit SHA of ocornut/imgui on the docking branch.
set(GTE_IMGUI_REF "docking" CACHE STRING "ocornut/imgui branch, tag or commit")
set(GTE_IMGUIZMO_REF "18cef5e031d8c6973d80284c67f60549fafd78c1" CACHE STRING
    "CedricGuillemet/ImGuizmo branch, tag or commit")

function(fetch_imgui)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/imgui")
    set(_core imgui.h imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp
        imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h imconfig.h)
    set(_backends backends/imgui_impl_sdl3.h backends/imgui_impl_sdl3.cpp
        backends/imgui_impl_vulkan.h backends/imgui_impl_vulkan.cpp)

    gte_dep_present(_present NAME imgui REF "${GTE_IMGUI_REF}" DIR "${_dir}"
        REQUIRES "${_dir}/imgui.h" "${_dir}/backends/imgui_impl_vulkan.h")
    if(NOT _present)
        gte_github_archive(ocornut/imgui "${GTE_IMGUI_REF}" _root)
        gte_require_files("${_root}" ${_core} ${_backends})
        file(REMOVE_RECURSE "${_dir}")
        foreach(_file ${_core})
            file(COPY "${_root}/${_file}" DESTINATION "${_dir}")
        endforeach()
        foreach(_file ${_backends})
            file(COPY "${_root}/${_file}" DESTINATION "${_dir}/backends")
        endforeach()
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_IMGUI_REF}")
    endif()

    if(NOT TARGET imgui)
        set(_sources imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp
            backends/imgui_impl_sdl3.cpp backends/imgui_impl_vulkan.cpp)
        list(TRANSFORM _sources PREPEND "${_dir}/")
        add_library(imgui STATIC ${_sources})
        target_include_directories(imgui PUBLIC "${_dir}" "${_dir}/backends")
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
        target_link_libraries(imgui PUBLIC SDL3::SDL3 volk)
    endif()
endfunction()

# Upstream bug: HandleScale() and HandleRotation() require mbMouseOver even while a drag is
# already running. A scale or rotate drag freezes when the cursor leaves the SetRect() area.
# The patch requires hover only to start a drag, like HandleTranslation() does.
function(_imguizmo_apply_patches cpp_file)
    set(_note "// GreatTamanaEngine patch (cmake/FetchImGui.cmake)")
    gte_replace_in_file("${cpp_file}"
        "if((!Intersects(op, SCALE) && !Intersects(op, SCALEU)) || type != MT_NONE || !gContext.mbMouseOver)"
        "if((!Intersects(op, SCALE) && !Intersects(op, SCALEU)) || type != MT_NONE || (!gContext.mbMouseOver && !gContext.mbUsing)) ${_note}")
    gte_replace_in_file("${cpp_file}"
        "if(!Intersects(op, ROTATE) || type != MT_NONE || !gContext.mbMouseOver)"
        "if(!Intersects(op, ROTATE) || type != MT_NONE || (!gContext.mbMouseOver && !gContext.mbUsing)) ${_note}")
endfunction()

function(fetch_imguizmo)
    if(NOT TARGET imgui)
        message(FATAL_ERROR "fetch_imguizmo() needs fetch_imgui() first.")
    endif()

    set(_dir "${CMAKE_SOURCE_DIR}/third_party/imguizmo")
    gte_dep_present(_present NAME ImGuizmo REF "${GTE_IMGUIZMO_REF}" DIR "${_dir}"
        REQUIRES "${_dir}/ImGuizmo.h" "${_dir}/ImGuizmo.cpp")
    if(NOT _present)
        gte_github_archive(CedricGuillemet/ImGuizmo "${GTE_IMGUIZMO_REF}" _root)
        # Newer revisions keep the sources in src/, older ones in the repo root.
        set(_src "${_root}/src")
        if(NOT EXISTS "${_src}/ImGuizmo.h")
            set(_src "${_root}")
        endif()
        gte_require_files("${_src}" ImGuizmo.h ImGuizmo.cpp)
        file(REMOVE_RECURSE "${_dir}")
        file(COPY "${_src}/ImGuizmo.h" "${_src}/ImGuizmo.cpp" DESTINATION "${_dir}")
        _imguizmo_apply_patches("${_dir}/ImGuizmo.cpp")
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_IMGUIZMO_REF}")
    endif()

    if(NOT TARGET imguizmo)
        add_library(imguizmo STATIC "${_dir}/ImGuizmo.cpp")
        target_include_directories(imguizmo PUBLIC "${_dir}")
        target_link_libraries(imguizmo PUBLIC imgui)
    endif()
endfunction()
