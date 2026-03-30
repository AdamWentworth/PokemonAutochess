# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(PokemonAutochess LANGUAGES CXX)

option(PAC_VERBOSE_STARTUP "Enable verbose startup/model-load logging" OFF)
option(PAC_BUILD_TOOLS "Build developer tools (data cooker, VFX previewer)" ON)

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
find_package(SDL2_ttf REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

find_package(sol2 CONFIG REQUIRED)
if (TARGET sol2::sol2)
    set(PAC_SOL2_TARGET sol2::sol2)
elseif (TARGET sol2)
    set(PAC_SOL2_TARGET sol2)
else()
    message(FATAL_ERROR "sol2 package found, but no known CMake target (sol2::sol2 or sol2) is available.")
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
        "${CMAKE_SOURCE_DIR}/src/engine/render/D3D12RenderBackend.cpp"
        "${CMAKE_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendCachedWorldMeshes.cpp"
        "${CMAKE_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp"
        "${CMAKE_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendTextures.cpp"
        "${CMAKE_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp"
        "${CMAKE_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendWorldScene.cpp"
        "${CMAKE_SOURCE_DIR}/src/engine/render/d3d12/D3D12TextureUpload.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionProjectedWorldView.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionBackendRenderHelpers.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionRenderScratch.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/session/SessionWorldRenderRuntime.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedRenderItems.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitModelRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitOverlays.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/game/runtime/shared/scene/SharedWorldScene.cpp"
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
                PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )
    else()
        target_include_directories(${target_name}
            PRIVATE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            PRIVATE
                PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )

        if (MSVC)
            target_compile_options(${target_name} PRIVATE /std:c++20 /permissive- /Zc:__cplusplus /FS)
        else()
            target_compile_options(${target_name} PRIVATE -std=c++20)
        endif()
        pac_apply_warning_flags(${target_name})
    endif()
endfunction()

# ---------------- engine_core (no SDL/GL) ----------------
add_library(engine_core STATIC
    src/engine/core/SystemRegistry.cpp
    src/engine/core/StdoutLogger.cpp
    src/engine/core/EventBus.cpp
    src/engine/core/ecs/World.cpp
)
target_sources(engine_core PRIVATE
    src/engine/core/SystemRegistry.h
    src/engine/core/Updatable.h
    src/engine/core/ILogger.h
    src/engine/core/StdoutLogger.h
    src/engine/core/IEventBus.h
    src/engine/core/EventBus.h
    src/engine/core/Services.h
    src/engine/core/AssetPackFormat.h
    src/engine/core/IRandom.h
    src/engine/core/Random.h
    src/engine/core/ITimeSource.h
    src/engine/core/TimeSources.h
    src/engine/core/ecs/Entity.h
    src/engine/core/ecs/ComponentStorage.h
    src/engine/core/ecs/ISystem.h
    src/engine/core/ecs/Scheduler.h
    src/engine/core/ecs/World.h
    src/engine/core/ecs/View.h
)
target_link_libraries(engine_core PRIVATE glm::glm)
pac_apply_common_target_settings(engine_core)

# ---------------- engine_platform (SDL, window/input) ----------------
add_library(engine_platform STATIC
    src/engine/platform/Window.cpp
    src/engine/input/SdlKeyMap.cpp
)
target_link_libraries(engine_platform
    PUBLIC SDL2::SDL2 SDL2_ttf::SDL2_ttf
    PRIVATE glm::glm
)
pac_apply_common_target_settings(engine_platform)

