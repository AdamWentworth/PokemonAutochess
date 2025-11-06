// GameConfig.h

#pragma once
#include <string>

struct GameConfigData {
    int cols = 8;
    int rows = 8;
    float cellSize = 1.2f;
    int benchSlots = 8;

    std::string fontPath = "assets/fonts/GillSans.ttf";
    int fontSize = 48;
};

class GameConfig {
public:
    // Loads once from scripts/config/game.lua and caches
    static const GameConfigData& get();
};
