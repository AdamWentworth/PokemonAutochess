#include "game/runtime/session/SessionProjectedWorldView.h"

#include "engine/render/Camera3D.h"
#include "game/GameConfig.h"
#include "game/world/GameWorld.h"

#include <string>
#include <unordered_map>

namespace {

PokemonInstance makeUnit(int id,
                         const std::string& name,
                         PokemonSide side,
                         const glm::vec3& position) {
    PokemonInstance unit;
    unit.id = id;
    unit.name = name;
    unit.side = side;
    unit.position = position;
    unit.alive = true;
    unit.hp = 100;
    unit.maxHP = 100;
    unit.level = 3;
    unit.visualScale = 1.0f;
    unit.captureScale = 1.0f;
    return unit;
}

game::runtime::session_projected_world_view::Args makeArgs(
    GameWorld& world,
    Camera3D& camera,
    ::GameDataDb& dataDb,
    game::runtime::session_render_scratch::RenderScratch& scratch,
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry>& textures) {
    game::runtime::session_projected_world_view::Args args;
    args.renderer = nullptr;
    args.gameWorld = &world;
    args.camera = &camera;
    args.dataDb = &dataDb;
    args.scratch = &scratch;
    args.backendTextureByPath = &textures;
    args.sharedUnitHudCfg = {10, 1.35f};
    args.supportsWorldTriangles3D = true;
    args.supportsWorldIndexedMeshes = false;
    args.allowPortraitFallback = true;
    args.drawableW = 1280;
    args.drawableH = 720;
    args.rows = 4;
    args.cols = 8;
    args.benchSlots = 8;
    args.minDim = 720.0f;
    args.boardX = 140.0f;
    args.boardY = 120.0f;
    args.boardW = 800.0f;
    args.boardH = 360.0f;
    args.cellW = 100.0f;
    args.cellH = 90.0f;
    args.simNowSec = 12.0;
    args.ensureBackendMeshLoaded = [](const std::string&) {
        return static_cast<game::runtime::render_model::MeshData*>(nullptr);
    };
    args.ensureBackendTextureLoaded =
        [&](const std::string& texturePath,
            bool) -> game::runtime::SharedBackendTextureCacheEntry* {
            auto& entry = textures[texturePath.empty() ? "__white__" : texturePath];
            if (!entry.attemptedLoad) {
                entry.attemptedLoad = true;
                entry.valid = true;
                entry.width = 1;
                entry.height = 1;
                entry.rgba = {255u, 255u, 255u, 255u};
            }
            return &entry;
        };
    return args;
}

} // namespace

bool test_session_projected_world_view_contract(std::string& outFail) {
    GameConfigData cfg;
    cfg.rows = 4;
    cfg.cols = 8;
    cfg.benchSlots = 8;

    GameWorld world(cfg);
    world.getPokemons().push_back(
        makeUnit(1, "bulbasaur", PokemonSide::Player, world.gridToWorld(1, 1)));
    world.getBenchPokemons().push_back(
        makeUnit(2, "charmander", PokemonSide::Player, world.gridToWorld(2, 1)));

    Camera3D camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
    camera.lookAt(glm::vec3(0.0f, -1.0f, 0.0f));

    ::GameDataDb dataDb;
    game::runtime::session_render_scratch::RenderScratch scratch;
    game::runtime::session_render_scratch::ensureCapacity(scratch);
    game::runtime::session_render_scratch::beginFrame(scratch, true);
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> textures;

    const auto result = game::runtime::session_projected_world_view::appendProjectedWorldView(
        makeArgs(world, camera, dataDb, scratch, textures));

    if (!result.hasWorldViewProj ||
        result.visibleAnimatedUnits != 2u ||
        result.projectedUnitsProcessed != 2u) {
        outFail =
            "SessionProjectedWorldView should compose projected board and bench units and report projected unit metrics.";
        return false;
    }

    if (scratch.lines.empty() ||
        (scratch.worldBackgroundQuads.empty() && scratch.world3DTriangles.empty()) ||
        scratch.worldQuads.empty() ||
        scratch.textLines.empty()) {
        outFail =
            "SessionProjectedWorldView should build projected backdrop and overlay batches into render scratch.";
        return false;
    }

    if (result.worldBackdropComposeMs < 0.0f ||
        result.worldVfxBridgeMs < 0.0f ||
        result.worldDepthFlushMs < 0.0f ||
        result.projectedUnitsMs < 0.0f) {
        outFail =
            "SessionProjectedWorldView should return non-negative projected render timing metrics.";
        return false;
    }

    return true;
}
