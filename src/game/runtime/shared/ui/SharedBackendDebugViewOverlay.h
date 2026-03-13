#pragma once

#include "engine/core/ecs/Entity.h"
#include "engine/render/IRenderBackend.h"
#include "game/runtime/backend_ui/InventoryPanel.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <functional>
#include <vector>

class Camera3D;
struct EngineServices;
struct EngineRenderBuildBreakdown;
struct GameServices;
class GameWorld;

namespace LogBus {
class Logger;
}

namespace engine::ecs {
class World;
}

namespace game::runtime::shared_backend_debug_view {

struct ComposeAndSubmitArgs {
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;
    GameServices* services = nullptr;
    GameWorld* gameWorld = nullptr;
    Camera3D* camera = nullptr;
    engine::ecs::World* ecsWorld = nullptr;
    engine::ecs::Entity roundPhaseEntity{};
    LogBus::Logger* log = nullptr;
    ui_inventory_panel::PanelState* backendInventoryPanel = nullptr;
    std::function<void()> refreshBackendInventoryFromWorld;

    bool showPerfOverlay = false;
    bool renderWorld = false;
    bool hasWorldViewProj = false;
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;

    int drawableW = 0;
    int drawableH = 0;
    float edgePad = 0.0f;
    float lineStep = 0.0f;
    float uiScale = 1.0f;
    const float* worldViewProj = nullptr;
    EngineRenderBuildBreakdown* renderBuildBreakdown = nullptr;

    std::vector<IRenderBackend::DebugQuad>* worldBackgroundQuads = nullptr;
    std::vector<IRenderBackend::DebugQuad>* worldQuads = nullptr;
    std::vector<IRenderBackend::DebugTriangle>* worldTriangles = nullptr;
    std::vector<IRenderBackend::WorldTriangle>* world3DTriangles = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::vector<IRenderBackend::DebugQuad>* overlayQuads = nullptr;
    std::vector<IRenderBackend::DebugLine>* lines = nullptr;
    std::vector<IRenderBackend::DebugLine>* textLines = nullptr;
    std::vector<IRenderBackend::DebugSprite>* sprites = nullptr;
};

void composeAndSubmit(const ComposeAndSubmitArgs& args);

} // namespace game::runtime::shared_backend_debug_view



