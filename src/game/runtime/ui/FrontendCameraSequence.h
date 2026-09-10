#pragma once

#include "game/runtime/ui/FrontendBackdrop.h"

#include <vector>

namespace game::runtime::ui_frontend {

// A bounded, pre-rendered camera move. Only its atlas pages and endpoint
// images are resident; the menu still has no scene, geometry or gameplay world.
struct CameraSequence {
    std::string atlasPrefix;
    std::string finalImage;
    int frameCount = 0;
    int columns = 4;
    int rows = 2;
    int frameWidth = 800;
    int frameHeight = 500;
    int padding = 2;

    bool valid() const {
        return !atlasPrefix.empty() && !finalImage.empty() && frameCount >= 2 && frameCount <= 128 &&
               columns >= 1 && columns <= 4 && rows >= 1 && rows <= 4 &&
               frameWidth >= 1 && frameWidth <= 2048 && frameHeight >= 1 && frameHeight <= 2048 &&
               padding >= 1 && padding <= 8 && (frameWidth + 2*padding)*columns <= 4096 &&
               (frameHeight + 2*padding)*rows <= 4096;
    }
    int pageCount() const { return valid() ? (frameCount + columns*rows - 1) / (columns*rows) : 0; }
    std::string pagePath(int page) const { return atlasPrefix + std::to_string(page) + ".png"; }
};

inline std::vector<IRenderBackend::DebugSprite> cameraSequenceSprites(
    const CameraSequence& sequence, const std::string& openingImage, float imageAspect,
    int width, int height, float progress) {
    if (!std::isfinite(progress)) progress = 0.0f;
    if (!sequence.valid() || progress <= 0.0f)
        return {backdropSprite(openingImage, imageAspect, width, height)};
    if (progress >= 1.0f)
        return {backdropSprite(sequence.finalImage, imageAspect, width, height)};

    const float position = progress * (sequence.frameCount - 1);
    const int frame = std::min(static_cast<int>(std::round(position)), sequence.frameCount-1);
    const auto frameSprite = [&](int index, float alpha) {
        const int perPage = sequence.columns * sequence.rows;
        auto sprite = backdropSprite(sequence.pagePath(index / perPage), imageAspect, width, height);
        const int tile = index % perPage;
        const float tileW = static_cast<float>(sequence.frameWidth + 2*sequence.padding);
        const float tileH = static_cast<float>(sequence.frameHeight + 2*sequence.padding);
        const float x = (tile % sequence.columns)*tileW + sequence.padding;
        const float y = (tile / sequence.columns)*tileH + sequence.padding;
        sprite.u0 = (x + sprite.u0*sequence.frameWidth) / (tileW*sequence.columns);
        sprite.u1 = (x + sprite.u1*sequence.frameWidth) / (tileW*sequence.columns);
        sprite.v0 = (y + sprite.v0*sequence.frameHeight) / (tileH*sequence.rows);
        sprite.v1 = (y + sprite.v1*sequence.frameHeight) / (tileH*sequence.rows);
        sprite.a = alpha;
        return sprite;
    };
    // The authored move supplies roughly 60 fps. Keep each sample sharp rather
    // than ghosting two different camera perspectives through a crossfade.
    return {frameSprite(frame, 1.0f)};
}

} // namespace game::runtime::ui_frontend
