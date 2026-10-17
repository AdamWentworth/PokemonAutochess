include("${_pac_resolved_engine_source_dir}/cmake/ComposeMaterialProfile.cmake")
phlosion_compose_material_profile(TARGET PAC_WorldMaterialShaders
    OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/.phlosion/materials/world"
    SOURCE_DIRECTORY "${POKEMON_MODEL_SUPPORT_SOURCE_DIR}/shaders/character"
    FIELD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/field")
