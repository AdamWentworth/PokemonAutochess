#pragma once

#include "game/runtime/ui/PokemonArtworkData.h"
#include "engine/render/IRenderBackend.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game::runtime::pokemon_artwork {

inline std::string speciesKey(std::string_view name) {
    std::string key;
    for (unsigned char c : name)
        if (c < 128 && std::isalnum(c)) key += static_cast<char>(std::tolower(c));
    if (name.find("\xE2\x99\x80") != std::string_view::npos) key += 'f';
    if (name.find("\xE2\x99\x82") != std::string_view::npos) key += 'm';
    return key;
}

inline const Entry *find(std::string_view species) {
    static const auto lookup = [] {
        std::unordered_map<std::string, const Entry *> map;
        for (const auto &entry : entries)
            map.emplace(speciesKey(entry.species), &entry);
        return map;
    }();
    const auto it = lookup.find(speciesKey(species));
    return it == lookup.end() ? nullptr : it->second;
}

inline std::string path(const Entry &entry) {
    const auto number = std::to_string(entry.dex);
    return "assets/ui/pokemon/tcg/" + std::string(3 - number.size(), '0') + number + ".jpg";
}

inline void appendPortrait(std::vector<IRenderBackend::DebugSprite> &sprites,
                           std::string_view species, float x, float y, float size) {
    const auto *entry = find(species);
    if (!entry) return;
    IRenderBackend::DebugSprite sprite;
    sprite.texturePath = path(*entry);
    sprite.x = x;
    sprite.y = y;
    sprite.w = sprite.h = size;
    sprite.u0 = static_cast<float>(entry->portrait.x) / entry->width;
    sprite.v0 = static_cast<float>(entry->portrait.y) / entry->height;
    sprite.u1 = static_cast<float>(entry->portrait.x + entry->portrait.w) / entry->width;
    sprite.v1 = static_cast<float>(entry->portrait.y + entry->portrait.h) / entry->height;
    sprites.push_back(std::move(sprite));
}

// Incoming UVs are relative to the illustration, not the whole physical card.
// Cover the frame opening at any aspect ratio without stretching the artwork
// or sampling card labels. The portrait and card share one cached original.
inline void applyCardCrop(IRenderBackend::DebugSprite &sprite, const Entry &entry) {
    float x = entry.art.x + sprite.u0 * entry.art.w;
    float y = entry.art.y + sprite.v0 * entry.art.h;
    float w = (sprite.u1 - sprite.u0) * entry.art.w;
    float h = (sprite.v1 - sprite.v0) * entry.art.h;
    if (sprite.w > 0 && sprite.h > 0 && w > 0 && h > 0) {
        const float aspect = sprite.w / sprite.h;
        if (w / h > aspect) {
            const float fitted = h * aspect;
            x += (w - fitted) * .5f;
            w = fitted;
        } else {
            const float fitted = w / aspect;
            y += (h - fitted) * .5f;
            h = fitted;
        }
    }
    sprite.u0 = x / entry.width;
    sprite.v0 = y / entry.height;
    sprite.u1 = (x + w) / entry.width;
    sprite.v1 = (y + h) / entry.height;
}

} // namespace game::runtime::pokemon_artwork
