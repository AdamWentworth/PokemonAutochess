# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(PokemonAutochess LANGUAGES CXX)

option(PAC_VERBOSE_STARTUP "Enable verbose startup/model-load logging" OFF)
option(PAC_BUILD_TOOLS "Build developer tools (data cooker, VFX previewer)" ON)
option(PAC_BUILD_EDITOR "Build the Pokemon Autochess project plugin for Phlosion Editor" ON)

set(_pac_werror_default OFF)
if (DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "" AND NOT "$ENV{CI}" STREQUAL "0")
    set(_pac_werror_default ON)
endif()
option(PAC_ENABLE_WARNINGS "Enable compiler warning flags for project sources" ON)
option(PAC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for project sources" ${_pac_werror_default})
option(PAC_ENABLE_RUNTIME_SMOKE_TESTS "Run short executable startup smoke tests for renderer backends" OFF)
option(PAC_OPTIMIZE_RENDER_HOTPATHS_IN_DEBUG
    "Compile selected render/runtime hotpath files with optimization in Debug"
    ON)
option(PAC_OPTIMIZE_D3D12_SHADERS_IN_DEBUG
    "Compile D3D12 shaders with optimization plus debug info in Debug"
    ON)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- Dependencies ----------------
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Vulkan REQUIRED)


find_package(sol2 CONFIG REQUIRED)
if (TARGET sol2::sol2)
    set(PAC_SOL2_TARGET sol2::sol2)
elseif (TARGET sol2)
    set(PAC_SOL2_TARGET sol2)
else()
    message(FATAL_ERROR "sol2 package found, but no known CMake target (sol2::sol2 or sol2) is available.")
endif()

if (TARGET SDL2_ttf::SDL2_ttf)
    set(PAC_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf)
elseif (TARGET SDL2_ttf::SDL2_ttf-static)
    set(PAC_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf-static)
else()
    message(FATAL_ERROR "SDL2_ttf package found, but no known CMake target (SDL2_ttf::SDL2_ttf or SDL2_ttf::SDL2_ttf-static) is available.")
endif()

set(PROJECT_SRC_ROOT "${CMAKE_SOURCE_DIR}/src")