# ---------------- engine_render (OpenGL + renderer + resources) ----------------
add_library(engine_render STATIC
    src/engine/render/D3D12RenderBackend.cpp
    src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp
    src/engine/render/d3d12/D3D12RenderBackendDebugDraw.cpp
    src/engine/render/d3d12/D3D12RenderBackendTextures.cpp
    src/engine/render/d3d12/D3D12RenderBackendCachedWorldMeshes.cpp
    src/engine/render/d3d12/D3D12RenderBackendWorldScene.cpp
    src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp
    src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp
    src/engine/render/d3d12/D3D12TextureUpload.cpp
    src/engine/render/OpenGLRenderBackend.cpp
    src/engine/render/opengl/OpenGLRenderBackendDebugDraw.cpp
    src/engine/render/opengl/OpenGLRenderBackendPipelines.cpp
    src/engine/render/opengl/OpenGLRenderBackendShaderUtils.cpp
    src/engine/render/opengl/OpenGLRenderBackendTextures.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp
    src/engine/render/ModelAnimation.cpp
    src/engine/render/ModelCache.cpp
    src/engine/render/RendererParityContract.cpp
    src/engine/render/WorldPbrShaderShared.cpp
    src/engine/render/NeutralPmrem.cpp
    src/engine/render/gltf/ModelFastGltfLoader.cpp
    src/engine/render/gltf/ModelFastGltfLoaderHelpers.cpp
    src/engine/render/gltf/ModelFastGltfMaterial.cpp
    src/engine/render/gltf/ModelFastGltfSceneData.cpp
    src/engine/render/gltf/ModelFastGltfTextures.cpp

    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp
    src/engine/ui/BootLoadingView.cpp

    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderCache.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/Log.cpp
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp

    src/engine/vfx/ParticleSystem.cpp
)
target_link_libraries(engine_render
    PRIVATE
        OpenGL::GL
        glad::glad
        glm::glm
        lua
        nlohmann_json::nlohmann_json
        fastgltf::fastgltf
        ${PAC_SOL2_TARGET}
)
if (WIN32)
    target_link_libraries(engine_render PRIVATE d3d12 dxgi d3dcompiler)
endif()
if (PAC_OPTIMIZE_D3D12_SHADERS_IN_DEBUG)
    target_compile_definitions(engine_render PRIVATE "$<$<CONFIG:Debug>:PAC_OPTIMIZE_D3D12_SHADERS_IN_DEBUG=1>")
endif()
pac_apply_common_target_settings(engine_render)

# ---------------- Engine (compat umbrella target) ----------------
add_library(Engine INTERFACE)
target_link_libraries(Engine INTERFACE engine_core engine_platform engine_render)
pac_apply_common_target_settings(Engine)

# ---------------- engine_runtime (optional SDL/GL loop host) ----------------
add_library(engine_runtime STATIC
    src/engine/runtime/Application.cpp
)
target_link_libraries(engine_runtime PRIVATE engine_core engine_platform engine_render)
pac_apply_common_target_settings(engine_runtime)

# ---------------- engine_tools_vfx_preview (reusable preview framework) ----------------
add_library(engine_tools_vfx_preview STATIC
    src/engine/tools/vfx_preview/VfxPreviewApp.cpp
)
target_sources(engine_tools_vfx_preview PRIVATE
    src/engine/tools/vfx_preview/VfxPreviewApp.h
    src/engine/tools/vfx_preview/VfxPreviewTypes.h
    src/engine/tools/vfx_preview/PreviewDebugDraw.h
    src/engine/tools/vfx_preview/IVfxPreviewEffect.h
    src/engine/tools/vfx_preview/IVfxPreviewProject.h
)
target_link_libraries(engine_tools_vfx_preview
    PUBLIC
        engine_core
        engine_platform
        engine_render
        glm::glm
)
pac_apply_common_target_settings(engine_tools_vfx_preview)

# ---------------- PAC_VfxCore (game-free reusable VFX) ----------------
add_library(PAC_VfxCore STATIC
    src/vfx/effects/growl/GrowlWaveVFX.cpp
    src/vfx/runtime/growl/SharedGrowlVfxHelpers.cpp
)
target_sources(PAC_VfxCore PRIVATE
    src/vfx/effects/growl/GrowlWaveVFX.h
    src/vfx/runtime/growl/SharedGrowlVfxHelpers.h
)
target_link_libraries(PAC_VfxCore
    PUBLIC
        Engine
        glm::glm
        nlohmann_json::nlohmann_json
)
pac_apply_common_target_settings(PAC_VfxCore)

# ---------------- PAC_GameVfxInterop (game-specific backend bridge) ----------------
add_library(PAC_GameVfxInterop STATIC
    src/vfx/runtime/growl/SharedGrowlWaveBridge.cpp
    src/vfx/runtime/growl/SharedGrowlWaveBatches.cpp
)
target_sources(PAC_GameVfxInterop PRIVATE
    src/vfx/runtime/growl/SharedGrowlWaveBridge.h
    src/vfx/runtime/growl/SharedGrowlWaveBatches.h
)
target_link_libraries(PAC_GameVfxInterop
    PUBLIC
        PAC_VfxCore
)
pac_apply_common_target_settings(PAC_GameVfxInterop)

