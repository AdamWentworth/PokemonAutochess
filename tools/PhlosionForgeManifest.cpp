#include "PhlosionForgeManifest.h"

#include "PhlosionCookManifest.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace tools::phlosion_forge_manifest {
namespace {

namespace fs = std::filesystem;

std::string hex64(std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (std::size_t index = 0u; index < out.size(); ++index) {
        const std::size_t reverseIndex = out.size() - 1u - index;
        out[reverseIndex] = kDigits[value & 0x0full];
        value >>= 4u;
    }
    return out;
}

bool readFile(
    const fs::path& path,
    std::vector<std::uint8_t>& out,
    std::string& outError) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        outError = "Could not open " + path.string();
        return false;
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        outError = "Could not measure " + path.string();
        return false;
    }
    out.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    }
    if (!input) {
        outError = "Could not read " + path.string();
        return false;
    }
    return true;
}

bool loadJson(
    const fs::path& path,
    nlohmann::json& out,
    std::string& outError) {
    std::ifstream input(path);
    if (!input) {
        outError = "Could not open JSON file " + path.string();
        return false;
    }
    try {
        input >> out;
        return true;
    } catch (const std::exception& exception) {
        outError =
            "Could not parse " + path.string() + ": " + exception.what();
        return false;
    }
}

bool assetCatalogManifestRecord(
    const phlosion_asset_catalog::Catalog& catalog,
    nlohmann::json& outRecord,
    std::string& outError) {
    std::vector<std::uint8_t> bytes;
    if (!readFile(catalog.catalogPath, bytes, outError)) {
        return false;
    }
    outRecord = {
        {"source", catalog.catalogPath},
        {"source_fnv1a64",
         hex64(engine::assets::phrc::contentHash64(bytes))},
        {"native_model_count", catalog.nativeModels.size()},
        {"active_model_count", catalog.activeModelSources().size()},
        {"staged_model_count", catalog.stagedModelSources().size()},
        {"authored_runtime_source_count",
         catalog.authoredRuntimeSources.size()},
        {"environment_count", catalog.environmentResources.size()},
        {"retained_review_source_count",
         catalog.retainedReviewSources.size()},
        {"recipe_count", catalog.recipePaths.size()}};
    return true;
}

nlohmann::json retainedReviewManifest(
    const phlosion_asset_catalog::Catalog& catalog) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& source : catalog.retainedReviewSources) {
        result.push_back({
            {"id", source.id},
            {"source", source.sourcePath},
            {"animset", source.animsetPath},
            {"disposition", source.disposition},
            {"replacement_stems", source.replacementStems},
            {"legacy_cooked_identities", source.legacyCookedIdentities}});
    }
    return result;
}

bool collectSharedDependencies(
    const nlohmann::json& pokemon,
    const nlohmann::json& stagedPokemon,
    const nlohmann::json& runtimeAuxiliaries,
    nlohmann::json& out,
    std::string& outError) {
    std::map<std::string, game::runtime::phlosion::ModelTextureDependency>
        dependencies;
    const auto collectSet = [&](const nlohmann::json& records) -> bool {
        for (const auto& record : records) {
            const std::string objectPath =
                record.at("object").get<std::string>();
            std::vector<game::runtime::phlosion::ModelTextureDependency>
                objectDependencies;
            if (!game::runtime::phlosion::listModelObjectTextureDependencies(
                    objectPath,
                    objectDependencies,
                    &outError)) {
                outError =
                    "Could not collect shared dependencies for " +
                    objectPath + ": " + outError;
                return false;
            }
            for (const auto& dependency : objectDependencies) {
                if (!std::string_view(dependency.assetId).starts_with(
                        "dependencies/ktx2/")) {
                    outError =
                        "Cooked object still uses a private texture dependency; "
                        "recook it before finalization: " + objectPath +
                        " -> " + dependency.assetId;
                    return false;
                }
                const auto [found, inserted] = dependencies.emplace(
                    dependency.assetId,
                    dependency);
                if (!inserted &&
                    (found->second.physicalPath != dependency.physicalPath ||
                     found->second.expectedContentHash !=
                         dependency.expectedContentHash ||
                     found->second.byteCount != dependency.byteCount)) {
                    outError =
                        "Shared texture dependency identity is inconsistent: " +
                        dependency.assetId;
                    return false;
                }
            }
        }
        return true;
    };
    if (!collectSet(pokemon) ||
        !collectSet(stagedPokemon) ||
        !collectSet(runtimeAuxiliaries)) {
        return false;
    }
    out = nlohmann::json::array();
    for (const auto& [assetId, dependency] : dependencies) {
        out.push_back({
            {"asset_id", assetId},
            {"path", dependency.physicalPath},
            {"fnv1a64", hex64(dependency.expectedContentHash)},
            {"bytes", dependency.byteCount}});
    }
    return true;
}

