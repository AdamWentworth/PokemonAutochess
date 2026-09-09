#pragma once

#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "engine/assets/phlosion/PhlosionAuthoredScene.h"
#include "game/arena/ArenaMapData.h"

namespace game::runtime::authored_arena {

// A PHSC archive is the atomic runtime unit. Loose files are review/authoring
// mirrors and are never substituted for missing entries in a mounted arena.
struct Bundle {
    engine::assets::phlosion::SceneArchiveStore store;
    engine::assets::phlosion::AuthoredSceneDocument scene;
    game::arena::ArenaMapData map;
    std::string boardPath;
    bool load(const engine::IAssetStore &host, const std::string &path, std::string *error);
    bool loadBytes(const std::vector<std::uint8_t> &bytes, std::string *error);

  private:
    bool validate(std::string *error);
};

bool encode(const engine::IAssetStore &stagedInputs, const std::string &recipePath,
            std::vector<std::uint8_t> &bytes, std::string *error);

} // namespace game::runtime::authored_arena
