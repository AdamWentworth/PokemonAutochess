// BattleFeed.cpp

#include "BattleFeed.h"
#include "TextRenderer.h"
#include <algorithm>
#include <glad/glad.h>

BattleFeed::BattleFeed(const std::string& fontPath, int fontSize) {
    text = std::make_unique<TextRenderer>(fontPath, fontSize);
}

void BattleFeed::push(const std::string& msg, const glm::vec3& color, float lifetime) {
    if (msg.empty()) return;
    lines.push_back({msg, color, 0.f, lifetime});
    while ((int)lines.size() > maxLines) lines.pop_front();
}

void BattleFeed::clear() { lines.clear(); }

void BattleFeed::update(float dt) {
    for (auto& l : lines) l.age += dt;
    while (!lines.empty() && lines.front().age >= lines.front().lifetime) {
        lines.pop_front();
    }
}

void BattleFeed::render(int screenW, int screenH) {
    if (!text || lines.empty()) return;

    // Draw as 2D overlay: no depth; enable alpha blending
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);

    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint oldBlendSrc = 0, oldBlendDst = 0;
    if (!blendWasEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &oldBlendSrc);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &oldBlendDst);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    const float padX = 16.f, padY = 16.f;
    float x = padX, y = screenH - padY; // start from bottom

    for (int i = (int)lines.size() - 1; i >= 0; --i) {
        const auto& ln = lines[i];
        float t = std::clamp(ln.age / ln.lifetime, 0.f, 1.f);
        float alpha = (t < 0.75f) ? 1.f : std::max(0.f, 1.f - (t - 0.75f) / 0.25f);

        auto wrapped = wrap(ln.text, wrapWidth, baseScale);
        for (int w = (int)wrapped.size() - 1; w >= 0; --w) {
            y -= 18.f; // line height (font dependent; tweak if needed)
            text->renderText(
                wrapped[w],
                x, y,
                glm::vec3(ln.color) * alpha,
                baseScale
            );
        }
        y -= lineGap;
        if (y < 0) break;
    }

    // Restore GL state
    if (!blendWasEnabled) {
        glDisable(GL_BLEND);
    } else {
        glBlendFunc(oldBlendSrc, oldBlendDst);
    }
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
}

std::vector<std::string> BattleFeed::wrap(const std::string& s, float maxWidth, float scale) {
    std::vector<std::string> out;
    std::string cur, word;

    auto flush = [&](){
        if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    };

    for (size_t i = 0; i <= s.size(); ++i) {
        char c = (i < s.size() ? s[i] : ' ');
        if (c == ' ' || i == s.size()) {
            std::string candidate = cur.empty() ? word : (cur + " " + word);
            float w = text->measureTextWidth(candidate, scale);
            if (w > maxWidth && !cur.empty()) {
                flush();
                cur = word;
            } else {
                cur = candidate;
            }
            word.clear();
            if (i == s.size()) flush();
        } else {
            word.push_back(c);
        }
    }
    if (out.empty()) out.push_back(s);
    return out;
}