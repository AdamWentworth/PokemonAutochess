#pragma once

#include "game/GameWorld.h"
#include "game/ui/legacy/Card.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace game::state::shop_cards {

inline std::vector<GameWorld::ClassicShopCard> toClassicCards(const std::vector<CardData>& cards) {
    std::vector<GameWorld::ClassicShopCard> out;
    out.reserve(cards.size());
    for (const auto& cardData : cards) {
        if (cardData.pokemonName.empty()) continue;
        if (cardData.type == CardType::Item) continue;
        GameWorld::ClassicShopCard card;
        card.name = cardData.pokemonName;
        card.level = std::max(1, cardData.level);
        card.cost = std::max(0, cardData.cost);
        out.push_back(std::move(card));
    }
    return out;
}

} // namespace game::state::shop_cards
