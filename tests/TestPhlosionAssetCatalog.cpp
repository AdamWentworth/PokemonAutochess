#include "../tools/PhlosionAssetCatalog.h"
#include "../tools/PhlosionCookManifest.h"
#include "../tools/PhlosionForgeManifest.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

struct TempProject {
    fs::path root;

    TempProject() {
        root = fs::temp_directory_path() /
            ("pac-asset-catalog-test-" +
             std::to_string(
                 std::chrono::high_resolution_clock::now()
                     .time_since_epoch()
                     .count()));
        fs::create_directories(root);
    }

    ~TempProject() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

bool writeText(
    const fs::path& path,
    const std::string& text,
    std::string& outFail) {
    std::error_code errorCode;
    fs::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        outFail = "Could not create test fixture directory.";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output) {
        outFail = "Could not write test fixture " + path.string();
        return false;
    }
    return true;
}

bool writeJson(
    const fs::path& path,
    const json& document,
    std::string& outFail) {
    return writeText(path, document.dump(2) + "\n", outFail);
}

bool createFixture(TempProject& project, std::string& outFail) {
    const json config{
        {"active", {
            {"model", "0001_Active.phmodel"},
            {"modelVariants", {
                {"regular", "0001_Active.phmodel"}}}}}};
    const json recipe{
        {"schema", "fixture"},
        {"sourceGame", "fixture-game"},
        {"imports", json::array({{
            {"speciesId", 1},
            {"speciesName", "Active"},
            {"outputs", json::array({
                {{"appearance", "regular"},
                 {"stem", "0001_Active"}},
                {{"appearance", "regular"},
                 {"stem", "0002_Staged"}}})}}})}};
    const json catalog{
        {"schema_version", 1},
        {"kind", "pokemon_autochess_asset_catalog"},
        {"pokemon_config", "config/pokemon_config.json"},
        {"native_import_sets", json::array({{
            {"recipe", "tools/assets/fixture_recipe.json"},
            {"selection", "all_outputs"}}})},
        {"explicit_native_models", json::array()},
        {"authored_runtime_sources", json::array({{
            {"id", "vfx/fixture"},
            {"source", "assets/meshes/fixture.glb"},
            {"prefab_kind", "Object"},
            {"purpose", "fixture"},
            {"migration", "fixture"}}})},
        {"environment_resources", json::array({{
            {"id", "environment/fixture"},
            {"scene", "content/phlosion/scenes/fixture.phscene"},
            {"authored_scene", "scenes/fixture.scene.json"},
            {"cooked_object_root", "content/phlosion/objects/environment"}}})},
        {"retained_review_sources", json::array({{
            {"id", "legacy/fixture"},
            {"source", "assets/models/legacy.glb"},
            {"animset", "assets/models/legacy.animset.json"},
            {"disposition", "retain_until_replaced"},
            {"replacement_stems", json::array({"0001_Active"})},
            {"legacy_cooked_identities", json::array({"legacy"})}}})}};

    if (!writeJson(
            project.root / "config/pokemon_config.json",
            config,
            outFail) ||
        !writeJson(
            project.root / "tools/assets/fixture_recipe.json",
            recipe,
            outFail) ||
        !writeJson(
            project.root / "config/assets/asset_catalog.json",
            catalog,
            outFail)) {
        return false;
    }
    const std::string files[]{
        "assets/models/0001_Active.phmodel",
        "assets/models/0001_Active.animset.json",
        "assets/models/0002_Staged.phmodel",
        "assets/models/0002_Staged.animset.json",
        "assets/models/legacy.glb",
        "assets/models/legacy.animset.json",
        "assets/meshes/fixture.glb",
        "content/phlosion/scenes/fixture.phscene",
        "scenes/fixture.scene.json"};
    for (const std::string& path : files) {
        if (!writeText(project.root / path, "fixture", outFail)) {
            return false;
        }
    }
    fs::create_directories(
        project.root / "content/phlosion/objects/environment");
    return true;
}

std::string hashText(std::string_view value) {
    std::ostringstream formatted;
    formatted << std::hex << std::setfill('0') << std::setw(16)
              << engine::assets::phrc::contentHash64(
                     value.data(), value.size());
    return formatted.str();
}

json cookedEntry(
    const std::string& source,
    const std::string& object,
    std::string_view sourceContents,
    std::string_view objectContents) {
    return {
        {"source", source},
        {"object", object},
        {"source_fnv1a64", hashText(sourceContents)},
        {"object_fnv1a64", hashText(objectContents)},
        {"texture_dependencies", json::array()}};
}

} // namespace

