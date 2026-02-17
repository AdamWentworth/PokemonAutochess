#include "game/state/ShopCardConversion.h"

#include <string>
#include <vector>

bool test_shop_card_conversion_contract(std::string& outFail) {
    std::vector<CardData> cards;

    {
        CardData c;
        c.pokemonName = "charmander";
        c.level = 3;
        c.cost = 2;
        c.type = CardType::Shop;
        cards.push_back(c);
    }
    {
        CardData c;
        c.pokemonName = "potion";
        c.level = 1;
        c.cost = 1;
        c.type = CardType::Item;
        cards.push_back(c);
    }
    {
        CardData c;
        c.pokemonName = "squirtle";
        c.level = 0;
        c.cost = -5;
        c.type = CardType::Shop;
        cards.push_back(c);
    }
    {
        CardData c;
        c.pokemonName = "";
        c.level = 4;
        c.cost = 4;
        c.type = CardType::Shop;
        cards.push_back(c);
    }

    const auto out = game::state::shop_cards::toClassicCards(cards);
    if (out.size() != 2u) {
        outFail = "toClassicCards should drop item/empty-name cards";
        return false;
    }
    if (out[0].name != "charmander" || out[0].level != 3 || out[0].cost != 2) {
        outFail = "toClassicCards should preserve pokemon card fields";
        return false;
    }
    if (out[1].name != "squirtle" || out[1].level != 1 || out[1].cost != 0) {
        outFail = "toClassicCards should clamp level/cost bounds";
        return false;
    }

    return true;
}
