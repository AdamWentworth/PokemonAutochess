#pragma once

#include "game/runtime/ui/PokemonPortraitData.h"
#include "engine/render/IRenderBackend.h"
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::runtime::pokemon_portraits {

inline std::string speciesKey(std::string_view name) {
    std::string key;
    for (unsigned char c : name)
        if (c < 128 && std::isalnum(c)) key += static_cast<char>(std::tolower(c));
    // Accept both config identities (nidoran-f/m) and display names.
    if (name.find("\xE2\x99\x80") != std::string_view::npos) key += 'f';
    if (name.find("\xE2\x99\x82") != std::string_view::npos) key += 'm';
    return key;
}

inline const Entry *find(std::string_view species) {
    static const auto lookup = [] {
        std::unordered_map<std::string, const Entry *> map;
        for (const auto &entry : entries) map.emplace(speciesKey(entry.species), &entry);
        return map;
    }();
    const auto it = lookup.find(speciesKey(species));
    return it == lookup.end() ? nullptr : it->second;
}

inline std::string path(const Entry &entry) {
    const auto number = std::to_string(entry.dex);
    return "assets/ui/pokemon/home/" + std::string(3 - number.size(), '0') + number + ".png";
}

inline void append(std::vector<IRenderBackend::DebugSprite> &sprites,
                   std::string_view species, float x, float y, float size) {
    const auto *entry = find(species);
    if (!entry) return;
    IRenderBackend::DebugSprite sprite;
    sprite.texturePath = path(*entry);
    sprite.x = x;
    sprite.y = y;
    sprite.w = sprite.h = size;
    sprite.u0 = entry->cropX / 512.0f;
    sprite.v0 = entry->cropY / 512.0f;
    sprite.u1 = (entry->cropX + entry->cropSize) / 512.0f;
    sprite.v1 = (entry->cropY + entry->cropSize) / 512.0f;
    sprites.push_back(std::move(sprite));
}

} // namespace game::runtime::pokemon_portraits
