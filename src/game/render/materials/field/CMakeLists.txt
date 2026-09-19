# Compile the engine's world templates with this game's material additions.
# The standalone engine shader target has no dependency on this directory.
set(_field_output "${PROJECT_SOURCE_DIR}/.phlosion/materials/field")
file(MAKE_DIRECTORY "${_field_output}")
foreach(_source IN ITEMS field_opengl_declarations.glsl field_opengl_evaluation.glsl
                         field_d3d12_declarations.hlsl field_d3d12_evaluation.hlsl)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_source}" "${_field_output}/${_source}" COPYONLY)
endforeach()
set(_field_templates "${_pac_resolved_engine_source_dir}/assets/shaders/vulkan")
file(GLOB _field_shared CONFIGURE_DEPENDS "${_field_templates}/*.glsl")
set(_field_dependencies
    "${CMAKE_CURRENT_SOURCE_DIR}/project_world_declarations.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/project_world_evaluation.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/project_world_indirect_declarations.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/project_world_indirect_evaluation.glsl")
set(_field_outputs)
foreach(_variant IN ITEMS world world_indirect)
    foreach(_blend IN ITEMS standard dual_source)
        set(_defines -DPHLOSION_PROJECT_MATERIAL=1)
        set(_name "${_variant}")
        if (_blend STREQUAL "dual_source")
            list(APPEND _defines -DPHLOSION_VULKAN_DUAL_SOURCE_BLEND=1)
            string(APPEND _name "_dual_source")
        endif()
        set(_output "${_field_output}/${_name}.frag.spv")
        add_custom_command(OUTPUT "${_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_field_output}"
            COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
                "-I${_field_templates}" "-I${CMAKE_CURRENT_SOURCE_DIR}"
                ${_defines} -o "${_output}" "${_field_templates}/${_variant}.frag"
            DEPENDS "${_field_templates}/${_variant}.frag" ${_field_shared} ${_field_dependencies}
            COMMENT "Compiling game field material ${_name}"
            VERBATIM)
        list(APPEND _field_outputs "${_output}")
    endforeach()
endforeach()
add_custom_target(PAC_FieldMaterialShaders DEPENDS ${_field_outputs})
