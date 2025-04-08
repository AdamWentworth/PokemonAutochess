// CardFactory.cpp

#include "CardFactory.h"

namespace CardFactory {

std::vector<Card> createCardRow(const std::vector<CardData>& dataList, int screenWidth, int yOffset) {
    std::vector<Card> cards;

    const int cardWidth = 220;
    const int cardHeight = 150;
    const int spacing = 50;

    int totalWidth = static_cast<int>(dataList.size()) * (cardWidth + spacing) - spacing;
    int startX = (screenWidth - totalWidth) / 2;

    for (size_t i = 0; i < dataList.size(); ++i) {
        const CardData& data = dataList[i];
        std::string imagePath = "assets/images/" + data.pokemonName + ".png";

        SDL_Rect rect = {
            startX + static_cast<int>(i) * (cardWidth + spacing),
            yOffset,
            cardWidth,
            cardHeight
        };

        Card card(rect, imagePath);
        card.setData(data);
        cards.push_back(std::move(card));
    }

    return cards;
}

} // namespace CardFactory
