#include "game/runtime/session/SessionLegacyWorldView.h"

#include "game/GameConfig.h"
#include "game/world/GameWorld.h"

#include <string>

namespace {

PokemonInstance makeUnit(const std::string& name,
                         PokemonSide side,
                         const glm::vec3& position) {
    PokemonInstance unit;
    unit.name = name;
    unit.side = side;
    unit.position = position;
    unit.alive = true;
    unit.hp = 100;
    unit.maxHP = 100;
    unit.level = 3;
    return unit;
}

game::runtime::session_legacy_world_view::Args makeArgs() {
    game::runtime::session_legacy_world_view::Args args;
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
    args.sharedUnitHudCfg = {10, 1.35f};
    return args;
}

} // namespace

bool test_session_legacy_world_view_contract(std::string& outFail) {
    using game::runtime::session_legacy_world_view::appendLegacyWorldView;

    {
        auto args = makeArgs();
        args.renderWorld = false;
        game::runtime::session_render_scratch::RenderScratch scratch;
        const auto result = appendLegacyWorldView(args, scratch);
        if (result.visibleAnimatedUnits != 0u ||
            scratch.worldBackgroundQuads.size() != 1u + static_cast<std::size_t>(args.rows * args.cols) ||
            scratch.lines.size() != static_cast<std::size_t>((args.cols + 1) + (args.rows + 1)) ||
            !scratch.worldQuads.empty() ||
            !scratch.sprites.empty()) {
            outFail = "SessionLegacyWorldView should build legacy board backdrop geometry without world-unit overlays when renderWorld is disabled.";
            return false;
        }
    }

    {
        GameConfigData cfg;
        cfg.rows = 4;
        cfg.cols = 8;
        cfg.benchSlots = 8;
        GameWorld world(cfg);
        world.getPokemons().push_back(makeUnit("bulbasaur", PokemonSide::Player, world.gridToWorld(1, 1)));
        world.getBenchPokemons().push_back(makeUnit("charmander", PokemonSide::Player, glm::vec3(0.0f, 0.0f, 0.0f)));

        auto args = makeArgs();
        args.renderWorld = true;
        args.gameWorld = &world;
        game::runtime::session_render_scratch::RenderScratch scratch;
        const auto result = appendLegacyWorldView(args, scratch);
        if (result.visibleAnimatedUnits != 2u ||
            scratch.sprites.size() < 2u ||
            scratch.textLines.empty() ||
            scratch.worldQuads.empty()) {
            outFail = "SessionLegacyWorldView should add board and bench unit overlays for the legacy world fallback path.";
            return false;
        }
    }

    return true;
}
