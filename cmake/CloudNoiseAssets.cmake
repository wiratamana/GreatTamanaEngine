# Bakes the tileable cloud noise assets once and stages them next to the editor.
#
#   gte_add_cloud_noise_assets(<exe_target>)
#
# tools/cloud_noise_bake is built as a host tool. A custom command runs it only when an output is
# missing or the tool changed, so a no-op build stays fast. The bake needs no network: the blue
# noise source is vendored in tools/cloud_noise_bake/data. The engine loads <exe dir>/assets/clouds.

set(GTE_CLOUD_NOISE_NAMES cloud_shape_128 cloud_detail_32 cloud_weather_512 blue_noise_128)

function(gte_add_cloud_noise_assets exe_target)
    set(_tool_dir "${CMAKE_SOURCE_DIR}/tools/cloud_noise_bake")
    set(_blue_png "${_tool_dir}/data/LDR_LLL1_0.png")
    set(_out_dir "${CMAKE_BINARY_DIR}/assets/clouds")

    add_executable(cloud_noise_bake "${_tool_dir}/main.cpp")
    target_include_directories(cloud_noise_bake PRIVATE "${CMAKE_SOURCE_DIR}/third_party/stb")
    target_link_libraries(cloud_noise_bake PRIVATE Threads::Threads)
    target_link_options(cloud_noise_bake PRIVATE -static)
    target_compile_options(cloud_noise_bake PRIVATE -O2)
    set_target_properties(cloud_noise_bake PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

    set(_outputs)
    foreach(_name ${GTE_CLOUD_NOISE_NAMES})
        list(APPEND _outputs "${_out_dir}/${_name}.gtvol")
    endforeach()

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
        COMMAND cloud_noise_bake bake --out "${_out_dir}" --blue "${_blue_png}"
        DEPENDS cloud_noise_bake "${_blue_png}"
        COMMENT "Baking cloud noise assets into ${_out_dir}"
        VERBATIM)
    add_custom_target(GteCloudNoiseAssets DEPENDS ${_outputs})
    add_dependencies(${exe_target} GteCloudNoiseAssets)
endfunction()
