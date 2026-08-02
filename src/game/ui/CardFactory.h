// CardFactory.h

#pragma once

#include "game/ui/legacy/Card.h"
#include <string>
#include <vector>

namespace CardFactory {
    // Creates a row of cards based on CardData (starter or shop).
    std::vector<Card> createCardRow(const std::vector<CardData>& dataList, int screenWidth, int yOffset);
}