if (PAC_OPTIMIZE_RENDER_HOTPATHS_IN_DEBUG)
    if (MSVC)
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endif()

    set(_pac_hotpath_sources
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/GameSession.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionProjectedWorldView.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionBackendRenderHelpers.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionRenderScratch.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionWorldRenderRuntime.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/core/SharedProjectedRenderItems.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCpuRewrite.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGpuSkinBatchState.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGeometryTemplateCache.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedFinalize.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTrianglePrep.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastTriangleAppend.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastPath.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGraphicsQuality.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldGrowlBridge.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldScratchBridge.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTackleBridge.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldParticleVfxBridge.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldVfxBridges.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldCaptureBridge.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleSubmit.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/unit/SharedProjectedUnitOverlays.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/scene/SharedWorldScene.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/scene/LgpeRoute1ProjectedShadow.cpp"
    )
    if (MSVC)
        foreach(_src IN LISTS _pac_hotpath_sources)
            set_property(SOURCE ${_src} APPEND PROPERTY COMPILE_OPTIONS
                "$<$<CONFIG:Debug>:/O2>")
            set_source_files_properties(${_src} PROPERTIES MSVC_RUNTIME_CHECKS "")
        endforeach()
    else()
        foreach(_src IN LISTS _pac_hotpath_sources)
            set_property(SOURCE ${_src} APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
        endforeach()
    endif()
endif()

# ---------------- Common target settings ----------------
function(pac_apply_warning_flags target_name)
    if (NOT PAC_ENABLE_WARNINGS)
        return()
    endif()

    if (MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
        if (PAC_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if (PAC_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(pac_apply_common_target_settings target_name)
    get_target_property(_type ${target_name} TYPE)

    if (_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${target_name}
            INTERFACE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            INTERFACE
                PHLOSION_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )
    else()
        target_include_directories(${target_name}
            PRIVATE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            PRIVATE
                PHLOSION_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )

        if (MSVC)
            target_compile_options(${target_name} PRIVATE /std:c++20 /permissive- /Zc:__cplusplus /FS /MP)
        else()
            target_compile_options(${target_name} PRIVATE -std=c++20)
        endif()
        pac_apply_warning_flags(${target_name})
    endif()
endfunction()

# ---------------- Phlosion Engine dependency ----------------
set(PHLOSION_VERBOSE_STARTUP ${PAC_VERBOSE_STARTUP} CACHE BOOL
    "Enable verbose Phlosion startup logging" FORCE)
set(PHLOSION_OPTIMIZE_D3D12_SHADERS_IN_DEBUG
    ${PAC_OPTIMIZE_D3D12_SHADERS_IN_DEBUG} CACHE BOOL
    "Optimize Phlosion D3D12 shaders in Debug" FORCE)
set(PHLOSION_BUILD_EDITOR OFF CACHE BOOL
    "Build the Engine-owned editor inside the game dependency graph" FORCE)
set(PHLOSION_ENGINE_GIT_REPOSITORY
    "https://github.com/AdamWentworth/PhlosionEngine.git"
    CACHE STRING
    "Phlosion Engine Git repository")
set(PHLOSION_ENGINE_GIT_TAG
    "fdb1e18575eee853ef5025c3a33c6b6e4dc46316"
    CACHE STRING
    "Pinned Phlosion Engine commit")

# Local platform checkouts live together under a reusable Phlosion workspace,
# independently of any individual game repository.  Prefer an explicit cache
# value, then the shared environment setting, then the standard workspace
# layout.  The final branch retains compatibility with the former sibling-repo
# layout for contributors who have not migrated yet.
set(_pac_dev_root_default "")
if (DEFINED ENV{PHLOSION_DEV_ROOT}
        AND EXISTS "$ENV{PHLOSION_DEV_ROOT}/PhlosionEngine/CMakeLists.txt")
    file(TO_CMAKE_PATH "$ENV{PHLOSION_DEV_ROOT}" _pac_dev_root_default)
elseif (EXISTS "${CMAKE_SOURCE_DIR}/../../Phlosion/PhlosionEngine/CMakeLists.txt")
    get_filename_component(
        _pac_dev_root_default
        "${CMAKE_SOURCE_DIR}/../../Phlosion"
        ABSOLUTE)
endif()
set(PHLOSION_DEV_ROOT
    "${_pac_dev_root_default}"
    CACHE PATH
    "Root containing local PhlosionEngine, PhlosionPackages, and PhlosionVFX checkouts")

set(_pac_sibling_engine "")
if (PHLOSION_DEV_ROOT)
    set(_pac_sibling_engine "${PHLOSION_DEV_ROOT}/PhlosionEngine")
elseif (EXISTS "${CMAKE_SOURCE_DIR}/../PhlosionEngine/CMakeLists.txt")
    set(_pac_sibling_engine "${CMAKE_SOURCE_DIR}/../PhlosionEngine")
endif()
set(_pac_engine_source_default "")
if (EXISTS "${_pac_sibling_engine}/CMakeLists.txt")
    set(_pac_engine_source_default "${_pac_sibling_engine}")
endif()
set(PHLOSION_ENGINE_SOURCE_DIR
    "${_pac_engine_source_default}"
    CACHE PATH
    "Local Phlosion Engine checkout; empty downloads the pinned commit")

set(PHLOSION_BUILD_TESTS OFF CACHE BOOL
    "Build Phlosion Engine tests as part of Pokemon Autochess" FORCE)
if (PHLOSION_ENGINE_SOURCE_DIR)
    if (NOT EXISTS "${PHLOSION_ENGINE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "PHLOSION_ENGINE_SOURCE_DIR does not contain CMakeLists.txt: "
            "${PHLOSION_ENGINE_SOURCE_DIR}")
    endif()
    add_subdirectory(
        "${PHLOSION_ENGINE_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/phlosion-engine")
    set(_pac_resolved_engine_source_dir "${PHLOSION_ENGINE_SOURCE_DIR}")
    message(STATUS
        "Pokemon Autochess is using local Phlosion Engine: "
        "${PHLOSION_ENGINE_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(
        PhlosionEngine
        GIT_REPOSITORY "${PHLOSION_ENGINE_GIT_REPOSITORY}"
        GIT_TAG "${PHLOSION_ENGINE_GIT_TAG}"
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(PhlosionEngine)
    FetchContent_GetProperties(
        PhlosionEngine
        SOURCE_DIR _pac_resolved_engine_source_dir)
    message(STATUS
        "Pokemon Autochess is using pinned Phlosion Engine commit: "
        "${PHLOSION_ENGINE_GIT_TAG}")
endif()

if (NOT TARGET Phlosion::Engine OR NOT TARGET Engine)
    message(FATAL_ERROR "Phlosion Engine did not provide its required CMake targets.")
endif()

# ---------------- Optional Phlosion editor packages ----------------
# First-party tooling lives in the PhlosionPackages monorepo. It is
# included only when this game builds its editor integration; shipped gameplay
# targets do not load or link editor package code.
if (PAC_BUILD_EDITOR)
    set(_pac_sibling_packages "")
    if (PHLOSION_DEV_ROOT)
        set(_pac_sibling_packages
            "${PHLOSION_DEV_ROOT}/PhlosionPackages")
    elseif (EXISTS "${CMAKE_SOURCE_DIR}/../PhlosionPackages/CMakeLists.txt")
        set(_pac_sibling_packages
            "${CMAKE_SOURCE_DIR}/../PhlosionPackages")
    endif()
    set(_pac_packages_source_default "")
    if (EXISTS "${_pac_sibling_packages}/CMakeLists.txt")
        set(_pac_packages_source_default
            "${_pac_sibling_packages}")
    endif()
    set(PHLOSION_PACKAGES_SOURCE_DIR
        "${_pac_packages_source_default}"
        CACHE PATH
        "Local PhlosionPackages checkout")
    if (NOT EXISTS
        "${PHLOSION_PACKAGES_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "PAC_BUILD_EDITOR requires a PhlosionPackages checkout. "
            "Set PHLOSION_PACKAGES_SOURCE_DIR to the monorepo root.")
    endif()
    set(PHLOSION_PACKAGES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PHLOSION_PACKAGES_BUILD_EDITOR ON CACHE BOOL "" FORCE)
    set(PHLOSION_PACKAGE_OUTPUT_ROOT
        "${CMAKE_SOURCE_DIR}/.phlosion/packages"
        CACHE PATH "" FORCE)
    add_subdirectory(
        "${PHLOSION_PACKAGES_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/phlosion-packages")
    set(_pac_resolved_packages_source_dir
        "${PHLOSION_PACKAGES_SOURCE_DIR}")
    if (NOT TARGET PhlosionPackages::TileToolsEditor)
        message(FATAL_ERROR
            "PhlosionPackages did not provide phlosion.tile-tools.")
    endif()
endif()

# ---------------- Phlosion VFX dependency ----------------
set(PHLOSION_VFX_GIT_REPOSITORY
    "https://github.com/AdamWentworth/PhlosionVFX.git"
    CACHE STRING
    "Phlosion VFX Git repository")
set(PHLOSION_VFX_GIT_TAG
    "cf1c81a5d7eb368d9ef079f0927218bd3bb0f20b"
    CACHE STRING
    "Pinned Phlosion VFX commit")
set(_pac_sibling_vfx "")
if (PHLOSION_DEV_ROOT)
    set(_pac_sibling_vfx "${PHLOSION_DEV_ROOT}/PhlosionVFX")
elseif (EXISTS "${CMAKE_SOURCE_DIR}/../PhlosionVFX/CMakeLists.txt")
    set(_pac_sibling_vfx "${CMAKE_SOURCE_DIR}/../PhlosionVFX")
endif()
set(_pac_vfx_source_default "")
if (EXISTS "${_pac_sibling_vfx}/CMakeLists.txt")
    set(_pac_vfx_source_default "${_pac_sibling_vfx}")
endif()
set(PHLOSION_VFX_SOURCE_DIR
    "${_pac_vfx_source_default}"
    CACHE PATH
    "Local Phlosion VFX checkout; empty downloads the pinned commit")

set(PHLOSION_VFX_BUILD_TESTS OFF CACHE BOOL
    "Build Phlosion VFX tests as part of Pokemon Autochess" FORCE)
if (PHLOSION_VFX_SOURCE_DIR)
    if (NOT EXISTS "${PHLOSION_VFX_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "PHLOSION_VFX_SOURCE_DIR does not contain CMakeLists.txt: "
            "${PHLOSION_VFX_SOURCE_DIR}")
    endif()
    add_subdirectory(
        "${PHLOSION_VFX_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/phlosion-vfx")
    set(_pac_resolved_vfx_source_dir "${PHLOSION_VFX_SOURCE_DIR}")
    message(STATUS
        "Pokemon Autochess is using local Phlosion VFX: "
        "${PHLOSION_VFX_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(
        PhlosionVFX
        GIT_REPOSITORY "${PHLOSION_VFX_GIT_REPOSITORY}"
        GIT_TAG "${PHLOSION_VFX_GIT_TAG}"
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(PhlosionVFX)
    FetchContent_GetProperties(
        PhlosionVFX
        SOURCE_DIR _pac_resolved_vfx_source_dir)
    message(STATUS
        "Pokemon Autochess is using pinned Phlosion VFX commit: "
        "${PHLOSION_VFX_GIT_TAG}")
endif()

if (NOT TARGET PhlosionVFX::Runtime OR NOT TARGET PhlosionVFX::Preview)
    message(FATAL_ERROR "Phlosion VFX did not provide its required CMake targets.")
endif()

# ---------------- Game objects (shared by exe + tests) ----------------
add_library(PAC_GameObjects STATIC
    # Project-owned source format adapters
    src/game/assets/lgpe/LgpeCanonicalScene.cpp

    # Game Core
    src/game/runtime/GameApp.cpp
    src/game/runtime/GameRunner.cpp
    src/game/runtime/RuntimeGameRunnerEntry.cpp
    src/game/world/GameWorld.cpp
    src/game/world/GameWorldProgression.cpp
    src/game/world/GameWorldRoster.cpp
    src/game/world/GameWorldEconomy.cpp
    src/game/world/GameWorldCapture.cpp
    src/game/world/GameWorldQueries.cpp
    src/game/world/GameWorldRecovery.cpp
    src/game/world/GameWorldLeechSeed.cpp
    src/game/world/GameWorldDebugSnapshot.cpp
    src/game/world/GameWorldSharedSnapshots.cpp
    src/game/world/GameWorldAnimation.cpp
    src/game/world/MoveImpactRouting.cpp
    src/game/world/MoveImpactMath.cpp
    src/game/world/GameWorldMoveImpactGrowl.cpp
    src/game/world/GameWorldMoveImpactStyles.cpp
    src/game/world/GameWorldMoveImpact.cpp
    src/game/world/GameWorldVfx.cpp
    src/game/GameStateManager.cpp
    src/game/GameConfig.cpp
    src/game/runtime/GamePreload.cpp
    src/game/runtime/GameRuntime.cpp
    src/game/runtime/GameBootstrap.cpp
    src/game/runtime/startup/RuntimeRenderModelPrewarm.cpp
    src/game/runtime/startup/RuntimeGrowlVfxPrewarm.cpp
    src/game/runtime/startup/RuntimeScratchVfxPrewarm.cpp
    src/game/runtime/startup/RuntimeTackleVfxPrewarm.cpp
    src/game/runtime/startup/RuntimeParticleVfxPrewarm.cpp
    src/game/runtime/startup/RuntimeUiCardPrewarm.cpp
    src/game/runtime/startup/RuntimeStartupAssetPrewarm.cpp
    src/game/runtime/RuntimeBootLoading.cpp
    src/game/runtime/renderer/RendererBackendBootstrap.cpp
    src/game/runtime/loop/RuntimeGameRunnerEventPump.cpp
    src/game/runtime/loop/RuntimeGameRunnerFrameExecution.cpp
    src/game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.cpp
    src/game/runtime/loop/RuntimeGameRunnerLoopPolicy.cpp
    src/game/runtime/loop/RuntimeFixedStepPhase.cpp
    src/game/runtime/loop/RuntimeFrameObservation.cpp
    src/game/runtime/renderer/RendererStartupDiagnostics.cpp
    src/game/runtime/loop/RuntimeFramePerfCapture.cpp
    src/game/runtime/loop/RuntimeLoopConfig.cpp
    src/game/runtime/loop/RuntimeLoopControl.cpp
    src/game/runtime/RuntimeOpenGlBootstrap.cpp
    src/game/runtime/loop/RuntimePerfAccumulator.cpp
    src/game/runtime/loop/RuntimePerfLogging.cpp
    src/game/runtime/RuntimeRelaunchLoop.cpp
    src/game/runtime/renderer/RuntimeRendererActivation.cpp
    src/game/runtime/renderer/RuntimeGameRunnerRendererBootstrap.cpp
    src/game/runtime/renderer/RuntimeRendererRecovery.cpp
    src/game/runtime/renderer/RuntimeRendererStartupState.cpp
    src/game/runtime/RuntimeRestartPolicy.cpp
    src/game/runtime/video/RuntimeSdlEventDispatch.cpp
    src/game/runtime/video/RuntimeSdlInput.cpp
    src/game/runtime/video/RuntimeGameRunnerWindowBootstrap.cpp
    src/game/runtime/video/RuntimeSdlVideoMode.cpp
    src/game/runtime/video/RuntimeWindowPresentationController.cpp
    src/game/runtime/startup/RuntimeStartupConfig.cpp
    src/game/runtime/startup/RuntimeGameRunnerStartupFinalize.cpp
    src/game/runtime/startup/RuntimeStartupPresentation.cpp
    src/game/runtime/startup/RuntimeStartupSession.cpp
    src/game/runtime/startup/RuntimeStartupVideoOverride.cpp
    src/game/runtime/startup/RuntimeWorldLayerPrewarm.cpp
    src/game/runtime/video/RuntimeWindowBootstrap.cpp
    src/game/runtime/session/GameSession.cpp
    src/game/runtime/session/SessionBackendAssetBridge.cpp
    src/game/runtime/session/SessionBackendInventoryUi.cpp
    src/game/runtime/session/SessionCoordinatorBridge.cpp
    src/game/runtime/session/SessionBackendUnitHydration.cpp
    src/game/runtime/session/SessionBackendRenderHelpers.cpp
    src/game/runtime/session/SessionCoreBootstrapRuntime.cpp
    src/game/runtime/session/SessionDebugSnapshot.cpp
    src/game/runtime/session/SessionInitBridge.cpp
    src/game/runtime/session/SessionInventoryBridge.cpp
    src/game/runtime/session/SessionLegacyWorldView.cpp
    src/game/runtime/session/SessionLifecycleBridge.cpp
    src/game/runtime/session/SessionFrameMetrics.cpp
    src/game/runtime/session/SessionLoopBridge.cpp
    src/game/runtime/session/SessionLoopRuntime.cpp
    src/game/runtime/session/SessionProjectedWorldView.cpp
    src/game/runtime/session/SessionRenderLayout.cpp
    src/game/runtime/session/SessionRenderBridge.cpp
    src/game/runtime/session/SessionRenderScratch.cpp
    src/game/runtime/session/SessionRenderConfig.cpp
    src/game/runtime/session/SessionSnapshotController.cpp
    src/game/runtime/session/SessionSnapshotRuntime.cpp
    src/game/runtime/session/SessionStartupBridge.cpp
    src/game/runtime/session/SessionStartupRuntime.cpp
    src/game/runtime/session/SessionTextureCache.cpp
    src/game/runtime/session/SessionWorldLayerBridge.cpp
    src/game/runtime/session/SessionWorldRenderRuntime.cpp
    src/game/runtime/session/SessionWorldBackdrop.cpp
    src/game/runtime/shared/backend/SharedBackendPoseEval.cpp
    src/game/runtime/shared/capture/SharedCapturePresentation.cpp
    src/game/runtime/shared/capture/SharedCaptureOverlayVfx.cpp
    src/game/runtime/shared/capture/SharedCaptureModelBridge.cpp
    src/game/runtime/shared/capture/SharedCaptureD3d12FastPath.cpp
    src/game/runtime/shared/capture/SharedCapturePreparedMeshCache.cpp
    src/game/runtime/shared/world/SharedBoardGridBatches.cpp
    src/game/runtime/shared/world/SharedWorldContentSubmit.cpp
    src/game/runtime/shared/projected/core/SharedProjectedBodyPresentation.cpp
    src/game/runtime/shared/projected/core/SharedProjectedDebugVfx.cpp
    src/game/runtime/shared/projected/core/SharedPreviewBodyPresentationPath.cpp
    src/game/runtime/shared/projected/core/SharedProjectedRenderItems.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCpuRewrite.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGpuSkinBatchState.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGeometryTemplateCache.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedFinalize.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTrianglePrep.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleLoop.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastTriangleAppend.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastPath.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGraphicsQuality.cpp
    src/game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldGrowlBridge.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldScratchBridge.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTackleBridge.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldParticleVfxBridge.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldVfxBridges.cpp
    src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldCaptureBridge.cpp
    src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.cpp
    src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleSubmit.cpp
    src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.cpp
    src/game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.cpp
    src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.cpp
    src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.cpp
    src/game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.cpp
    src/game/runtime/shared/projected/unit/SharedProjectedUnitOverlays.cpp
    src/game/runtime/shared/ui/SharedBackendDebugViewOverlay.cpp
    src/game/runtime/shared/ui/SharedBackendDebugViewSupport.cpp
    src/game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.cpp
    src/game/runtime/shared/scene/SharedWorldScene.cpp
    src/game/runtime/shared/scene/LgpeWorldSceneAdapter.cpp
    src/game/runtime/shared/scene/LgpeRoute1ProjectedShadow.cpp
    src/game/runtime/shared/scene/LgpeRoute1TreeInstances.cpp
    src/game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.cpp
    src/game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleBillboardBatches.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleVfxStyles.cpp
    src/game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.cpp
    src/game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.cpp
    src/game/runtime/shared/ui/SharedUnitHudBatches.cpp
    src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp
    src/game/runtime/session/GameUpdateGraph.cpp
    src/game/runtime/video/VideoPreferences.cpp
    src/game/runtime/video/GpuAdapters.cpp
    src/game/runtime/video/D3D12Probe.cpp
    src/game/runtime/render_model_cache/RenderModelCache.cpp
    src/game/runtime/render_model_cache/RenderModelCacheReadDecode.cpp
    src/game/runtime/render_model_cache/RenderModelCacheReadScene.cpp
    src/game/runtime/render_model_cache/RenderModelCacheLoadOrRebuild.cpp
    src/game/runtime/render_model_cache/RenderModelCacheWrite.cpp
    src/game/runtime/render_model_cache/RenderModelCacheSourceBuild.cpp
    src/game/runtime/phlosion/PhlosionModelObject.cpp
    src/game/runtime/phlosion/PhlosionTextureDependencyStore.cpp

    # Game Config
    src/game/config/PokemonConfigLoader.cpp
    src/game/config/MovesConfigLoader.cpp
    src/game/config/AttackAnimConfigLoader.cpp
    src/game/config/AnimSetLoader.cpp
    src/game/config/FlyerConfigLoader.cpp
    src/game/config/EvolutionConfigLoader.cpp
    src/game/config/LeechSeedConfigDB.cpp

    # Game Animations
    src/game/animation/FlightLocomotion.cpp

    # Game Logging
    src/game/logging/LogBus.cpp

    # Game UI
    src/game/ui/HealthBarQuery.cpp
    src/game/ui/legacy/Card.cpp
    src/game/ui/ItemInventoryUI.cpp
    src/game/ui/ShopLayout.cpp
    src/game/ui/ClassicShopHud.cpp
    src/game/ui/ShopUiFacade.cpp
    src/game/ui/AdventureHud.cpp

    # Game VFX
    src/game/vfx/GrassImpactVFX.cpp
    src/game/vfx/TackleImpactVFX.cpp
    src/game/vfx/LeechSeedProjectileVFX.cpp
    src/game/vfx/HealPlusVFX.cpp
    src/game/vfx/LeechSeedDrainVFX.cpp
    src/game/vfx/ClawSwipeVFX.cpp
    src/game/vfx/AquaSwooshVFX.cpp

    # Game Assets
    src/game/assets/DevAssetStore.cpp
    src/game/assets/PackedAssetStore.cpp

    # Game Scripting
    src/game/scripting/LuaBindings.cpp
    src/game/scripting/LuaBindings_Core.cpp
    src/game/scripting/LuaBindings_World.cpp
    src/game/scripting/LuaBindings_UnitMove.cpp
    src/game/scripting/LuaBindings_Util.cpp
    src/game/scripting/LuaCardParser.cpp
    src/game/scripting/LuaScriptHelpers.cpp
    src/game/scripting/LuaTextMenuParser.cpp
    src/game/scripting/LuaScript.cpp
    src/game/scripting/ScriptAPI.cpp
    src/game/scripting/ScriptAPICommands.cpp
    src/game/scripting/ScriptAPICombat.cpp
    src/game/scripting/ScriptAPICombatShared.cpp
    src/game/scripting/ScriptAPICombatAnimation.cpp
    src/game/scripting/ScriptAPICombatDamage.cpp
    src/game/scripting/ScriptAPIWorldState.cpp
    src/game/scripting/ScriptEventBus.cpp

    # Game States
    src/game/state/scripted/ScriptedState.cpp
    src/game/state/scripted/ScriptedStateBackendUi.cpp
    src/game/state/scripted/ScriptedStateCardUi.cpp
    src/game/state/scripted/ScriptedStateInputUi.cpp
    src/game/state/scripted/ScriptedStateRenderUi.cpp
    src/game/state/PlacementState.cpp
    src/game/state/CombatState.cpp

    # Game Systems
    src/game/systems/CameraSystem.cpp
    src/game/systems/UnitInteractionSystem.cpp
    src/game/systems/RoundSystem.cpp
    src/game/systems/CardSystem.cpp
    src/game/systems/BenchSystem.cpp
    src/game/systems/MovementSystem.cpp
    src/game/systems/CombatSystem.cpp
    src/game/systems/ShopSystem.cpp
)
target_link_libraries(PAC_GameObjects
    PUBLIC
        Engine
        glm::glm
        nlohmann_json::nlohmann_json
        PhlosionVFX::Runtime
    PRIVATE
        KTX::ktx
)
if (WIN32)
    target_link_libraries(PAC_GameObjects PRIVATE dxgi d3d12)
endif()
pac_apply_common_target_settings(PAC_GameObjects)

# ---------------- Preview adapters (game-specific preview bindings) ----------------
add_library(PAC_VfxPreviewSupport STATIC
    src/game/preview/PokemonAutochessVfxPreviewProject.cpp
    src/game/preview/PreviewAnimatedModelPresentation.cpp
    src/game/preview/PreviewPokemonVisual.cpp
    src/game/preview/PreviewSceneUtils.cpp
    src/game/preview/effects/GrowlPreviewEffect.cpp
    src/game/preview/effects/GameplayParticlePreviewEffect.cpp
    src/game/preview/effects/ScratchPreviewEffect.cpp
    src/game/preview/effects/TacklePreviewEffect.cpp
    src/game/preview/effects/LeechSeedPreviewEffect.cpp
)
target_sources(PAC_VfxPreviewSupport PRIVATE
    src/game/preview/PokemonAutochessVfxPreviewProject.h
    src/game/preview/PreviewAnimatedModelPresentation.h
    src/game/preview/PreviewPokemonVisual.h
    src/game/preview/PreviewSceneUtils.h
    src/game/preview/effects/GrowlPreviewEffect.h
    src/game/preview/effects/GameplayParticlePreviewEffect.h
    src/game/preview/effects/ScratchPreviewEffect.h
    src/game/preview/effects/TacklePreviewEffect.h
    src/game/preview/effects/LeechSeedPreviewEffect.h
)
target_link_libraries(PAC_VfxPreviewSupport
    PUBLIC
        PAC_GameObjects
        PhlosionVFX::Preview
)
pac_apply_common_target_settings(PAC_VfxPreviewSupport)

# ---------------- Generic VFX lab adapters (no game preview dependency) ----------------
add_library(PAC_VfxLabSupport INTERFACE)
target_link_libraries(PAC_VfxLabSupport
    INTERFACE
        PhlosionVFX::Preview
)
pac_apply_common_target_settings(PAC_VfxLabSupport)

# ---------------- Project editor components ----------------
add_library(PAC_EditorProjectSupport STATIC
    src/game/editor/PokemonAutochessEditorAssetCatalog.cpp
    src/game/editor/PokemonAutochessEditorAssetCatalog.h
    src/game/editor/PokemonAutochessEditorCommands.cpp
    src/game/editor/PokemonAutochessEditorCommands.h
    src/game/editor/PokemonAutochessEditorHierarchy.cpp
    src/game/editor/PokemonAutochessEditorHierarchy.h
    src/game/editor/PokemonAutochessEditorLayoutTransactions.cpp
    src/game/editor/PokemonAutochessEditorLayoutTransactions.h
    src/game/editor/PokemonAutochessEditorPersistence.cpp
    src/game/editor/PokemonAutochessEditorPersistence.h
    src/game/editor/PokemonAutochessEditorPreviewCatalog.cpp
    src/game/editor/PokemonAutochessEditorPreviewCatalog.h
    src/game/editor/PokemonAutochessEditorSceneMutationSession.cpp
    src/game/editor/PokemonAutochessEditorSceneMutationSession.h
    src/game/editor/PokemonAutochessEditorSceneMutations.cpp
    src/game/editor/PokemonAutochessEditorSceneMutations.h
    src/game/editor/PokemonAutochessEditorViewportProjection.cpp
    src/game/editor/PokemonAutochessEditorViewportProjection.h
)
target_link_libraries(PAC_EditorProjectSupport
    PUBLIC
        PAC_GameObjects
        Phlosion::EditorCore
)
pac_apply_common_target_settings(PAC_EditorProjectSupport)

# ---------------- Executable ----------------
add_executable(PokemonAutochess
    src/main.cpp
)
target_link_libraries(PokemonAutochess PRIVATE PAC_GameObjects)
pac_apply_common_target_settings(PokemonAutochess)

# ---------------- Tools ----------------
if (PAC_BUILD_TOOLS)
    add_executable(PhlosionForge
        tools/PhlosionForge.cpp
        tools/PhlosionAssetCatalog.cpp
        tools/PhlosionAssetCatalog.h
        tools/PhlosionCookManifest.cpp
        tools/PhlosionCookManifest.h
        tools/PhlosionForgeManifest.cpp
        tools/PhlosionForgeManifest.h
        tools/PhlosionNativeModelIr.cpp
        tools/PhlosionNativeModelIr.h
    )
    target_link_libraries(PhlosionForge
        PRIVATE
            PAC_GameObjects
    )
    pac_apply_common_target_settings(PhlosionForge)

    add_executable(PAC_DataCooker
        tools/PAC_DataCooker.cpp
    )
    target_link_libraries(PAC_DataCooker
        PRIVATE
            Phlosion::Core
            nlohmann_json::nlohmann_json
    )
    pac_apply_common_target_settings(PAC_DataCooker)

    add_executable(PAC_LgpeInspect
        tools/PAC_LgpeInspect.cpp
    )
    target_link_libraries(PAC_LgpeInspect
        PRIVATE
            PAC_GameObjects
    )
    pac_apply_common_target_settings(PAC_LgpeInspect)

    add_executable(PAC_LgpeQualification
        tools/PAC_LgpeQualification.cpp
    )
    target_link_libraries(PAC_LgpeQualification
        PRIVATE
            PAC_GameObjects
    )
    set_target_properties(PAC_LgpeQualification PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    pac_apply_common_target_settings(PAC_LgpeQualification)

    add_executable(PAC_VfxPreviewer
        tools/PAC_VfxPreviewer.cpp
    )
    target_link_libraries(PAC_VfxPreviewer
        PRIVATE
            PAC_VfxPreviewSupport
    )
    set_target_properties(PAC_VfxPreviewer PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    pac_apply_common_target_settings(PAC_VfxPreviewer)

    add_executable(VfxLab
        tools/PAC_VfxLab.cpp
    )
    target_link_libraries(VfxLab
        PRIVATE
            PAC_VfxLabSupport
    )
    set_target_properties(VfxLab PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    pac_apply_common_target_settings(VfxLab)

endif()

if (PAC_BUILD_EDITOR)
    if (NOT TARGET Phlosion::EditorCore)
        message(FATAL_ERROR
            "PAC_BUILD_EDITOR requires the Phlosion::EditorCore target.")
    endif()
    add_library(PokemonAutochessEditorProject SHARED
        tools/PokemonAutochessEditorProject.cpp
        src/game/editor/PokemonPrefabPreview.cpp
        src/game/editor/PokemonPrefabPreview.h
        src/game/editor/PokemonVfxPrefabPreview.cpp
        src/game/editor/PokemonVfxPrefabPreview.h
        src/game/editor/Route1EnvironmentPrefabPreview.cpp
        src/game/editor/Route1EnvironmentPrefabPreview.h
    )
    target_link_libraries(PokemonAutochessEditorProject
        PRIVATE
            PAC_GameObjects
            PAC_EditorProjectSupport
            PAC_VfxPreviewSupport
            Phlosion::EditorCore
    )
    set_target_properties(PokemonAutochessEditorProject PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_SOURCE_DIR}/.phlosion/editor/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY
            "${CMAKE_SOURCE_DIR}/.phlosion/editor/$<CONFIG>"
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/editor-plugin/$<CONFIG>"
    )
    target_compile_definitions(PokemonAutochessEditorProject
        PRIVATE
            PHLOSION_EDITOR_PLUGIN_BUILD_CONFIGURATION="$<CONFIG>"
    )
    pac_apply_common_target_settings(
        PokemonAutochessEditorProject)
endif()

# ---------------- Tests ----------------
if (BUILD_TESTING)
    add_test(
        NAME PAC_ProjectOwnershipBoundary
        COMMAND ${CMAKE_COMMAND}
            -DPAC_ROOT=${CMAKE_SOURCE_DIR}
            -DPHLOSION_ROOT=${_pac_resolved_engine_source_dir}
            -DPHLOSION_PACKAGES_ROOT=${_pac_resolved_packages_source_dir}
            -P ${CMAKE_SOURCE_DIR}/tests/CheckProjectOwnership.cmake)
    if (EXISTS
        "${_pac_resolved_engine_source_dir}/tests/CheckEngineSemanticBoundary.cmake")
        add_test(
            NAME PAC_EngineSemanticBoundary
            COMMAND ${CMAKE_COMMAND}
                -DPHLOSION_ROOT=${_pac_resolved_engine_source_dir}
                -P ${_pac_resolved_engine_source_dir}/tests/CheckEngineSemanticBoundary.cmake)
    endif()
    add_executable(PAC_Tests
        tests/TestMain.cpp
        tests/TestEditorAssetCatalog.cpp
        tests/TestEditorCommands.cpp
        tests/TestEditorHierarchy.cpp
        tests/TestEditorLayoutTransactions.cpp
        tests/TestEditorPersistence.cpp
        tests/TestEditorPreviewCatalog.cpp
        tests/TestEditorSceneMutations.cpp
        tests/TestEditorViewportProjection.cpp
        tests/TestLuaBindings.cpp
        tests/TestLuaScriptCache.cpp
        tests/TestEventBus.cpp
        tests/TestGameConfig.cpp
        tests/TestECS.cpp
        tests/TestDeterminism.cpp
        tests/TestEnvironment.cpp
        tests/TestAutoQuitPolicy.cpp
        tests/TestGameServicesRoutes.cpp
        tests/TestGameServiceRenderRoutes.cpp
        tests/TestStartupRenderRoutePolicy.cpp
        tests/TestRenderRouteOwnership.cpp
        tests/TestStateUiRoutePolicy.cpp
        tests/TestRenderRoutes.cpp
        tests/TestRenderPolicyApi.cpp
        tests/TestRenderBackendInterfaceSplit.cpp
        tests/TestUiHudFormatting.cpp
        tests/TestUiDebugText.cpp
        tests/TestUiImagePath.cpp
        tests/TestRenderPrepMaterialShading.cpp
        tests/TestRenderPrepMeshNormals.cpp
        tests/TestRenderPrepProceduralPose.cpp
        tests/TestRenderModelCache.cpp
        tests/TestPhlosionNativeModelIr.cpp
        tests/TestNativeCharmanderFamilyFire.cpp
        tests/TestPhlosionAssetCatalog.cpp
        tools/PhlosionAssetCatalog.cpp
        tools/PhlosionCookManifest.cpp
        tools/PhlosionForgeManifest.cpp
        tools/PhlosionNativeModelIr.cpp
        tests/TestPhlosionResourceContainer.cpp
        tests/TestPhlosionSceneArchive.cpp
        tests/TestLgpeCanonicalScene.cpp
        tests/TestLgpeWorldSceneAdapter.cpp
        tests/TestLgpeRoute1RuntimeEnvironment.cpp
        tests/TestRoute1CookedEnvironment.cpp
        tests/TestRenderPrepWorldProjection.cpp
        tests/TestRenderPrepWorldProxyGeometry.cpp
        tests/TestRenderPrepUnitVisuals.cpp
        tests/TestSharedProjectedWorldSceneHelpers.cpp
        tests/TestUiInventoryOverlay.cpp
        tests/TestUiInventoryPanel.cpp
        tests/TestBackendCardLayoutModel.cpp
        tests/TestUiCardVisuals.cpp
        tests/TestUiCardRenderer.cpp
        tests/TestUiShopHudModel.cpp
        tests/TestUiSellOverlayModel.cpp
        tests/TestUiStatusText.cpp
        tests/TestUiScale.cpp
        tests/TestUiTopBanner.cpp
        tests/TestBackendUiPolicy.cpp
        tests/TestBackendShopSnapshot.cpp
        tests/TestShopCardConversion.cpp
        tests/TestUiInputSlots.cpp
        tests/TestSellOverlayUiPolicy.cpp
        tests/TestLogSink.cpp
        tests/TestLogBus.cpp
        tests/TestRenderFlowDecisions.cpp
        tests/TestBackendRenderPolicy.cpp
        tests/TestWorldBlendPolicy.cpp
        tests/TestVulkanGeometryArenaLayout.cpp
        tests/TestVulkanWorldIndirectState.cpp
        tests/TestRendererParityContract.cpp
        tests/TestDebugGeometry.cpp
        tests/TestVideoInitGuards.cpp
        tests/TestVideoPreferences.cpp
        tests/TestRuntimeRenderModelPrewarm.cpp
        tests/TestRuntimeUiCardPrewarm.cpp
        tests/TestRuntimeStartupAssetPrewarm.cpp
        tests/TestRuntimeBootLoading.cpp
        tests/TestRuntimeFixedStepPhase.cpp
        tests/TestRuntimeFrameObservation.cpp
        tests/TestRuntimeFramePerfCapture.cpp
        tests/TestRuntimeGameRunnerFrameExecution.cpp
        tests/TestRuntimeGameRunnerFrameDiagnostics.cpp
        tests/TestRuntimeGameRunnerLoopPolicy.cpp
        tests/TestRendererBackendBootstrap.cpp
        tests/TestRendererStartupDiagnostics.cpp
        tests/TestRuntimeLoopConfig.cpp
        tests/TestRuntimeLoopControl.cpp
        tests/TestRuntimeOpenGlBootstrap.cpp
        tests/TestRuntimePerfAccumulator.cpp
        tests/TestRuntimePerfLogging.cpp
        tests/TestRuntimeRelaunchLoop.cpp
        tests/TestRuntimeRendererActivation.cpp
        tests/TestRuntimeRendererRecovery.cpp
        tests/TestRuntimeRendererStartupState.cpp
        tests/TestRuntimeRestartPolicy.cpp
        tests/TestRuntimeSdlEventDispatch.cpp
        tests/TestRuntimeSdlInput.cpp
        tests/TestRuntimeSdlVideoMode.cpp
        tests/TestRuntimeWindowPresentationController.cpp
        tests/TestRuntimeStartupConfig.cpp
        tests/TestRuntimeStartupPresentation.cpp
        tests/TestCameraPanClamp.cpp
        tests/TestRuntimeStartupSession.cpp
        tests/TestRuntimeStartupVideoOverride.cpp
        tests/TestRuntimeWorldLayerPrewarm.cpp
        tests/TestRuntimeWindowBootstrap.cpp
        tests/TestDxgiAdapterSelectionPolicy.cpp
        tests/TestD3D12Probe.cpp
        tests/TestContentValidation.cpp
        tests/TestBattleInvariants.cpp
        tests/TestEndToEnd.cpp
        tests/TestMovementSystem.cpp
        tests/TestModelParse.cpp
        tests/TestModelLoaderStructure.cpp
        tests/TestProjectedMeshRendererHotPath.cpp
        tests/TestProjectedWorldSceneSeams.cpp
        tests/TestGltfAsset.cpp
        tests/TestCombatAnimIndexCache.cpp
        tests/TestScriptApiContract.cpp
        tests/TestSessionBackendRenderHelpers.cpp
        tests/TestSessionBackendInventoryUi.cpp
        tests/TestSessionBackendAssetBridge.cpp
        tests/TestSessionBackendUnitHydration.cpp
        tests/TestSessionLifecycleBridge.cpp
        tests/TestSessionRenderBridge.cpp
        tests/TestSessionDebugSnapshot.cpp
        tests/TestRuntimeGrowlVfxPrewarm.cpp
        tests/TestRuntimeScratchVfxPrewarm.cpp
        tests/TestRuntimeTackleVfxPrewarm.cpp
        tests/TestSharedAuthoredVfxPreviewMeshLoader.cpp
        tests/TestPreviewEffectPokemonSpecies.cpp
        tests/TestGameplayParticleVfx.cpp
        tests/TestPreviewPokemonVisual.cpp
        tests/TestSessionLegacyWorldView.cpp
        tests/TestSessionFrameMetrics.cpp
        tests/TestSessionLoopBridge.cpp
        tests/TestSessionLoopRuntime.cpp
        tests/TestRuntimeParticleVfxPrewarm.cpp
        tests/TestSessionProjectedWorldView.cpp
        tests/TestSessionRenderLayout.cpp
        tests/TestSharedProjectedRenderItems.cpp
        tests/TestSessionRenderScratch.cpp
        tests/TestSessionRenderConfig.cpp
        tests/TestSessionSnapshotRuntime.cpp
        tests/TestSessionWorldRenderRuntime.cpp
        tests/TestSessionTextureCache.cpp
        tests/TestSessionWorldBackdrop.cpp
        tests/TestRoundFlow.cpp
        tests/TestAnimsetRoles.cpp
        tests/TestCombatSlice.cpp
        tests/TestMoveImpactRouting.cpp
        tests/TestMoveImpactMath.cpp
        tests/TestRenderSmoke.cpp
        tests/TestAnimsetGlbNames.cpp
        tests/TestPlacementToCombat.cpp
        tests/TestAnimsetClipNames.cpp
        tests/TestLayering.cpp
        tests/TestStateManager.cpp
        tests/TestModeSplit.cpp
        tests/TestShopLayout.cpp
        tests/TestLuaCardParser.cpp
        tests/TestLuaTextMenuParser.cpp
        tests/TestLuaScriptHelpers.cpp
        tests/TestShopSystem.cpp
        tests/TestGameWorldProgression.cpp
        tests/TestGameWorldRoster.cpp
        tests/TestGameWorldEconomy.cpp
        tests/TestGameWorldCapture.cpp
        tests/TestGameWorldQueries.cpp
        tests/TestGameWorldRecovery.cpp
        tests/TestGameWorldLeechSeed.cpp
        tests/TestSharedCapturePresentation.cpp
        tests/TestSharedCaptureOverlayVfx.cpp
        tests/TestSharedBackendDebugViewSupport.cpp
        tests/TestSharedParticleBillboardBatches.cpp
        tests/TestSharedParticleSnapshotBillboards.cpp
        tests/TestSharedParticleVfxBridgeDispatch.cpp
        tests/TestSharedParticleVfxStyles.cpp
        tests/TestSharedProjectedUnitBackendMeshSupport.cpp
        tests/TestSharedProjectedUnitRenderer.cpp
        tests/TestSharedProjectedUnitWorldSceneRenderer.cpp
        tests/TestSharedProjectedBodyPresentation.cpp
        tests/TestSharedPreviewBodyPresentationPath.cpp
        tests/TestSharedAuthoredVfxHelpers.cpp
        tests/TestSharedAuthoredVfxBridge.cpp
        tests/TestSharedAuthoredVfxInterop.cpp
        tests/TestSharedAuthoredVfxBatches.cpp
        tests/TestTackleSmokeVfx.cpp
        tests/TestSharedWorldScene.cpp
        tests/TestSharedWorldIndexedBatches.cpp
        tests/TestD3D12WorldMaterialConstants.cpp
        tests/TestVulkanWorldMaterialState.cpp
        tests/TestVulkanSpriteInstanceState.cpp
        tests/TestConfigLoaders.cpp
        tests/TestSourceAscii.cpp
    )

    target_link_libraries(PAC_Tests
        PRIVATE
            PAC_GameObjects
            PAC_EditorProjectSupport
            PAC_VfxPreviewSupport
            PAC_VfxLabSupport
            engine_core
            glad::glad
            OpenGL::GL
    )

    # Tests include both /src and /tests
    target_include_directories(PAC_Tests
        PRIVATE
            "${PROJECT_SRC_ROOT}"
            "${CMAKE_SOURCE_DIR}/tests"
    )
    target_compile_definitions(PAC_Tests
        PRIVATE
            PAC_PHLOSION_ENGINE_SOURCE_DIR="${_pac_resolved_engine_source_dir}"
            PAC_PHLOSION_VFX_SOURCE_DIR="${_pac_resolved_vfx_source_dir}"
    )

    if (MSVC)
        target_compile_options(PAC_Tests PRIVATE /std:c++20 /permissive- /Zc:__cplusplus /FS /MP)
    else()
        target_compile_options(PAC_Tests PRIVATE -std=c++20)
    endif()
    pac_apply_warning_flags(PAC_Tests)

    # Register individual tests for CTest output granularity.
    # Source of truth is tests/TestMain.cpp (single list used by runtime filtering).
    set(PAC_TEST_MAIN_FILE "${CMAKE_SOURCE_DIR}/tests/TestMain.cpp")
    file(READ "${PAC_TEST_MAIN_FILE}" PAC_TEST_MAIN_CONTENTS)
    string(REGEX MATCHALL "\\{\"[A-Za-z0-9_]+\",[ \t]*&test_[A-Za-z0-9_]+\\}" PAC_TEST_CASE_ROWS "${PAC_TEST_MAIN_CONTENTS}")

    if (PAC_TEST_CASE_ROWS STREQUAL "")
        message(FATAL_ERROR "Failed to parse test cases from ${PAC_TEST_MAIN_FILE}.")
    endif()

    set(PAC_TEST_NAMES)
    foreach(test_case_row IN LISTS PAC_TEST_CASE_ROWS)
        string(REGEX REPLACE ".*\\{\"([A-Za-z0-9_]+)\",[ \t]*&test_[A-Za-z0-9_]+\\}.*" "\\1" test_name "${test_case_row}")
        list(APPEND PAC_TEST_NAMES "${test_name}")
    endforeach()

    list(LENGTH PAC_TEST_NAMES PAC_TEST_COUNT_RAW)
    list(REMOVE_DUPLICATES PAC_TEST_NAMES)
    list(LENGTH PAC_TEST_NAMES PAC_TEST_COUNT_UNIQUE)
    if (NOT PAC_TEST_COUNT_RAW EQUAL PAC_TEST_COUNT_UNIQUE)
        message(FATAL_ERROR "Duplicate PAC test names found in ${PAC_TEST_MAIN_FILE}.")
    endif()

    set(PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE TRUE)
    foreach(required_private_asset IN ITEMS
        "${CMAKE_SOURCE_DIR}/assets/models/0001_Bulbasaur_SV.phmodel"
        "${CMAKE_SOURCE_DIR}/assets/meshes/growl_1076_mesh.glb"
        "${CMAKE_SOURCE_DIR}/assets/shaders"
        "${CMAKE_SOURCE_DIR}/content/phlosion/scenes/route1.phscene")
        if (NOT EXISTS "${required_private_asset}")
            set(PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE FALSE)
        endif()
    endforeach()

    set(PAC_PRIVATE_ASSET_TEST_NAMES
        ui_image_path_contract
        native_charmander_family_fire_contract
        route1_cooked_environment_contract
        render_prep_unit_visuals_contract
        ui_card_visuals_contract
        ui_card_renderer_contract
        content_invariants
        model_parse_smoke
        model_asset_smoke
        session_legacy_world_view_contract
        animset_roles_smoke
        animset_roles_prefer_best_idle_match
        native_starter_motion_sets_preserved
        render_pipeline_smoke
        animset_model_name_smoke
        animset_clip_name_smoke
        shared_projected_unit_renderer_gastly_tongue_timeline_contract
        shared_projected_unit_renderer_idle_clip_loop_closure_contract
        shared_projected_unit_renderer_idle_fixed_step_wrap_contract
        shared_authored_vfx_preview_mesh_loader_contract
    )

    if (PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE)
        message(STATUS "Private asset qualification tests are enabled.")
    else()
        message(STATUS
            "Private asset qualification tests are omitted because the ignored source/cooked corpus is unavailable.")
    endif()

    set(PAC_REGISTERED_TEST_NAMES)
    foreach(test_name IN LISTS PAC_TEST_NAMES)
        if (NOT PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE AND
            test_name IN_LIST PAC_PRIVATE_ASSET_TEST_NAMES)
            continue()
        endif()
        add_test(NAME PAC_Tests.${test_name} COMMAND PAC_Tests --filter ${test_name})
        list(APPEND PAC_REGISTERED_TEST_NAMES "${test_name}")
    endforeach()

    # Ensure tests run with repo root as working directory so relative config/script paths resolve.
    set(PAC_ALL_TESTS)
    foreach(test_name IN LISTS PAC_REGISTERED_TEST_NAMES)
        list(APPEND PAC_ALL_TESTS PAC_Tests.${test_name})
    endforeach()

    set_tests_properties(${PAC_ALL_TESTS} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        ENVIRONMENT "PHLOSION_DATA_ROOT=${CMAKE_SOURCE_DIR};PHLOSION_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets"
        TIMEOUT 120
    )
    if (PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE)
        set(PAC_PRIVATE_ASSET_CTESTS)
        foreach(test_name IN LISTS PAC_PRIVATE_ASSET_TEST_NAMES)
            list(APPEND PAC_PRIVATE_ASSET_CTESTS PAC_Tests.${test_name})
        endforeach()
        set_tests_properties(${PAC_PRIVATE_ASSET_CTESTS} PROPERTIES
            LABELS private-assets
        )
        set_tests_properties(PAC_Tests.model_parse_smoke PROPERTIES
            TIMEOUT 600
        )
    endif()

    if (WIN32)
        add_test(
            NAME PAC_Tools.ci_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/test_ci_workflow.ps1)
        set_tests_properties(PAC_Tools.ci_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.phlosion_dependency_pins
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/test_phlosion_dependency_pins.ps1)
        set_tests_properties(PAC_Tools.phlosion_dependency_pins PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.script_syntax
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/check_script_syntax.ps1)
        set_tests_properties(PAC_Tools.script_syntax PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )

        add_test(
            NAME PAC_Tools.render_parity_content_guard
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/test_render_parity_content_guard.ps1)
        set_tests_properties(PAC_Tools.render_parity_content_guard PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.renderer_qualification_support
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/test_renderer_qualification_report.ps1)
        set_tests_properties(PAC_Tools.renderer_qualification_support PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.housekeeping_cleanup_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/housekeeping/test_cleanup_workspace.ps1)
        set_tests_properties(PAC_Tools.housekeeping_cleanup_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.housekeeping_inventory_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/housekeeping/test_inventory_workspace.ps1)
        set_tests_properties(PAC_Tools.housekeeping_inventory_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.game_editor_retirement_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/housekeeping/test_retire_game_editor_artifacts.ps1)
        set_tests_properties(PAC_Tools.game_editor_retirement_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.native_model_payload_store_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/test_native_model_payload_store.ps1)
        set_tests_properties(PAC_Tools.native_model_payload_store_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.runtime_content_depot_publisher_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/test_publish_runtime_content_to_depot.ps1)
        set_tests_properties(PAC_Tools.runtime_content_depot_publisher_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.unreferenced_cooked_object_pruner_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/test_prune_unreferenced_cooked_objects.ps1)
        set_tests_properties(PAC_Tools.unreferenced_cooked_object_pruner_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.unreferenced_native_import_pruner_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/test_prune_unreferenced_native_imports.ps1)
        set_tests_properties(PAC_Tools.unreferenced_native_import_pruner_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
            RUN_SERIAL TRUE
        )

        add_test(
            NAME PAC_Tools.unreferenced_review_model_pruner_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/test_prune_unreferenced_review_models.ps1)
        set_tests_properties(PAC_Tools.unreferenced_review_model_pruner_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
            RUN_SERIAL TRUE
        )

        if (PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE)
            add_test(
                NAME PAC_Tools.kanto_model_promotions
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/assets/validate_kanto_model_promotions.ps1)
            set_tests_properties(PAC_Tools.kanto_model_promotions PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 30
                LABELS private-assets
            )
        endif()

        add_test(
            NAME PAC_Tools.kanto_gender_model_policy
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/validate_kanto_gender_models.ps1)
        set_tests_properties(PAC_Tools.kanto_gender_model_policy PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        if (PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE)
            add_test(
                NAME PAC_Tools.kanto_model_material_inventory
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/assets/audit_kanto_model_materials.ps1
                    -OutputDirectory ${CMAKE_BINARY_DIR}/kanto-model-material-audit
                    -RequireAllSelectedModels)
            set_tests_properties(PAC_Tools.kanto_model_material_inventory PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 600
                RUN_SERIAL TRUE
                LABELS private-assets
            )
        endif()

        add_test(
            NAME PAC_Tools.kanto_model_material_inventory_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/assets/test_kanto_model_material_audit.ps1)
        set_tests_properties(PAC_Tools.kanto_model_material_inventory_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        if (PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE)
            add_test(
                NAME PAC_Tools.sv_eevee_character_capture_plan
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/research/validate_character_capture.ps1
                    -SpecPath tools/research/captures/sv-eevee-modern-surface-v1.json)
            set_tests_properties(PAC_Tools.sv_eevee_character_capture_plan PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 30
                LABELS private-assets
            )

            add_test(
                NAME PAC_Tools.character_capture_workflow_contract
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/research/test_character_capture_workflow.ps1)
            set_tests_properties(PAC_Tools.character_capture_workflow_contract PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 30
                LABELS private-assets
            )
        endif()

        add_test(
            NAME PAC_Tools.sv_eevee_static_material_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_sv_eevee_static_material_workflow.ps1)
        set_tests_properties(PAC_Tools.sv_eevee_static_material_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.sv_fresnel_effect_static_material_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_sv_fresnel_effect_static_material_workflow.ps1)
        set_tests_properties(PAC_Tools.sv_fresnel_effect_static_material_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        if (PAC_PRIVATE_ASSET_QUALIFICATION_AVAILABLE)
            add_test(
                NAME PAC_Tools.sv_kanto_shader_permutation_workflow_contract
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/research/test_sv_kanto_shader_permutation_workflow.ps1)
            set_tests_properties(PAC_Tools.sv_kanto_shader_permutation_workflow_contract PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 30
                LABELS private-assets
            )

            add_test(
                NAME PAC_Tools.sv_kanto_runtime_bridge_contract
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/research/test_sv_kanto_runtime_bridge.ps1)
            set_tests_properties(PAC_Tools.sv_kanto_runtime_bridge_contract PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 30
                LABELS private-assets
            )
        endif()

        add_test(
            NAME PAC_Tools.za_kanto_shader_research_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_kanto_shader_research_workflow.ps1)
        set_tests_properties(PAC_Tools.za_kanto_shader_research_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_local_reflection_probe_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_local_reflection_probe_workflow.ps1)
        set_tests_properties(PAC_Tools.za_local_reflection_probe_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_ik_character_static_material_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_ik_character_static_material_workflow.ps1)
        set_tests_properties(PAC_Tools.za_ik_character_static_material_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_ik_character_dataflow_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_ik_character_dataflow_workflow.ps1)
        set_tests_properties(PAC_Tools.za_ik_character_dataflow_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_ik_eye_runtime_coverage_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_ik_eye_runtime_coverage_workflow.ps1)
        set_tests_properties(PAC_Tools.za_ik_eye_runtime_coverage_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_scene_color_boundary_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_scene_color_boundary_workflow.ps1)
        set_tests_properties(PAC_Tools.za_scene_color_boundary_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_ui_offscreen_lighting_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_ui_offscreen_lighting_contract.ps1
                -GameRoot ${CMAKE_SOURCE_DIR}
                -EngineRoot ${_pac_resolved_engine_source_dir})
        set_tests_properties(PAC_Tools.za_ui_offscreen_lighting_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_eye_static_material_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_eye_static_material_workflow.ps1)
        set_tests_properties(PAC_Tools.za_eye_static_material_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )

        add_test(
            NAME PAC_Tools.za_fresnel_effect_static_material_workflow_contract
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tools/research/test_za_fresnel_effect_static_material_workflow.ps1)
        set_tests_properties(PAC_Tools.za_fresnel_effect_static_material_workflow_contract PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
    endif()

    if (PAC_ENABLE_RUNTIME_SMOKE_TESTS)
        add_test(NAME PAC_RuntimeSmoke.opengl COMMAND PokemonAutochess)
        set_tests_properties(PAC_RuntimeSmoke.opengl PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 45
            ENVIRONMENT
                "PHLOSION_DATA_ROOT=${CMAKE_SOURCE_DIR};PHLOSION_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_RENDER_BACKEND=opengl;PAC_AUTO_QUIT_SECONDS=2;PAC_DISABLE_MODELCACHE=1"
        )

        add_test(NAME PAC_RuntimeSmoke.vulkan COMMAND PokemonAutochess)
        set_tests_properties(PAC_RuntimeSmoke.vulkan PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 45
            ENVIRONMENT
                "PHLOSION_DATA_ROOT=${CMAKE_SOURCE_DIR};PHLOSION_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_RENDER_BACKEND=vulkan;PAC_AUTO_QUIT_SECONDS=2;PAC_DISABLE_MODELCACHE=1"
        )

        if (WIN32)
            add_test(NAME PAC_RuntimeSmoke.d3d12 COMMAND PokemonAutochess)
            set_tests_properties(PAC_RuntimeSmoke.d3d12 PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 45
                ENVIRONMENT
                    "PHLOSION_DATA_ROOT=${CMAKE_SOURCE_DIR};PHLOSION_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_RENDER_BACKEND=d3d12;PAC_AUTO_QUIT_SECONDS=2;PAC_DISABLE_MODELCACHE=1"
            )

            add_test(
                NAME PAC_RuntimeSmoke.parity_contract
                COMMAND powershell -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/tools/check_renderer_parity_contract.ps1
                    -BuildDir ${CMAKE_BINARY_DIR}
                    -Config $<CONFIG>
                    -AutoQuitSeconds 2)
            set_tests_properties(PAC_RuntimeSmoke.parity_contract PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 120
                ENVIRONMENT
                    "PHLOSION_DATA_ROOT=${CMAKE_SOURCE_DIR};PHLOSION_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_DISABLE_MODELCACHE=1"
            )
        endif()
    endif()
endif()

# ---------------- Convenience aggregate target ----------------
# Build everything with one target (engine libs + game + optional tests/tools).
set(PAC_ALL_TARGETS
    engine_core
    engine_platform
    engine_render
    engine_runtime
    PAC_GameObjects
    PokemonAutochess
)
if (BUILD_TESTING)
    list(APPEND PAC_ALL_TARGETS PAC_Tests)
endif()
if (PAC_BUILD_TOOLS)
    list(APPEND PAC_ALL_TARGETS
        PAC_DataCooker
        PAC_LgpeInspect
        PAC_VfxPreviewer
        VfxLab)
endif()
if (PAC_BUILD_EDITOR)
    list(APPEND PAC_ALL_TARGETS
        PokemonAutochessEditorProject)
endif()
add_custom_target(PAC_All
    DEPENDS
        ${PAC_ALL_TARGETS}
)

# ---------------- Data pack helper targets ----------------
if (PAC_BUILD_TOOLS)
    add_custom_target(PAC_ValidateData
        COMMAND $<TARGET_FILE:PAC_DataCooker> --validate-only
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS PAC_DataCooker
    )

    add_custom_target(PAC_PackData
        COMMAND $<TARGET_FILE:PAC_DataCooker> --out content_pak/content.pak
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS PAC_DataCooker
    )
endif()





