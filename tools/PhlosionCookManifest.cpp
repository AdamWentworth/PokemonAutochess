#include "PhlosionCookManifest.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace tools::phlosion_cook_manifest {

namespace {

namespace fs = std::filesystem;

bool fileHash(
    const fs::path& path,
    std::string& outHash,
    std::string& outError) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        outError = "Could not open manifest dependency " + path.string();
        return false;
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        outError = "Could not measure manifest dependency " + path.string();
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        outError = "Could not read manifest dependency " + path.string();
        return false;
    }
    std::ostringstream formatted;
    formatted << std::hex << std::setfill('0') << std::setw(16)
              << engine::assets::phrc::contentHash64(bytes);
    outHash = formatted.str();
    return true;
}

bool validateFileHash(
    const fs::path& projectRoot,
    const nlohmann::json& record,
    std::string_view pathField,
    std::string_view hashField,
    std::string& outError) {
    const auto path = record.find(std::string(pathField));
    const auto hash = record.find(std::string(hashField));
    if (path == record.end() || !path->is_string() ||
        hash == record.end() || !hash->is_string()) {
        outError =
            "Cook manifest integrity fields are missing: " +
            std::string(pathField) + "/" + std::string(hashField);
        return false;
    }
    std::string actual;
    if (!fileHash(
            projectRoot / fs::path(path->get<std::string>()),
            actual,
            outError)) {
        return false;
    }
    if (actual != hash->get<std::string>()) {
        outError =
            "Cook manifest hash mismatch for " + path->get<std::string>() +
            ": expected " + hash->get<std::string>() +
            ", observed " + actual;
        return false;
    }
    return true;
}

bool validateCookedSet(
    const fs::path& projectRoot,
    const nlohmann::json& manifest,
    std::string_view field,
    const std::vector<std::string>& expectedSources,
    std::string& outError) {
    const auto entries = manifest.find(std::string(field));
    if (entries == manifest.end() || !entries->is_array()) {
        outError =
            "Cook manifest field '" + std::string(field) +
            "' must be an array.";
        return false;
    }
    std::set<std::string> expected(
        expectedSources.begin(),
        expectedSources.end());
    std::set<std::string> observed;
    for (const auto& entry : *entries) {
        if (!entry.is_object() || !entry.contains("source") ||
            !entry["source"].is_string() ||
            !entry.contains("object") ||
            !entry["object"].is_string()) {
            outError =
                "Cook manifest field '" + std::string(field) +
                "' contains an invalid entry.";
            return false;
        }
        const std::string source = entry["source"].get<std::string>();
        const std::string object = entry["object"].get<std::string>();
        if (!observed.insert(source).second) {
            outError =
                "Cook manifest repeats source in '" +
                std::string(field) + "': " + source;
            return false;
        }
        std::error_code errorCode;
        if (!fs::is_regular_file(projectRoot / fs::path(source), errorCode) ||
            errorCode) {
            outError = "Cook manifest source is missing: " + source;
            return false;
        }
        if (!fs::is_regular_file(projectRoot / fs::path(object), errorCode) ||
            errorCode) {
            outError = "Cook manifest object is missing: " + object;
            return false;
        }
        const std::string expectedObject =
            game::runtime::phlosion::objectPathForModel(source);
        if (fs::path(object).lexically_normal() !=
            fs::path(expectedObject).lexically_normal()) {
            outError =
                "Cook manifest object identity does not match its source: " +
                source + " -> " + object + " (expected " +
                expectedObject + ")";
            return false;
        }
        if (!validateFileHash(
                projectRoot,
                entry,
                "source",
                "source_fnv1a64",
                outError) ||
            !validateFileHash(
                projectRoot,
                entry,
                "object",
                "object_fnv1a64",
                outError)) {
            return false;
        }
    }
    if (observed != expected) {
        for (const std::string& source : expected) {
            if (observed.find(source) == observed.end()) {
                outError =
                    "Cook manifest field '" + std::string(field) +
                    "' is missing catalog source: " + source;
                return false;
            }
        }
        for (const std::string& source : observed) {
            if (expected.find(source) == expected.end()) {
                outError =
                    "Cook manifest field '" + std::string(field) +
                    "' contains undeclared source: " + source;
                return false;
            }
        }
    }
    return true;
}

bool writeJsonFile(
    const fs::path& path,
    const nlohmann::json& document,
    std::string& outError) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        outError = "Could not create manifest candidate " + path.string();
        return false;
    }
    const std::string text = document.dump(2) + "\n";
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
        outError = "Could not write manifest candidate " + path.string();
        return false;
    }
    output.close();
    if (!output) {
        outError = "Could not close manifest candidate " + path.string();
        return false;
    }
    return true;
}

} // namespace

