#pragma once

#include "PhlosionAssetCatalog.h"

#include <nlohmann/json.hpp>

#include <string>

namespace tools::phlosion_forge_manifest {

inline constexpr char kCookManifest[] =
    "content/phlosion/cook_manifest.json";

struct PreparedCookManifest {
    // Kept together so publication can validate and prune exactly the shared
    // dependency set represented by this immutable manifest document.
    nlohmann::json document;
    nlohmann::json sharedDependencies;
};

bool prepareCookManifest(
    const phlosion_asset_catalog::Catalog& catalog,
    const nlohmann::json& route1,
    const nlohmann::json& pokemon,
    const nlohmann::json& stagedPokemon,
    const nlohmann::json& runtimeAuxiliaries,
    PreparedCookManifest& out,
    std::string& outError);

bool publishPrepared(
    const phlosion_asset_catalog::Catalog& catalog,
    const PreparedCookManifest& prepared,
    std::string& outError);

bool validateCurrent(
    const phlosion_asset_catalog::Catalog& catalog,
    std::string& outError);

} // namespace tools::phlosion_forge_manifest
