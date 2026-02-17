#include "game/state/BackendCardLayoutModel.h"

#include <cmath>
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
        if (buttons[0].y <= 0.0f) {
            outFail = "starter mode y should be positive";
            return false;
        }
    }

    return true;
}
