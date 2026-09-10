#include "game/state/BackendCardLayoutModel.h"
#include "game/runtime/ui/FrontendBackdrop.h"
#include "game/runtime/ui/FrontendIntro.h"
#include "game/runtime/ui/FrontendCameraSequence.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
CardData makeCard(const std::string& name, CardType type, int level = 1, int cost = 1) {
    CardData c;
    c.pokemonName = name;
    c.type = type;
    c.level = level;
    c.cost = cost;
    return c;
}
}

bool test_backend_card_layout_model_contract(std::string& outFail) {
    using game::state::backend_cards::BuildInput;
    using game::state::backend_cards::LayoutMode;
    using game::state::backend_cards::allItemCards;
    using game::state::backend_cards::buildButtons;

    {
        std::vector<CardData> cards = {
            makeCard("charmander", CardType::Shop),
            makeCard("squirtle", CardType::Shop),
            makeCard("bulbasaur", CardType::Shop)
        };
        if (allItemCards(cards)) {
            outFail = "allItemCards should be false for pokemon cards";
            return false;
        }
    }

    {
        std::vector<CardData> cards = {
            makeCard("potion", CardType::Item),
            makeCard("berry", CardType::Item)
        };
        if (!allItemCards(cards)) {
            outFail = "allItemCards should be true for pure item rows";
            return false;
        }
    }

    {
        BuildInput in;
        in.cards = {
            makeCard("charmander", CardType::Shop),
            makeCard("squirtle", CardType::Shop),
            makeCard("bulbasaur", CardType::Shop)
        };
        in.uiW = 1280;
        in.uiH = 720;
        in.mode = LayoutMode::Shop;
        const auto buttons = buildButtons(in);
        if (buttons.size() != 3u) {
            outFail = "buildButtons shop mode size mismatch";
            return false;
        }
        if (!(buttons[0].x < buttons[1].x && buttons[1].x < buttons[2].x)) {
            outFail = "shop button x positions should be strictly increasing";
            return false;
        }
        if (!(std::abs(buttons[0].y - buttons[1].y) < 0.001f &&
              std::abs(buttons[1].y - buttons[2].y) < 0.001f)) {
            outFail = "shop button y positions should be aligned";
            return false;
        }
        if (buttons[0].item || buttons[1].item || buttons[2].item) {
            outFail = "pokemon buttons should not be marked as item row";
            return false;
        }
    }

    {
        BuildInput in;
        in.cards = {
            makeCard("potion", CardType::Item),
            makeCard("berry", CardType::Item)
        };
        in.uiW = 1280;
        in.uiH = 720;
        in.mode = LayoutMode::Shop;
        in.forceItemRow = true;
        const auto buttons = buildButtons(in);
        if (buttons.size() != 2u) {
            outFail = "buildButtons item row size mismatch";
            return false;
        }
        if (!buttons[0].item || !buttons[1].item) {
            outFail = "forceItemRow should mark all buttons as item";
            return false;
        }
        const game::ui::ShopRowLayout layout = game::ui::computeShopRowLayout(in.uiW, in.uiH, true);
        const float expectedY = static_cast<float>(std::max(
            layout.edgeMargin + 64,
            static_cast<int>(std::round(static_cast<float>(in.uiH) * 0.16f))));
        if (std::abs(buttons[0].y - expectedY) > 0.001f) {
            outFail = "item row y position should use top shelf layout";
            return false;
        }
    }

    {
        BuildInput in;
        in.cards = {
            makeCard("charmander", CardType::Shop),
            makeCard("squirtle", CardType::Shop)
        };
        in.uiW = 1000;
        in.uiH = 600;
        in.mode = LayoutMode::Starter;
        const auto buttons = buildButtons(in);
        if (buttons.size() != 2u) {
            outFail = "buildButtons starter mode size mismatch";
            return false;
        }
        if (buttons[0].x < 0.0f || buttons[0].x >= buttons[1].x) {
            outFail = "starter mode should place cards in a centered horizontal row";
            return false;
        }
        if (std::abs(buttons[0].w - 176.0f) > 0.001f || std::abs(buttons[0].h - 120.0f) > 0.001f) {
            outFail = "starter mode should retain readable card dimensions at normal viewport sizes";
            return false;
        }
        if (std::abs((buttons[1].x - buttons[0].x) - 216.0f) > 0.001f) {
            outFail = "starter mode should retain normal row spacing";
            return false;
        }
        if (buttons[0].y < in.uiH * 0.6f || buttons[0].y + buttons[0].h > in.uiH - 32) {
            outFail = "starter choices must leave the lab visible and room for the input hint";
            return false;
        }
    }

    for (const auto [width, height] : {std::pair{480, 320}, {800, 600}, {1280, 720}, {2560, 1080}}) {
        BuildInput in;
        in.cards = {makeCard("bulbasaur", CardType::Shop), makeCard("charmander", CardType::Shop),
                    makeCard("squirtle", CardType::Shop)};
        in.uiW = width;
        in.uiH = height;
        in.mode = LayoutMode::Starter;
        const auto buttons = buildButtons(in);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const auto &button = buttons[i];
            if (button.x < 0 || button.y < height * 0.5f || button.x + button.w > width ||
                button.y + button.h > height || (i && buttons[i - 1].x + buttons[i - 1].w >= button.x)) {
                outFail = "resized starter choices must remain visible and independently clickable";
                return false;
            }
        }
        const auto backdrop = game::runtime::ui_frontend::backdropSprite("lab.png", 1.6f, width, height);
        const float visibleAspect = 1.6f * (backdrop.u1 - backdrop.u0) / (backdrop.v1 - backdrop.v0);
        if (std::abs(visibleAspect - static_cast<float>(width) / height) > 0.001f ||
            std::abs(backdrop.u0 + backdrop.u1 - 1) > 0.001f ||
            std::abs(backdrop.v0 + backdrop.v1 - 1) > 0.001f) {
            outFail = "lab backdrop must preserve proportions and center when cropped";
            return false;
        }
        for (float zoom : {1.0f, 1.4f, 2.15f}) {
            const auto focused = game::runtime::ui_frontend::backdropSprite(
                "lab.png", 1.6f, width, height, .706f, .575f, zoom);
            const float aspect = 1.6f * (focused.u1 - focused.u0) / (focused.v1 - focused.v0);
            if (focused.u0 < 0 || focused.u1 > 1 || focused.v0 < 0 || focused.v1 > 1 ||
                std::abs(aspect - static_cast<float>(width) / height) > .001f) {
                outFail = "intro pan/zoom must preserve proportions without exposing empty edges";
                return false;
            }
        }
    }

    using game::runtime::ui_frontend::CameraSequence;
    using game::runtime::ui_frontend::cameraSequenceSprites;
    CameraSequence sequence{.atlasPrefix = "camera_", .finalImage = "table.png", .frameCount = 116};
    const auto opening = cameraSequenceSprites(sequence, "lab.png", 1.6f, 844, 512, 0.0f);
    const auto final = cameraSequenceSprites(sequence, "lab.png", 1.6f, 844, 512, 1.0f);
    if (sequence.pageCount() != 15 || opening.size() != 1 || opening[0].texturePath != "lab.png" ||
        final.size() != 1 || final[0].texturePath != "table.png") {
        outFail = "camera playback must use full-resolution stills at both endpoints";
        return false;
    }
    for (const auto [width, height] : {std::pair{480, 320}, {844, 512}, {2560, 1080}}) {
        for (int frame = 0; frame < 116; ++frame) {
            const auto sprites = cameraSequenceSprites(sequence, "lab.png", 1.6f, width, height,
                                                        (frame + (frame == 115 ? -.25f : .25f)) / 115.0f);
            if (sprites.size() != 1 || sprites[0].a != 1) {
                outFail = "camera playback must use one opaque sample without crossfade ghosting";
                return false;
            }
            {
                const auto& sprite = sprites[0];
                const int tile = frame % 8;
                const float x = (tile % 4)*804.0f, y = (tile / 4)*504.0f;
                const float aspect = (sprite.u1 - sprite.u0)*3216 / ((sprite.v1 - sprite.v0)*1008);
                if (sprite.texturePath != "camera_" + std::to_string(frame/8) + ".png" ||
                    sprite.u0*3216 < x+1.9f || sprite.u1*3216 > x+802.1f ||
                    sprite.v0*1008 < y+1.9f || sprite.v1*1008 > y+502.1f ||
                    std::abs(aspect - static_cast<float>(width)/height) > .001f) {
                    outFail = "camera sampling must stay within the padded frame and preserve aspect after resize";
                    return false;
                }
            }
        }
    }
    sequence.columns = 0;
    const auto fallback = cameraSequenceSprites(sequence, "lab.png", 1.6f, 844, 512, .5f);
    if (fallback.size() != 1 || fallback[0].texturePath != "lab.png") {
        outFail = "invalid camera layouts must safely fall back to the opening image";
        return false;
    }

    game::runtime::ui_frontend::FrontendIntro intro;
    if (!intro.frame().selectionReady || intro.frame().uiAlpha != 1.0f) {
        outFail = "frontends without an intro must remain immediately interactive";
        return false;
    }
    intro.reset({.enabled = true});
    intro.advance(-1.0f);
    intro.advance(std::numeric_limits<float>::quiet_NaN());
    intro.advance(std::numeric_limits<float>::infinity());
    if (intro.frame().uiAlpha != 0.0f || intro.frame().selectionReady) {
        outFail = "invalid or backward time must not skip the starter intro";
        return false;
    }

    return true;
}
