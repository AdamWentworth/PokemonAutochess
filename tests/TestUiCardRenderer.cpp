#include "game/runtime/ui/CardRenderer.h"
#include "engine/render/SpriteTextureCardArt.h"

#include <string>
#include <vector>
#include <cmath>

bool test_ui_card_renderer_contract(std::string& outFail) {
    namespace artwork = game::runtime::pokemon_artwork;
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
    if (artSprite.texturePath != "assets/ui/pokemon/tcg/004.jpg") {
        outFail = "appendCard should choose the catalog scan even when old states carry legacy artwork paths.";
        return false;
    }
    const auto &charmander = *artwork::find("charmander");
    if (artSprite.u0 * charmander.width < charmander.art.x + charmander.art.w * .20f - .001f ||
        artSprite.v0 * charmander.height < charmander.art.y + charmander.art.h * .10f - .001f ||
        artSprite.u1 * charmander.width > charmander.art.x + charmander.art.w * .60f + .001f ||
        artSprite.v1 * charmander.height > charmander.art.y + charmander.art.h * .90f + .001f) {
        outFail = "Pokemon card UVs must stay within their requested portion of the illustration.";
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

    for (const auto &entry : artwork::entries) {
        for (const auto size : {std::pair{90.25f, 61.0f}, std::pair{220.0f, 150.0f},
                                std::pair{180.0f, 180.0f}, std::pair{300.0f, 120.0f}}) {
            auto card = in;
            card.speciesName = std::string(entry.species);
            card.w = size.first;
            card.h = size.second;
            card.u0 = card.v0 = 0;
            card.u1 = card.v1 = 1;
            std::vector<IRenderBackend::DebugQuad> cardQuads;
            std::vector<IRenderBackend::DebugSprite> cardSprites, portraitSprites;
            game::runtime::ui_card_renderer::appendCard(cardQuads, &cardSprites, card);
            artwork::appendPortrait(portraitSprites, entry.species, 10, 10, 52);
            if (cardSprites.size() != 2 || portraitSprites.size() != 1 ||
                cardSprites[0].texturePath != portraitSprites[0].texturePath) {
                outFail = "All 151 Pokemon must share one scan between cards and portraits.";
                return false;
            }
            const auto &sprite = cardSprites[0];
            const float pixelW = (sprite.u1 - sprite.u0) * entry.width;
            const float pixelH = (sprite.v1 - sprite.v0) * entry.height;
            if (sprite.u0 * entry.width < entry.art.x - .001f || sprite.v0 * entry.height < entry.art.y - .001f ||
                sprite.u1 * entry.width > entry.art.x + entry.art.w + .001f ||
                sprite.v1 * entry.height > entry.art.y + entry.art.h + .001f ||
                std::abs(pixelW / pixelH - sprite.w / sprite.h) > .001f) {
                outFail = "Card art must fill the frame without stretching or sampling physical-card labels at any aspect ratio.";
                return false;
            }
        }
    }
    {
        auto item = in;
        item.item = true;
        std::vector<IRenderBackend::DebugQuad> itemQuads;
        std::vector<IRenderBackend::DebugSprite> itemSprites;
        game::runtime::ui_card_renderer::appendCard(itemQuads, &itemSprites, item);
        if (itemSprites[0].texturePath != in.explicitImagePath || itemSprites[0].u0 != .20f ||
            itemSprites[0].v0 != .10f || itemSprites[0].u1 != .60f || itemSprites[0].v1 != .90f) {
            outFail = "Item images and atlas UVs must remain independent of the Pokemon artwork catalog.";
            return false;
        }
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



