// Card.h

#pragma once

#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <string>

class Shader;

enum class CardType {
    Starter,
    Shop,
    Bench
};

struct CardData {
    std::string pokemonName;
    int cost = 0;
    CardType type = CardType::Shop;
};

class Card {
public:
    Card(const SDL_Rect& rect, const std::string& imagePath);
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;
    Card(Card&& other) noexcept;
    Card& operator=(Card&& other) noexcept;
    ~Card();

    void draw(Shader* uiShader) const;
    bool isPointInside(int x, int y) const;

    void setRect(const SDL_Rect& r) { rect = r; }
    SDL_Rect getRect() const { return rect; }

    void setImagePath(const std::string& path) { imagePath = path; }
    std::string getImagePath() const { return imagePath; }

    void setData(const CardData& data) { cardData = data; }
    const CardData& getData() const { return cardData; }

    static void setGlobalFramePath(const std::string& path);

private:
    SDL_Rect rect;
    std::string imagePath;
    unsigned int textureID;
    int imgWidth, imgHeight, imgChannels;

    CardData cardData;

    static std::string framePath;
    static unsigned int frameTextureID;
    static bool frameLoaded;

    unsigned int loadTexture(const std::string& path);
    static void loadFrameTexture();
};
