# Compose project-owned field and character programs against the engine's
# versioned world vertex/fragment interface. The engine builds independently.
set(_material_output "${PROJECT_SOURCE_DIR}/.phlosion/materials/world")
file(MAKE_DIRECTORY "${_material_output}")
set(_material_templates "${_pac_resolved_engine_source_dir}/assets/shaders/vulkan")
file(GLOB _material_engine_includes CONFIGURE_DEPENDS "${_material_templates}/*.glsl")
file(GLOB _material_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/character/*.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/character/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/field/*.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/field/*.hlsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_material_sources})

foreach(_api IN ITEMS opengl d3d12 world world_indirect)
    if (_api STREQUAL "d3d12")
        set(_extension hlsl)
    else()
        set(_extension glsl)
    endif()
    foreach(_part IN ITEMS declarations evaluation)
        if (_api MATCHES "^world")
            set(_field_name "project_${_api}_${_part}.glsl")
            set(_output_name "project_${_api}_${_part}.glsl")
        else()
            set(_field_name "field_${_api}_${_part}.${_extension}")
            set(_output_name "${_api}_${_part}.${_extension}")
        endif()
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/field/${_field_name}" _field)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/character/${_api}_${_part}.${_extension}" _character)
        string(TOUPPER "${_part}" _part_upper)
        string(REPLACE "__AUTOCHESS_FIELD_${_part_upper}__" "${_field}" _composed "${_character}")
        file(CONFIGURE OUTPUT "${_material_output}/${_output_name}" CONTENT "${_composed}" @ONLY)
    endforeach()
    if (_api MATCHES "^world")
        configure_file("character/${_api}_vertex_evaluation.glsl"
            "${_material_output}/project_${_api}_vertex_evaluation.glsl" COPYONLY)
    else()
        foreach(_part IN ITEMS declarations evaluation)
            configure_file("character/${_api}_vertex_${_part}.${_extension}"
                "${_material_output}/${_api}_vertex_${_part}.${_extension}" COPYONLY)
        endforeach()
    endif()
endforeach()
foreach(_source IN ITEMS character_lighting.glsl layered_effects.glsl)
    configure_file("character/${_source}" "${_material_output}/${_source}" COPYONLY)
endforeach()

set(_material_outputs)
foreach(_variant IN ITEMS world world_indirect)
    foreach(_stage IN ITEMS vert frag)
        if (_stage STREQUAL "frag")
            set(_blends standard dual_source)
        else()
            set(_blends standard)
        endif()
        foreach(_blend IN LISTS _blends)
            set(_defines -DPHLOSION_PROJECT_MATERIAL=1)
            set(_name "${_variant}")
            if (_blend STREQUAL "dual_source")
                list(APPEND _defines -DPHLOSION_VULKAN_DUAL_SOURCE_BLEND=1)
                string(APPEND _name "_dual_source")
            endif()
            set(_output "${_material_output}/${_name}.${_stage}.spv")
            add_custom_command(OUTPUT "${_output}"
                COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
                    "-I${_material_templates}" "-I${_material_output}" ${_defines}
                    -o "${_output}" "${_material_templates}/${_variant}.${_stage}"
                DEPENDS "${_material_templates}/${_variant}.${_stage}" ${_material_engine_includes} ${_material_sources}
                COMMENT "Compiling game world material ${_name}.${_stage}" VERBATIM)
            list(APPEND _material_outputs "${_output}")
        endforeach()
    endforeach()
endforeach()
add_custom_target(PAC_WorldMaterialShaders DEPENDS ${_material_outputs})
