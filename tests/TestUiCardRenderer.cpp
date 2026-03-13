#include "game/runtime/backend_ui/CardRenderer.h"
#include "engine/render/SpriteTextureCardArt.h"

#include <string>
#include <vector>

bool test_ui_card_renderer_contract(std::string& outFail) {
    using engine::render::sprite_card_art::isProxyPath;
    using engine::render::sprite_card_art::sourcePathFromProxy;
    std::vector<IRenderBackend::DebugQuad> quads;
    std::vector<IRenderBackend::DebugSprite> sprites;

    game::runtime::ui_card_renderer::CardRenderInput in;
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

    game::runtime::ui_card_renderer::appendCard(quads, &sprites, in);

    if (quads.empty()) {
        outFail = "appendCard should emit visual quads";
        return false;
    }
    if (sprites.size() != 2u) {
        outFail = "appendCard should emit art + frame sprites when texture path resolves";
        return false;
    }

    const auto& artSprite = sprites.front();
    if (!isProxyPath(artSprite.texturePath) ||
        sourcePathFromProxy(artSprite.texturePath) != "assets/images/charmander.png") {
        outFail = "appendCard should map explicit art image path to backend card art proxy";
        return false;
    }
    if (artSprite.u0 != 0.20f || artSprite.v0 != 0.10f ||
        artSprite.u1 != 0.60f || artSprite.v1 != 0.90f) {
        outFail = "appendCard should propagate UV bounds to sprite";
        return false;
    }
    if (artSprite.w <= 0.0f || artSprite.h <= 0.0f) {
        outFail = "appendCard should emit positive sprite geometry";
        return false;
    }
    const auto& frameSprite = sprites.back();
    if (frameSprite.texturePath != "assets/ui/frame_gold.png") {
        outFail = "appendCard should emit legacy gold frame sprite";
        return false;
    }

    {
        std::vector<IRenderBackend::DebugQuad> baseQuads;
        std::vector<IRenderBackend::DebugQuad> textQuads;
        std::vector<IRenderBackend::DebugLine> textLines;
        std::vector<IRenderBackend::DebugSprite> layeredSprites;
        game::runtime::ui_card_renderer::appendCardLayered(
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
        if (layeredSprites.size() != 2u) {
            outFail = "appendCardLayered should preserve art + frame sprite emission";
            return false;
        }
    }

    {
        std::vector<IRenderBackend::DebugQuad> baseQuads;
        std::vector<IRenderBackend::DebugLine> textLines;
        std::vector<IRenderBackend::DebugSprite> layeredSprites;
        game::runtime::ui_card_renderer::appendCardLayered(
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
        if (layeredSprites.size() != 2u) {
            outFail = "appendCardLayered should still emit art + frame sprites without a text-quad sink";
            return false;
        }
    }

    return true;
}



