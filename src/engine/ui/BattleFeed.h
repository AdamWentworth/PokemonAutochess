// BattleFeed.h

#pragma once
#include <string>
#include <deque>
#include <vector>
#include <glm/glm.hpp>
#include <memory>

class TextRenderer;

struct FeedLine {
    std::string text;
    glm::vec3 color;
    float age = 0.f;
    float lifetime = 3.f;
};

class BattleFeed {
public:
    BattleFeed(const std::string& fontPath, int fontSize);
    void update(float dt);
    void render(int screenW, int screenH);

    void push(const std::string& msg, const glm::vec3& color = {1,1,1}, float lifetime = 3.f);
    void clear();

private:
    std::unique_ptr<TextRenderer> text;
    std::deque<FeedLine> lines;
    int   maxLines  = 5;
    float lineGap   = 4.f;
    float wrapWidth = 520.f;   // pixels
    float baseScale = 0.6f;    // smaller than your title text

    // Not const: relies on TextRenderer metrics which may not be const-safe everywhere
    std::vector<std::string> wrap(const std::string& s, float maxWidth, float scale);
};
