#include "game/runtime/BackendCardRenderer.h"

#include <string>
#include <vector>

bool test_backend_card_renderer_contract(std::string& outFail) {
    std::vector<IRenderBackend::DebugQuad> quads;
    std::vector<IRenderBackend::DebugSprite> sprites;

    game::runtime::backend_card_renderer::CardRenderInput in;
    in.x = 24.0f;
    in.y = 32.0f;
    in.w = 220.0f;
    in.h = 150.0f;
    in.displayName = "Charmander";
    in.speciesName = "charmander";
    in.subtitle = "Lv 5 Cost 3g";
    in.explicitImagePath = "assets/images/charmander.png";
    in.u0 = 0.20f;
    in.v0 = 0.10f;
    in.u1 = 0.60f;
    in.v1 = 0.90f;
    in.keyboardSlot = 1;

    game::runtime::backend_card_renderer::appendCard(quads, &sprites, in);

    if (quads.empty()) {
        outFail = "appendCard should emit visual quads";
        return false;
    }
    if (sprites.size() != 1u) {
        outFail = "appendCard should emit one sprite when texture path resolves";
        return false;
    }

    const auto& sprite = sprites.front();
    if (sprite.texturePath != "assets/images/charmander.png") {
        outFail = "appendCard should preserve explicit image path";
        return false;
    }
    if (sprite.u0 != 0.20f || sprite.v0 != 0.10f ||
        sprite.u1 != 0.60f || sprite.v1 != 0.90f) {
        outFail = "appendCard should propagate UV bounds to sprite";
        return false;
    }
    if (sprite.w <= 0.0f || sprite.h <= 0.0f) {
        outFail = "appendCard should emit positive sprite geometry";
        return false;
    }

    {
        std::vector<IRenderBackend::DebugQuad> baseQuads;
        std::vector<IRenderBackend::DebugQuad> textQuads;
        std::vector<IRenderBackend::DebugLine> textLines;
        std::vector<IRenderBackend::DebugSprite> layeredSprites;
        game::runtime::backend_card_renderer::appendCardLayered(
            baseQuads,
            &textQuads,
            &layeredSprites,
            in,
            &textLines);
        if (baseQuads.empty()) {
            outFail = "appendCardLayered should emit base quads";
            return false;
        }
        if (textLines.empty()) {
            outFail = "appendCardLayered should emit text lines when line sink is provided";
            return false;
        }
        if (!textQuads.empty()) {
            outFail = "appendCardLayered should avoid text quads when line sink is provided";
            return false;
        }
        if (layeredSprites.size() != 1u) {
            outFail = "appendCardLayered should preserve sprite emission";
            return false;
        }
    }

    {
        std::vector<IRenderBackend::DebugQuad> baseQuads;
        std::vector<IRenderBackend::DebugLine> textLines;
        std::vector<IRenderBackend::DebugSprite> layeredSprites;
        game::runtime::backend_card_renderer::appendCardLayered(
            baseQuads,
            nullptr,
            &layeredSprites,
            in,
            &textLines);
        if (baseQuads.empty()) {
            outFail = "appendCardLayered should emit base quads without a text-quad sink";
            return false;
        }
        if (textLines.empty()) {
            outFail = "appendCardLayered should still emit text lines without a text-quad sink";
            return false;
        }
        if (layeredSprites.size() != 1u) {
            outFail = "appendCardLayered should still emit sprites without a text-quad sink";
            return false;
        }
    }

    return true;
}