# ---------------- Game objects (shared by exe + tests) ----------------
add_library(PAC_GameObjects STATIC
    # Game Core
    src/game/runtime/GameApp.cpp
    src/game/runtime/GameRunner.cpp
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
    src/game/runtime/startup/RuntimeParticleVfxPrewarm.cpp
    src/game/runtime/startup/RuntimeUiCardPrewarm.cpp
    src/game/runtime/startup/RuntimeStartupAssetPrewarm.cpp
    src/game/runtime/RuntimeBootLoading.cpp
    src/game/runtime/renderer/RendererBackendBootstrap.cpp
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
    src/game/runtime/renderer/RuntimeRendererRecovery.cpp
    src/game/runtime/renderer/RuntimeRendererStartupState.cpp
    src/game/runtime/RuntimeRestartPolicy.cpp
    src/game/runtime/video/RuntimeSdlEventDispatch.cpp
    src/game/runtime/video/RuntimeSdlInput.cpp
    src/game/runtime/video/RuntimeSdlVideoMode.cpp
    src/game/runtime/startup/RuntimeStartupConfig.cpp
    src/game/runtime/startup/RuntimeStartupPresentation.cpp
    src/game/runtime/startup/RuntimeStartupSession.cpp
    src/game/runtime/startup/RuntimeStartupVideoOverride.cpp
    src/game/runtime/startup/RuntimeWorldLayerPrewarm.cpp
    src/game/runtime/video/RuntimeWindowBootstrap.cpp
    src/game/runtime/session/GameSession.cpp
    src/game/runtime/session/SessionBackendInventoryUi.cpp
    src/game/runtime/session/SessionBackendUnitHydration.cpp
    src/game/runtime/session/SessionBackendRenderHelpers.cpp
    src/game/runtime/session/SessionCoreBootstrapRuntime.cpp
    src/game/runtime/session/SessionDebugSnapshot.cpp
    src/game/runtime/session/SessionLegacyWorldView.cpp
    src/game/runtime/session/SessionFrameMetrics.cpp
    src/game/runtime/session/SessionLoopRuntime.cpp
    src/game/runtime/session/SessionProjectedWorldView.cpp
    src/game/runtime/session/SessionRenderLayout.cpp
    src/game/runtime/session/SessionRenderScratch.cpp
    src/game/runtime/session/SessionRenderConfig.cpp
    src/game/runtime/session/SessionSnapshotRuntime.cpp
    src/game/runtime/session/SessionStartupRuntime.cpp
    src/game/runtime/session/SessionTailFirePrewarm.cpp
    src/game/runtime/session/SessionTextureCache.cpp
    src/game/runtime/session/SessionWorldRenderRuntime.cpp
    src/game/runtime/session/SessionWorldBackdrop.cpp
    src/game/runtime/shared/backend/SharedBackendPoseEval.cpp
    src/game/runtime/shared/capture/SharedCapturePresentation.cpp
    src/game/runtime/shared/capture/SharedCaptureOverlayVfx.cpp
    src/game/runtime/shared/capture/SharedCaptureModelBridge.cpp
    src/game/runtime/shared/capture/SharedCaptureD3d12FastPath.cpp
    src/game/runtime/shared/capture/SharedCapturePreparedMeshCache.cpp
    src/game/runtime/shared/world/SharedBoardGridBatches.cpp
    src/game/runtime/shared/projected/SharedProjectedDebugVfx.cpp
    src/game/runtime/shared/projected/SharedProjectedRenderItems.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitModelRenderer.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp
    src/game/runtime/shared/projected/SharedProjectedUnitOverlays.cpp
    src/game/runtime/shared/ui/SharedBackendDebugViewOverlay.cpp
    src/game/runtime/shared/ui/SharedBackendDebugViewSupport.cpp
    src/game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.cpp
    src/game/runtime/shared/scene/SharedWorldScene.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleBillboardBatches.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.cpp
    src/game/runtime/shared/vfx/particles/SharedParticleVfxStyles.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotBillboards.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireAtlasHelpers.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireSyntheticEmitter.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuSnapshotBatches.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuTileBake.cpp
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.cpp
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
    src/game/ui/ItemInventoryUI.cpp
    src/game/ui/ShopLayout.cpp
    src/game/ui/ClassicShopHud.cpp
    src/game/ui/ShopUiFacade.cpp
    src/game/ui/AdventureHud.cpp

    # Game VFX
    src/game/vfx/TailFireVFX.cpp
    src/game/vfx/TailFireVFXConfigDB.cpp
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
target_link_libraries(PAC_GameObjects PUBLIC Engine glm::glm nlohmann_json::nlohmann_json PAC_GameVfxInterop)
if (WIN32)
    target_link_libraries(PAC_GameObjects PRIVATE dxgi d3d12)
endif()
pac_apply_common_target_settings(PAC_GameObjects)

# ---------------- Preview adapters (game-specific preview bindings) ----------------
add_library(PAC_VfxPreviewSupport STATIC
    src/game/preview/PokemonAutochessVfxPreviewProject.cpp
    src/game/preview/PreviewPokemonVisual.cpp
    src/game/preview/PreviewSceneUtils.cpp
    src/game/preview/PreviewTailFireBridge.cpp
    src/game/preview/effects/GrowlPreviewEffect.cpp
    src/game/preview/effects/LeechSeedPreviewEffect.cpp
    src/vfx/preview/growl/GrowlPreviewController.cpp
    src/vfx/preview/growl/GrowlSharedRenderer.cpp
)
target_sources(PAC_VfxPreviewSupport PRIVATE
    src/game/preview/PokemonAutochessVfxPreviewProject.h
    src/game/preview/PreviewPokemonVisual.h
    src/game/preview/PreviewSceneUtils.h
    src/game/preview/PreviewTailFireBridge.h
    src/game/preview/effects/GrowlPreviewEffect.h
    src/game/preview/effects/LeechSeedPreviewEffect.h
    src/vfx/preview/growl/GrowlPreviewController.h
    src/vfx/preview/growl/GrowlSharedRenderer.h
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.h
    src/game/runtime/shared/vfx/tail_fire/SharedTailFireSyntheticEmitter.h
    src/game/vfx/TailFireVFXConfig.h
)
target_link_libraries(PAC_VfxPreviewSupport
    PUBLIC
        PAC_GameObjects
        engine_tools_vfx_preview
)
pac_apply_common_target_settings(PAC_VfxPreviewSupport)

# ---------------- Generic VFX lab adapters (no game preview dependency) ----------------
add_library(PAC_VfxLabSupport STATIC
    src/vfx/preview/VfxLibraryPreviewProject.cpp
    src/vfx/preview/effects/GrowlLabPreviewEffect.cpp
    src/vfx/preview/growl/GrowlPreviewController.cpp
    src/vfx/preview/growl/GrowlSharedRenderer.cpp
)
target_sources(PAC_VfxLabSupport PRIVATE
    src/vfx/preview/VfxLibraryPreviewProject.h
    src/vfx/preview/effects/GrowlLabPreviewEffect.h
    src/vfx/preview/growl/GrowlPreviewController.h
    src/vfx/preview/growl/GrowlSharedRenderer.h
)
target_link_libraries(PAC_VfxLabSupport
    PUBLIC
        PAC_GameObjects
        engine_tools_vfx_preview
)
pac_apply_common_target_settings(PAC_VfxLabSupport)

# ---------------- Executable ----------------
add_executable(PokemonAutochess
    src/main.cpp
)
target_link_libraries(PokemonAutochess PRIVATE PAC_GameObjects)
pac_apply_common_target_settings(PokemonAutochess)

# ---------------- Tools ----------------
if (PAC_BUILD_TOOLS)
    add_executable(PAC_DataCooker
        tools/PAC_DataCooker.cpp
    )
    target_link_libraries(PAC_DataCooker
        PRIVATE
            nlohmann_json::nlohmann_json
    )
    pac_apply_common_target_settings(PAC_DataCooker)

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

# ---------------- Tests ----------------
if (BUILD_TESTING)
    add_executable(PAC_Tests
        tests/TestMain.cpp
        tests/TestLuaBindings.cpp
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
        tests/TestUiHudFormatting.cpp
        tests/TestUiDebugText.cpp
        tests/TestUiImagePath.cpp
        tests/TestRenderPrepMaterialShading.cpp
        tests/TestRenderPrepMeshNormals.cpp
        tests/TestRenderPrepProceduralPose.cpp
        tests/TestRenderModelCache.cpp
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
        tests/TestLogBus.cpp
        tests/TestRenderFlowDecisions.cpp
        tests/TestBackendRenderPolicy.cpp
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
        tests/TestGltfAsset.cpp
        tests/TestCombatAnimIndexCache.cpp
        tests/TestScriptApiContract.cpp
        tests/TestSessionBackendRenderHelpers.cpp
        tests/TestSessionBackendInventoryUi.cpp
        tests/TestSessionBackendUnitHydration.cpp
        tests/TestSessionDebugSnapshot.cpp
        tests/TestRuntimeGrowlVfxPrewarm.cpp
        tests/TestSessionLegacyWorldView.cpp
        tests/TestSessionFrameMetrics.cpp
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
        tests/TestSharedTailFireAnchorMath.cpp
        tests/TestSharedTailFireAtlasHelpers.cpp
        tests/TestSharedTailFireExactGpuBatches.cpp
        tests/TestSharedTailFireMeshPlayback.cpp
        tests/TestSharedTailFirePlaybackPolicy.cpp
        tests/TestSharedGrowlVfxHelpers.cpp
        tests/TestSharedGrowlWaveBridge.cpp
        tests/TestSharedGrowlWaveBatches.cpp
        tests/TestSharedWorldScene.cpp
        tests/TestSharedWorldIndexedBatches.cpp
        tests/TestD3D12WorldMaterialConstants.cpp
        tests/TestConfigLoaders.cpp
        tests/TestSourceAscii.cpp
    )

    target_link_libraries(PAC_Tests
        PRIVATE
            PAC_GameObjects
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

    if (MSVC)
        target_compile_options(PAC_Tests PRIVATE /std:c++20 /permissive- /Zc:__cplusplus /FS)
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

    foreach(test_name IN LISTS PAC_TEST_NAMES)
        add_test(NAME PAC_Tests.${test_name} COMMAND PAC_Tests --filter ${test_name})
    endforeach()

    # Ensure tests run with repo root as working directory so relative config/script paths resolve.
    set(PAC_ALL_TESTS)
    foreach(test_name IN LISTS PAC_TEST_NAMES)
        list(APPEND PAC_ALL_TESTS PAC_Tests.${test_name})
    endforeach()

    set_tests_properties(${PAC_ALL_TESTS} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        ENVIRONMENT "PAC_DATA_ROOT=${CMAKE_SOURCE_DIR};PAC_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets"
    )

    if (PAC_ENABLE_RUNTIME_SMOKE_TESTS)
        add_test(NAME PAC_RuntimeSmoke.opengl COMMAND PokemonAutochess)
        set_tests_properties(PAC_RuntimeSmoke.opengl PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 45
            ENVIRONMENT
                "PAC_DATA_ROOT=${CMAKE_SOURCE_DIR};PAC_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_RENDER_BACKEND=opengl;PAC_AUTO_QUIT_SECONDS=2;PAC_DISABLE_MODELCACHE=1"
        )

        if (WIN32)
            add_test(NAME PAC_RuntimeSmoke.d3d12 COMMAND PokemonAutochess)
            set_tests_properties(PAC_RuntimeSmoke.d3d12 PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 45
                ENVIRONMENT
                    "PAC_DATA_ROOT=${CMAKE_SOURCE_DIR};PAC_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_RENDER_BACKEND=d3d12;PAC_AUTO_QUIT_SECONDS=2;PAC_DISABLE_MODELCACHE=1"
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
                    "PAC_DATA_ROOT=${CMAKE_SOURCE_DIR};PAC_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets;PAC_DISABLE_MODELCACHE=1"
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
    list(APPEND PAC_ALL_TARGETS PAC_DataCooker PAC_VfxPreviewer VfxLab)
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