bool validate(
    const fs::path& projectRoot,
    const phlosion_asset_catalog::Catalog& catalog,
    const nlohmann::json& manifest,
    std::string& outError) {
    if (!manifest.is_object() ||
        manifest.value("kind", "") != "phlosion_cook_manifest" ||
        manifest.value("schema_version", 0) != 2) {
        outError = "Unsupported or invalid Phlosion cook manifest schema.";
        return false;
    }
    const auto catalogRecord = manifest.find("asset_catalog");
    if (catalogRecord == manifest.end() || !catalogRecord->is_object() ||
        catalogRecord->value("source", "") != catalog.catalogPath ||
        catalogRecord->value("native_model_count", 0u) !=
            catalog.nativeModels.size() ||
        catalogRecord->value("active_model_count", 0u) !=
            catalog.activeModelSources().size() ||
        catalogRecord->value("staged_model_count", 0u) !=
            catalog.stagedModelSources().size() ||
        catalogRecord->value("authored_runtime_source_count", 0u) !=
            catalog.authoredRuntimeSources.size()) {
        outError = "Cook manifest asset-catalog provenance is incomplete.";
        return false;
    }
    if (!validateFileHash(
            projectRoot,
            *catalogRecord,
            "source",
            "source_fnv1a64",
            outError)) {
        return false;
    }
    if (!validateCookedSet(
            projectRoot,
            manifest,
            "pokemon",
            catalog.activeModelSources(),
            outError) ||
        !validateCookedSet(
            projectRoot,
            manifest,
            "staged_imports",
            catalog.stagedModelSources(),
            outError) ||
        !validateCookedSet(
            projectRoot,
            manifest,
            "runtime_auxiliary_objects",
            catalog.authoredModelSources(),
            outError)) {
        return false;
    }
    const auto environment = manifest.find("environment");
    if (environment == manifest.end() || !environment->is_object() ||
        catalog.environmentResources.size() != 1u ||
        environment->value("scene", "") !=
            catalog.environmentResources.front().scenePath) {
        outError = "Cook manifest environment does not match the catalog.";
        return false;
    }
    std::error_code errorCode;
    if (!fs::is_regular_file(
            projectRoot / fs::path(
                catalog.environmentResources.front().scenePath),
            errorCode) || errorCode) {
        outError =
            "Cook manifest environment scene is missing: " +
            catalog.environmentResources.front().scenePath;
        return false;
    }
    if (environment->value("authored_scene", "") !=
            catalog.environmentResources.front().authoredScenePath ||
        !validateFileHash(
            projectRoot,
            *environment,
            "scene",
            "scene_fnv1a64",
            outError) ||
        !validateFileHash(
            projectRoot,
            *environment,
            "authored_scene",
            "authored_scene_fnv1a64",
            outError)) {
        if (outError.empty()) {
            outError =
                "Cook manifest authored environment does not match the "
                "catalog.";
        }
        return false;
    }
    const auto retained = manifest.find("retained_review_sources");
    if (retained == manifest.end() || !retained->is_array() ||
        retained->size() != catalog.retainedReviewSources.size()) {
        outError =
            "Cook manifest retained-review summary does not match the catalog.";
        return false;
    }
    std::set<std::string> expectedRetained;
    for (const auto& source : catalog.retainedReviewSources) {
        expectedRetained.insert(source.id);
    }
    std::set<std::string> observedRetained;
    for (const auto& entry : *retained) {
        if (!entry.is_object() || !entry.contains("id") ||
            !entry["id"].is_string()) {
            outError = "Cook manifest retained-review entry is invalid.";
            return false;
        }
        observedRetained.insert(entry["id"].get<std::string>());
    }
    if (expectedRetained != observedRetained) {
        outError =
            "Cook manifest retained-review identities do not match the catalog.";
        return false;
    }
    return true;
}

bool publishAtomically(
    const fs::path& path,
    const nlohmann::json& document,
    std::string& outError) {
    std::error_code errorCode;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), errorCode);
        if (errorCode) {
            outError =
                "Could not create cook-manifest directory: " +
                errorCode.message();
            return false;
        }
    }
    const auto nonce = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();
    const fs::path partial =
        path.string() + ".partial." + std::to_string(nonce);
    const fs::path backup =
        path.string() + ".backup." + std::to_string(nonce);
    const auto cleanup = [&]() {
        std::error_code ignored;
        fs::remove(partial, ignored);
    };
    if (!writeJsonFile(partial, document, outError)) {
        cleanup();
        return false;
    }
    nlohmann::json verification;
    {
        std::ifstream input(partial);
        try {
            input >> verification;
        } catch (const std::exception& exception) {
            outError =
                "Could not verify cook-manifest candidate: " +
                std::string(exception.what());
            cleanup();
            return false;
        }
    }
    if (verification != document) {
        outError = "Cook-manifest candidate changed during serialization.";
        cleanup();
        return false;
    }

    const bool hadExisting = fs::is_regular_file(path, errorCode) && !errorCode;
    if (errorCode) {
        outError =
            "Could not inspect existing cook manifest: " +
            errorCode.message();
        cleanup();
        return false;
    }
    if (hadExisting) {
        fs::rename(path, backup, errorCode);
        if (errorCode) {
            outError =
                "Could not preserve existing cook manifest: " +
                errorCode.message();
            cleanup();
            return false;
        }
    }
    fs::rename(partial, path, errorCode);
    if (errorCode) {
        const std::string publishFailure = errorCode.message();
        if (hadExisting) {
            std::error_code restoreError;
            fs::rename(backup, path, restoreError);
            if (restoreError) {
                outError =
                    "Could not publish cook manifest (" + publishFailure +
                    ") or restore its backup (" + restoreError.message() +
                    "). Backup remains at " + backup.string();
                cleanup();
                return false;
            }
        }
        outError = "Could not publish cook manifest: " + publishFailure;
        cleanup();
        return false;
    }
    if (hadExisting) {
        std::error_code ignored;
        fs::remove(backup, ignored);
    }
    return true;
}

} // namespace tools::phlosion_cook_manifest
