#pragma once

#include "PhlosionAssetCatalog.h"

#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace tools::phlosion_cook_manifest {

bool validate(
    const std::filesystem::path& projectRoot,
    const phlosion_asset_catalog::Catalog& catalog,
    const nlohmann::json& manifest,
    std::string& outError);

bool publishAtomically(
    const std::filesystem::path& path,
    const nlohmann::json& document,
    std::string& outError);

} // namespace tools::phlosion_cook_manifest
