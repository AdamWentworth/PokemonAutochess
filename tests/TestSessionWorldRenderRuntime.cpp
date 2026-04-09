#include "game/runtime/session/SessionWorldRenderRuntime.h"

#include "game/GameConfig.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/ui/InventoryPanel.h"
#include "TestRenderBackendDoubles.h"

#include <string>
#include <unordered_map>

namespace {
using test::render_doubles::ConfigurableFakeRenderBackend;
using test::render_doubles::FakeRenderBackendConfig;

} // namespace

bool test_session_world_render_runtime_contract(std::string& outFail) {
    ConfigurableFakeRenderBackend renderer(FakeRenderBackendConfig{
        .backendId = "test",
        .supportsWorldTriangles3D = true,
    });
    GameConfigData config;
    config.rows = 4;
    config.cols = 8;
    config.benchSlots = 8;
    GameDataDb dataDb;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> textures;
    game::runtime::ui_inventory_panel::PanelState inventoryPanel;
    inventoryPanel.hitRegions.push_back(
        game::runtime::ui_inventory_panel::HitRegion{});

    const std::size_t warmed = game::runtime::session_world_render_runtime::render(
        {
            .renderer = &renderer,
            .backendInventoryPanel = &inventoryPanel,
            .config = &config,
            .dataDb = &dataDb,
            .backendTextureByPath = &textures,
            .routes = game::runtime::render::makeRenderRoutes(true),
            .renderWorld = false,
            .allowBackendMenuBackdrop = false,
            .prewarmWorldIndexedOnly = true,
            .drawableW = 1280,
            .drawableH = 720,
            .ensureBackendMeshLoaded =
                [](const std::string&) {
                    return static_cast<game::runtime::render_model::MeshData*>(nullptr);
                },
            .ensureBackendTextureLoaded =
                [](const std::string&, bool) {
                    return static_cast<game::runtime::SharedBackendTextureCacheEntry*>(nullptr);
                },
        });

    if (warmed != 0u || !inventoryPanel.hitRegions.empty()) {
        outFail =
            "SessionWorldRenderRuntime should clear stale inventory hit regions and no-op prewarm requests when no indexed world batches are available.";
        return false;
    }

    return true;
}