bool pruneSharedDependencyStore(
    const nlohmann::json& sharedDependencies,
    std::string& outError) {
    const fs::path storeRoot =
        fs::path(game::runtime::phlosion::kCookedRoot) /
        "dependencies" / "ktx2";
    std::set<fs::path> retained;
    for (const auto& dependency : sharedDependencies) {
        retained.insert(
            fs::path(dependency.at("path").get<std::string>())
                .lexically_normal());
    }
    std::error_code errorCode;
    if (!fs::is_directory(storeRoot, errorCode)) {
        if (!errorCode && retained.empty()) return true;
        outError =
            "Shared texture dependency store is missing: " +
            storeRoot.generic_string();
        return false;
    }
    std::uint64_t removedBytes = 0u;
    std::size_t removedFiles = 0u;
    for (fs::directory_iterator iterator(storeRoot, errorCode);
         !errorCode && iterator != fs::directory_iterator();
         iterator.increment(errorCode)) {
        if (!iterator->is_regular_file(errorCode) || errorCode) continue;
        const fs::path path = iterator->path().lexically_normal();
        const std::string fileName = path.filename().string();
        const bool interruptedPartial =
            fileName.find(".partial.") != std::string::npos;
        if (!interruptedPartial && retained.contains(path)) continue;
        const std::uint64_t bytes = static_cast<std::uint64_t>(
            iterator->file_size(errorCode));
        if (errorCode) break;
        fs::remove(path, errorCode);
        if (errorCode) break;
        removedBytes += bytes;
        ++removedFiles;
    }
    if (errorCode) {
        outError =
            "Could not prune shared texture dependency store: " +
            errorCode.message();
        return false;
    }
    std::cout
        << "[Phlosion Forge] Shared KTX2 store: "
        << retained.size() << " retained, " << removedFiles
        << " orphan/partial files removed, " << removedBytes
        << " bytes recovered.\n";
    return true;
}

} // namespace

bool prepareCookManifest(
    const phlosion_asset_catalog::Catalog& catalog,
    const nlohmann::json& route1,
    const nlohmann::json& pokemon,
    const nlohmann::json& stagedPokemon,
    const nlohmann::json& runtimeAuxiliaries,
    PreparedCookManifest& out,
    std::string& outError) {
    out = PreparedCookManifest{};
    nlohmann::json catalogRecord;
    if (!assetCatalogManifestRecord(catalog, catalogRecord, outError)) {
        return false;
    }
    if (!collectSharedDependencies(
            pokemon,
            stagedPokemon,
            runtimeAuxiliaries,
            out.sharedDependencies,
            outError)) {
        return false;
    }
    out.document = {
        {"schema_version", 2},
        {"kind", "phlosion_cook_manifest"},
        {"asset_catalog", std::move(catalogRecord)},
        {"environment", route1},
        {"pokemon", pokemon},
        {"staged_imports", stagedPokemon},
        {"shared_dependencies", out.sharedDependencies},
        {"retained_review_sources", retainedReviewManifest(catalog)},
        {"runtime_auxiliary_objects", runtimeAuxiliaries}};
    return true;
}

bool publishPrepared(
    const phlosion_asset_catalog::Catalog& catalog,
    const PreparedCookManifest& prepared,
    std::string& outError) {
    if (!prepared.document.contains("shared_dependencies") ||
        prepared.document.at("shared_dependencies") !=
            prepared.sharedDependencies) {
        outError =
            "Prepared cook manifest dependency transaction is inconsistent.";
        return false;
    }
    return phlosion_cook_manifest::validate(
               ".", catalog, prepared.document, outError) &&
        phlosion_cook_manifest::publishAtomically(
            kCookManifest, prepared.document, outError) &&
        pruneSharedDependencyStore(prepared.sharedDependencies, outError);
}

bool validateCurrent(
    const phlosion_asset_catalog::Catalog& catalog,
    std::string& outError) {
    nlohmann::json manifest;
    return loadJson(kCookManifest, manifest, outError) &&
        phlosion_cook_manifest::validate(".", catalog, manifest, outError);
}

} // namespace tools::phlosion_forge_manifest