bool test_phlosion_asset_catalog_contract(std::string& outFail) {
    TempProject project;
    if (!createFixture(project, outFail)) {
        return false;
    }

    tools::phlosion_asset_catalog::Catalog catalog;
    std::string error;
    if (!tools::phlosion_asset_catalog::load(
            project.root,
            "config/assets/asset_catalog.json",
            catalog,
            error) ||
        !tools::phlosion_asset_catalog::validateWorkspace(
            project.root,
            catalog,
            error)) {
        outFail = "Valid asset catalog was rejected: " + error;
        return false;
    }
    if (catalog.nativeModels.size() != 2u ||
        catalog.activeModelSources().size() != 1u ||
        catalog.stagedModelSources().size() != 1u ||
        catalog.authoredRuntimeSources.size() != 1u ||
        catalog.environmentResources.size() != 1u ||
        catalog.retainedReviewSources.size() != 1u) {
        outFail = "Asset catalog did not preserve ownership scopes.";
        return false;
    }

    if (!writeText(
            project.root / "assets/models/unowned.phmodel",
            "fixture",
            outFail)) {
        return false;
    }
    error.clear();
    if (tools::phlosion_asset_catalog::validateWorkspace(
            project.root,
            catalog,
            error) ||
        error.find("no asset-catalog owner") == std::string::npos) {
        outFail = "Catalog validation did not reject an unowned native model.";
        return false;
    }
    fs::remove(project.root / "assets/models/unowned.phmodel");

    const std::string activeSource =
        "assets/models/0001_Active.phmodel";
    const std::string stagedSource =
        "assets/models/0002_Staged.phmodel";
    const std::string auxiliarySource =
        "assets/meshes/fixture.glb";
    const std::string activeObjectPath =
        game::runtime::phlosion::objectPathForModel(activeSource);
    const std::string stagedObjectPath =
        game::runtime::phlosion::objectPathForModel(stagedSource);
    const std::string auxiliaryObjectPath =
        game::runtime::phlosion::objectPathForModel(auxiliarySource);
    const fs::path activeObject = project.root / activeObjectPath;
    const fs::path stagedObject = project.root / stagedObjectPath;
    const fs::path auxiliaryObject = project.root / auxiliaryObjectPath;
    if (!writeText(activeObject, "active", outFail) ||
        !writeText(stagedObject, "staged", outFail) ||
        !writeText(auxiliaryObject, "vfx", outFail)) {
        return false;
    }
    std::ifstream catalogInput(
        project.root / catalog.catalogPath,
        std::ios::binary);
    std::ostringstream catalogBuffer;
    catalogBuffer << catalogInput.rdbuf();
    if (!catalogInput) {
        outFail = "Could not read asset catalog fixture for hashing.";
        return false;
    }
    const json manifest{
        {"schema_version", 2},
        {"kind", "phlosion_cook_manifest"},
        {"asset_catalog", {
            {"source", catalog.catalogPath},
            {"source_fnv1a64",
                hashText(catalogBuffer.str())},
            {"native_model_count", 2},
            {"active_model_count", 1},
            {"staged_model_count", 1},
            {"authored_runtime_source_count", 1}}},
        {"pokemon", json::array({cookedEntry(
            activeSource,
            activeObjectPath,
            "fixture",
            "active")})},
        {"staged_imports", json::array({cookedEntry(
            stagedSource,
            stagedObjectPath,
            "fixture",
            "staged")})},
        {"runtime_auxiliary_objects", json::array({cookedEntry(
            auxiliarySource,
            auxiliaryObjectPath,
            "fixture",
            "vfx")})},
        {"shared_dependencies", json::array()},
        {"environment", {
            {"scene", "content/phlosion/scenes/fixture.phscene"},
            {"scene_fnv1a64", hashText("fixture")},
            {"authored_scene", "scenes/fixture.scene.json"},
            {"authored_scene_fnv1a64", hashText("fixture")}}},
        {"retained_review_sources", json::array({{
            {"id", "legacy/fixture"}}})}};
    error.clear();
    if (!tools::phlosion_cook_manifest::validate(
            project.root,
            catalog,
            manifest,
            error)) {
        outFail = "Valid cook manifest was rejected: " + error;
        return false;
    }

    json corrupted = manifest;
    corrupted["pokemon"][0]["source_fnv1a64"] = "0000000000000000";
    error.clear();
    if (tools::phlosion_cook_manifest::validate(
            project.root,
            catalog,
            corrupted,
            error) ||
        error.find("hash mismatch") == std::string::npos) {
        outFail = "Cook manifest validation accepted a stale source hash.";
        return false;
    }

    json incomplete = manifest;
    incomplete["staged_imports"] = json::array();
    error.clear();
    if (tools::phlosion_cook_manifest::validate(
            project.root,
            catalog,
            incomplete,
            error) ||
        error.find("missing catalog source") == std::string::npos) {
        outFail = "Cook manifest validation accepted an incomplete generation.";
        return false;
    }

    tools::phlosion_forge_manifest::PreparedCookManifest
        inconsistentTransaction;
    inconsistentTransaction.document = manifest;
    inconsistentTransaction.document["shared_dependencies"] =
        json::array({{{"asset_id", "dependencies/ktx2/unpublished.ktx2"}}});
    inconsistentTransaction.sharedDependencies = json::array();
    error.clear();
    if (tools::phlosion_forge_manifest::publishPrepared(
            catalog,
            inconsistentTransaction,
            error) ||
        error.find("transaction is inconsistent") == std::string::npos) {
        outFail =
            "Forge publication accepted a dependency set that differed from its manifest.";
        return false;
    }

    const fs::path manifestPath =
        project.root / "content/phlosion/cook_manifest.json";
    if (!writeJson(manifestPath, {{"generation", "old"}}, outFail)) {
        return false;
    }
    error.clear();
    if (!tools::phlosion_cook_manifest::publishAtomically(
            manifestPath,
            manifest,
            error)) {
        outFail = "Atomic manifest publication failed: " + error;
        return false;
    }
    json published;
    {
        std::ifstream input(manifestPath);
        input >> published;
    }
    if (published != manifest) {
        outFail = "Atomic manifest publication did not replace old content.";
        return false;
    }
    for (const auto& child : fs::directory_iterator(manifestPath.parent_path())) {
        const std::string name = child.path().filename().string();
        if (name.find("cook_manifest.json.partial.") == 0u ||
            name.find("cook_manifest.json.backup.") == 0u) {
            outFail = "Atomic manifest publication left a temporary file.";
            return false;
        }
    }
    return true;
}
