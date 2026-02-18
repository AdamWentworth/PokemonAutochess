#include "game/runtime/BackendUnitVisuals.h"

#include <string>

bool test_backend_unit_visuals_contract(std::string& outFail) {
    using game::runtime::backend_units::applyWorldUnitTint;
    using game::runtime::backend_units::makeBenchUnitSprite;
    using game::runtime::backend_units::makeWorldUnitSprite;
    using game::runtime::backend_units::resolveWorldUnitImagePath;

    {
        IRenderBackend::DebugQuad q;
        PokemonInstance u;
        u.side = PokemonSide::Player;
        u.alive = true;
        applyWorldUnitTint(q, u);
        if (!(q.g > q.r && q.g > q.b && q.a > 0.0f)) {
            outFail = "player tint should be green-biased with non-zero alpha";
            return false;
        }
    }

    {
        IRenderBackend::DebugQuad q;
        PokemonInstance u;
        u.side = PokemonSide::Enemy;
        u.alive = false;
        u.captureInProgress = false;
        applyWorldUnitTint(q, u);
        if (!(q.r >= q.g && q.r >= q.b && q.a > 0.0f)) {
            outFail = "enemy tint should remain red-biased";
            return false;
        }
    }

    {
        const std::string image = resolveWorldUnitImagePath("charmander");
        if (image != "assets/images/charmander.png") {
            outFail = "resolveWorldUnitImagePath should resolve known portrait assets";
            return false;
        }
    }

    {
        const auto missing = makeWorldUnitSprite(100.0f, 80.0f, 40.0f, 50.0f, "");
        if (!missing.texturePath.empty()) {
            outFail = "world sprite should stay empty when no texture path is provided";
            return false;
        }

        const auto sprite = makeWorldUnitSprite(
            100.0f,
            80.0f,
            40.0f,
            50.0f,
            "assets/images/charmander.png");
        if (sprite.texturePath != "assets/images/charmander.png") {
            outFail = "world sprite should preserve texture path";
            return false;
        }
        if (!(sprite.w > 0.0f && sprite.h > 0.0f)) {
            outFail = "world sprite geometry should be positive";
            return false;
        }
    }

    {
        const auto bench = makeBenchUnitSprite(
            10.0f,
            20.0f,
            50.0f,
            40.0f,
            "assets/images/charmander.png");
        if (bench.texturePath != "assets/images/charmander.png") {
            outFail = "bench sprite should preserve texture path";
            return false;
        }
        if (!(bench.x > 10.0f && bench.y > 20.0f && bench.w < 50.0f && bench.h < 40.0f)) {
            outFail = "bench sprite should apply inset padding";
            return false;
        }
    }

    return true;
}
