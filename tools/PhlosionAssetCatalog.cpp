#include "PhlosionAssetCatalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace tools::phlosion_asset_catalog {

namespace {

namespace fs = std::filesystem;

bool loadJson(
    const fs::path& path,
    nlohmann::json& outDocument,
    std::string& outError) {
    std::ifstream input(path);
    if (!input) {
        outError = "Could not open asset-catalog input " + path.string();
        return false;
    }
    try {
        input >> outDocument;
        return true;
    } catch (const std::exception& exception) {
        outError =
            "Could not parse asset-catalog input " + path.string() +
            ": " + exception.what();
        return false;
    }
}

bool normalizeRelativePath(
    std::string_view raw,
    std::string& outPath,
    std::string& outError) {
    if (raw.empty()) {
        outError = "Asset catalog contains an empty project path.";
        return false;
    }
    const fs::path input{std::string(raw)};
    if (input.is_absolute() || input.has_root_name() ||
        input.has_root_directory()) {
        outError =
            "Asset catalog path must be project-relative: " +
            std::string(raw);
        return false;
    }
    const fs::path normalized = input.lexically_normal();
    if (normalized.empty() || normalized == "." ||
        (!normalized.empty() && *normalized.begin() == "..")) {
        outError =
            "Asset catalog path escapes the project root: " +
            std::string(raw);
        return false;
    }
    outPath = normalized.generic_string();
    return true;
}

bool requiredString(
    const nlohmann::json& object,
    std::string_view field,
    std::string& outValue,
    std::string& outError) {
    const auto found = object.find(std::string(field));
    if (found == object.end() || !found->is_string() ||
        found->get_ref<const std::string&>().empty()) {
        outError =
            "Asset catalog field '" + std::string(field) +
            "' must be a non-empty string.";
        return false;
    }
    outValue = found->get<std::string>();
    return true;
}

bool requiredRelativePath(
    const nlohmann::json& object,
    std::string_view field,
    std::string& outValue,
    std::string& outError) {
    std::string raw;
    return requiredString(object, field, raw, outError) &&
        normalizeRelativePath(raw, outValue, outError);
}

bool optionalString(
    const nlohmann::json& object,
    std::string_view field,
    std::string& outValue,
    std::string& outError) {
    const auto found = object.find(std::string(field));
    if (found == object.end()) {
        outValue.clear();
        return true;
    }
    if (!found->is_string()) {
        outError =
            "Asset catalog field '" + std::string(field) +
            "' must be a string when present.";
        return false;
    }
    outValue = found->get<std::string>();
    return true;
}

bool addUnique(
    std::set<std::string>& values,
    const std::string& value,
    std::string_view description,
    std::string& outError) {
    if (!values.insert(value).second) {
        outError =
            "Asset catalog contains duplicate " +
            std::string(description) + ": " + value;
        return false;
    }
    return true;
}

bool readActiveModels(
    const fs::path& projectRoot,
    const std::string& configPath,
    std::set<std::string>& outSources,
    std::string& outError) {
    nlohmann::json config;
    if (!loadJson(projectRoot / fs::path(configPath), config, outError)) {
        return false;
    }
    if (!config.is_object() || config.empty()) {
        outError = "Pokemon configuration must be a non-empty object.";
        return false;
    }
    const auto addModel = [&](
        const nlohmann::json& value,
        const std::string& context) -> bool {
        if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
            outError = context + " must name a model string.";
            return false;
        }
        std::string path;
        if (!normalizeRelativePath(
                (fs::path("assets/models") /
                    value.get<std::string>()).generic_string(),
                path,
                outError)) {
            return false;
        }
        if (fs::path(path).extension() != ".phmodel") {
            outError =
                context + " must use native .phmodel source: " + path;
            return false;
        }
        outSources.insert(std::move(path));
        return true;
    };
    for (const auto& [pokemonId, record] : config.items()) {
        if (!record.is_object()) {
            outError =
                "Pokemon configuration entry must be an object: " +
                pokemonId;
            return false;
        }
        const auto model = record.find("model");
        if (model == record.end() ||
            !addModel(*model, pokemonId + ".model")) {
            if (model == record.end()) {
                outError =
                    "Pokemon configuration is missing model: " + pokemonId;
            }
            return false;
        }
        const auto variants = record.find("modelVariants");
        if (variants == record.end()) {
            continue;
        }
        if (!variants->is_object()) {
            outError =
                "Pokemon modelVariants must be an object: " + pokemonId;
            return false;
        }
        for (const auto& [variant, value] : variants->items()) {
            if (!addModel(
                    value,
                    pokemonId + ".modelVariants." + variant)) {
                return false;
            }
        }
    }
    return !outSources.empty();
}

bool addNativeModel(
    NativeModel model,
    std::map<std::string, NativeModel>& models,
    std::set<std::string>& sourcePaths,
    std::string& outError) {
    if (model.stem.empty()) {
        outError = "Asset catalog native model has an empty stem.";
        return false;
    }
    if (!models.emplace(model.stem, model).second) {
        outError =
            "Asset catalog contains duplicate native stem: " + model.stem;
        return false;
    }
    if (!addUnique(
            sourcePaths,
            model.sourcePath,
            "native source path",
            outError)) {
        return false;
    }
    return true;
}

bool parseRecipeSet(
    const fs::path& projectRoot,
    const nlohmann::json& definition,
    std::map<std::string, NativeModel>& models,
    std::set<std::string>& sourcePaths,
    std::vector<std::string>& recipePaths,
    std::string& outError) {
    std::string recipePath;
    std::string selection;
    if (!requiredRelativePath(
            definition, "recipe", recipePath, outError) ||
        !requiredString(
            definition, "selection", selection, outError)) {
        return false;
    }
    if (std::find(
            recipePaths.begin(),
            recipePaths.end(),
            recipePath) != recipePaths.end()) {
        outError = "Asset catalog repeats recipe: " + recipePath;
        return false;
    }

    nlohmann::json recipe;
    if (!loadJson(projectRoot / fs::path(recipePath), recipe, outError)) {
        return false;
    }
    std::string sourceGame;
    if (!requiredString(recipe, "sourceGame", sourceGame, outError)) {
        return false;
    }
    const auto imports = recipe.find("imports");
    if (imports == recipe.end() || !imports->is_array()) {
        outError = "Import recipe has no imports array: " + recipePath;
        return false;
    }
    std::map<std::string, NativeModel> recipeModels;
    for (const auto& import : *imports) {
        const auto outputs = import.find("outputs");
        if (outputs == import.end() || !outputs->is_array()) {
            outError = "Import recipe entry has no outputs: " + recipePath;
            return false;
        }
        for (const auto& output : *outputs) {
            std::string stem;
            if (!requiredString(output, "stem", stem, outError)) {
                return false;
            }
            NativeModel model;
            model.stem = stem;
            model.sourcePath =
                (fs::path("assets/models") /
                    (stem + ".phmodel")).generic_string();
            model.animsetPath =
                (fs::path("assets/models") /
                    (stem + ".animset.json")).generic_string();
            model.owner = recipePath;
            model.sourceGame = sourceGame;
            if (!recipeModels.emplace(stem, std::move(model)).second) {
                outError =
                    "Import recipe contains duplicate output stem: " + stem;
                return false;
            }
        }
    }

    std::set<std::string> selectedStems;
    if (selection == "all_outputs") {
        for (const auto& [stem, model] : recipeModels) {
            (void)model;
            selectedStems.insert(stem);
        }
    } else if (selection == "include_stems") {
        const auto stems = definition.find("stems");
        if (stems == definition.end() || !stems->is_array() ||
            stems->empty()) {
            outError =
                "include_stems recipe selection must provide stems: " +
                recipePath;
            return false;
        }
        for (const auto& value : *stems) {
            if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
                outError =
                    "Recipe selection stem must be a non-empty string: " +
                    recipePath;
                return false;
            }
            const std::string stem = value.get<std::string>();
            if (!selectedStems.insert(stem).second) {
                outError =
                    "Recipe selection repeats stem: " + stem;
                return false;
            }
            if (recipeModels.find(stem) == recipeModels.end()) {
                outError =
                    "Recipe selection names an undeclared stem: " + stem;
                return false;
            }
        }
    } else {
        outError =
            "Unsupported recipe selection '" + selection +
            "' for " + recipePath;
        return false;
    }

    for (const std::string& stem : selectedStems) {
        if (!addNativeModel(
                recipeModels.at(stem),
                models,
                sourcePaths,
                outError)) {
            return false;
        }
    }
    recipePaths.push_back(std::move(recipePath));
    return true;
}

bool requireRegularFile(
    const fs::path& projectRoot,
    const std::string& relativePath,
    std::string_view description,
    std::string& outError) {
    std::error_code errorCode;
    if (!fs::is_regular_file(
            projectRoot / fs::path(relativePath),
            errorCode) || errorCode) {
        outError =
            std::string(description) + " is missing: " + relativePath;
        return false;
    }
    return true;
}

bool requireDirectory(
    const fs::path& projectRoot,
    const std::string& relativePath,
    std::string_view description,
    std::string& outError) {
    std::error_code errorCode;
    if (!fs::is_directory(
            projectRoot / fs::path(relativePath),
            errorCode) || errorCode) {
        outError =
            std::string(description) + " is missing: " + relativePath;
        return false;
    }
    return true;
}

bool validateOwnedFiles(
    const fs::path& projectRoot,
    const Catalog& catalog,
    std::string& outError) {
    std::set<std::string> nativeSources;
    std::set<std::string> animsets;
    std::set<std::string> glbSources;
    for (const NativeModel& model : catalog.nativeModels) {
        nativeSources.insert(model.sourcePath);
        animsets.insert(model.animsetPath);
    }
    for (const AuthoredRuntimeSource& source :
         catalog.authoredRuntimeSources) {
        glbSources.insert(source.sourcePath);
    }
    for (const RetainedReviewSource& source :
         catalog.retainedReviewSources) {
        glbSources.insert(source.sourcePath);
        animsets.insert(source.animsetPath);
    }

    const auto validateDirectory = [&](
        const fs::path& relativeRoot,
        bool recursive) -> bool {
        const fs::path fullRoot = projectRoot / relativeRoot;
        std::error_code errorCode;
        if (!fs::is_directory(fullRoot, errorCode) || errorCode) {
            outError =
                "Catalogued asset directory is missing: " +
                relativeRoot.generic_string();
            return false;
        }
        const auto validateFile = [&](const fs::path& fullPath) -> bool {
            const std::string relative =
                fs::relative(fullPath, projectRoot).generic_string();
            const std::string filename = fullPath.filename().string();
            if (fullPath.extension() == ".phmodel" &&
                nativeSources.find(relative) == nativeSources.end()) {
                outError =
                    "Native model has no asset-catalog owner: " + relative;
                return false;
            }
            if (filename.size() >= 13u &&
                filename.ends_with(".animset.json") &&
                animsets.find(relative) == animsets.end()) {
                outError =
                    "Animation set has no asset-catalog owner: " + relative;
                return false;
            }
            if (fullPath.extension() == ".glb" &&
                glbSources.find(relative) == glbSources.end()) {
                outError =
                    "GLB has no asset-catalog owner: " + relative;
                return false;
            }
            return true;
        };
        if (recursive) {
            for (fs::recursive_directory_iterator iterator(fullRoot, errorCode);
                 !errorCode && iterator != fs::recursive_directory_iterator();
                 iterator.increment(errorCode)) {
                if (iterator->is_regular_file(errorCode) && !errorCode &&
                    !validateFile(iterator->path())) {
                    return false;
                }
            }
        } else {
            for (fs::directory_iterator iterator(fullRoot, errorCode);
                 !errorCode && iterator != fs::directory_iterator();
                 iterator.increment(errorCode)) {
                if (iterator->is_regular_file(errorCode) && !errorCode &&
                    !validateFile(iterator->path())) {
                    return false;
                }
            }
        }
        if (errorCode) {
            outError =
                "Could not enumerate catalogued asset directory " +
                relativeRoot.generic_string() + ": " + errorCode.message();
            return false;
        }
        return true;
    };

    return validateDirectory("assets/models", false) &&
        validateDirectory("assets/meshes", true);
}

} // namespace

std::vector<std::string> Catalog::activeModelSources() const {
    std::vector<std::string> result;
    for (const NativeModel& model : nativeModels) {
        if (model.scope == NativeModelScope::ActiveGameplay) {
            result.push_back(model.sourcePath);
        }
    }
    return result;
}

std::vector<std::string> Catalog::stagedModelSources() const {
    std::vector<std::string> result;
    for (const NativeModel& model : nativeModels) {
        if (model.scope == NativeModelScope::StagedImport) {
            result.push_back(model.sourcePath);
        }
    }
    return result;
}

std::vector<std::string> Catalog::authoredModelSources() const {
    std::vector<std::string> result;
    result.reserve(authoredRuntimeSources.size());
    for (const AuthoredRuntimeSource& source : authoredRuntimeSources) {
        result.push_back(source.sourcePath);
    }
    return result;
}

bool load(
    const fs::path& projectRoot,
    const std::string& catalogPath,
    Catalog& outCatalog,
    std::string& outError) {
    std::string normalizedCatalogPath;
    if (!normalizeRelativePath(
            catalogPath,
            normalizedCatalogPath,
            outError)) {
        return false;
    }
    nlohmann::json document;
    if (!loadJson(
            projectRoot / fs::path(normalizedCatalogPath),
            document,
            outError)) {
        return false;
    }
    if (!document.is_object() ||
        document.value("kind", "") !=
            "pokemon_autochess_asset_catalog" ||
        document.value("schema_version", 0) != 1) {
        outError = "Unsupported Pokemon Autochess asset catalog schema.";
        return false;
    }

    Catalog catalog;
    catalog.catalogPath = normalizedCatalogPath;
    if (!requiredRelativePath(
            document,
            "pokemon_config",
            catalog.pokemonConfigPath,
            outError)) {
        return false;
    }

    std::set<std::string> activeSources;
    if (!readActiveModels(
            projectRoot,
            catalog.pokemonConfigPath,
            activeSources,
            outError)) {
        return false;
    }

    const auto recipeSets = document.find("native_import_sets");
    if (recipeSets == document.end() || !recipeSets->is_array()) {
        outError = "Asset catalog must define native_import_sets.";
        return false;
    }
    std::map<std::string, NativeModel> models;
    std::set<std::string> nativeSourcePaths;
    for (const auto& recipeSet : *recipeSets) {
        if (!recipeSet.is_object() ||
            !parseRecipeSet(
                projectRoot,
                recipeSet,
                models,
                nativeSourcePaths,
                catalog.recipePaths,
                outError)) {
            return false;
        }
    }

    const auto explicitModels = document.find("explicit_native_models");
    if (explicitModels == document.end() || !explicitModels->is_array()) {
        outError = "Asset catalog must define explicit_native_models.";
        return false;
    }
    for (const auto& definition : *explicitModels) {
        NativeModel model;
        std::string scope;
        if (!requiredString(definition, "stem", model.stem, outError) ||
            !requiredRelativePath(
                definition, "source", model.sourcePath, outError) ||
            !requiredRelativePath(
                definition, "animset", model.animsetPath, outError) ||
            !requiredString(definition, "scope", scope, outError) ||
            !optionalString(
                definition, "purpose", model.purpose, outError)) {
            return false;
        }
        if (scope != "staged_import") {
            outError =
                "Explicit native model scope must be staged_import: " +
                model.stem;
            return false;
        }
        model.owner = catalog.catalogPath;
        if (!addNativeModel(
                std::move(model),
                models,
                nativeSourcePaths,
                outError)) {
            return false;
        }
    }

    for (auto& [stem, model] : models) {
        (void)stem;
        if (activeSources.find(model.sourcePath) != activeSources.end()) {
            model.scope = NativeModelScope::ActiveGameplay;
        }
    }
    for (const std::string& activeSource : activeSources) {
        if (nativeSourcePaths.find(activeSource) == nativeSourcePaths.end()) {
            outError =
                "Active Pokemon model has no selected catalog import: " +
                activeSource;
            return false;
        }
    }
    for (auto& [stem, model] : models) {
        (void)stem;
        catalog.nativeModels.push_back(std::move(model));
    }

    const auto authored = document.find("authored_runtime_sources");
    if (authored == document.end() || !authored->is_array()) {
        outError = "Asset catalog must define authored_runtime_sources.";
        return false;
    }
    std::set<std::string> authoredIds;
    std::set<std::string> authoredPaths;
    for (const auto& definition : *authored) {
        AuthoredRuntimeSource source;
        if (!requiredString(definition, "id", source.id, outError) ||
            !requiredRelativePath(
                definition, "source", source.sourcePath, outError) ||
            !requiredString(
                definition, "prefab_kind", source.prefabKind, outError) ||
            !requiredString(
                definition, "purpose", source.purpose, outError) ||
            !requiredString(
                definition, "migration", source.migration, outError) ||
            !addUnique(
                authoredIds, source.id, "authored source id", outError) ||
            !addUnique(
                authoredPaths,
                source.sourcePath,
                "authored source path",
                outError)) {
            return false;
        }
        catalog.authoredRuntimeSources.push_back(std::move(source));
    }

    const auto environments = document.find("environment_resources");
    if (environments == document.end() || !environments->is_array()) {
        outError = "Asset catalog must define environment_resources.";
        return false;
    }
    std::set<std::string> environmentIds;
    for (const auto& definition : *environments) {
        EnvironmentResource resource;
        if (!requiredString(definition, "id", resource.id, outError) ||
            !requiredRelativePath(
                definition, "scene", resource.scenePath, outError) ||
            !requiredRelativePath(
                definition,
                "authored_scene",
                resource.authoredScenePath,
                outError) ||
            !requiredRelativePath(
                definition,
                "cooked_object_root",
                resource.cookedObjectRoot,
                outError) ||
            !addUnique(
                environmentIds,
                resource.id,
                "environment id",
                outError)) {
            return false;
        }
        catalog.environmentResources.push_back(std::move(resource));
    }

    const auto retained = document.find("retained_review_sources");
    if (retained == document.end() || !retained->is_array()) {
        outError = "Asset catalog must define retained_review_sources.";
        return false;
    }
    std::set<std::string> retainedIds;
    std::set<std::string> retainedPaths;
    for (const auto& definition : *retained) {
        RetainedReviewSource source;
        if (!requiredString(definition, "id", source.id, outError) ||
            !requiredRelativePath(
                definition, "source", source.sourcePath, outError) ||
            !requiredRelativePath(
                definition, "animset", source.animsetPath, outError) ||
            !requiredString(
                definition,
                "disposition",
                source.disposition,
                outError) ||
            !addUnique(
                retainedIds, source.id, "retained source id", outError) ||
            !addUnique(
                retainedPaths,
                source.sourcePath,
                "retained source path",
                outError)) {
            return false;
        }
        if (authoredPaths.find(source.sourcePath) != authoredPaths.end()) {
            outError =
                "GLB cannot be both authored runtime and retained review: " +
                source.sourcePath;
            return false;
        }
        const auto replacements = definition.find("replacement_stems");
        if (replacements != definition.end()) {
            if (!replacements->is_array()) {
                outError = "replacement_stems must be an array.";
                return false;
            }
            for (const auto& replacement : *replacements) {
                if (!replacement.is_string() ||
                    replacement.get_ref<const std::string&>().empty()) {
                    outError = "replacement_stems contains an invalid value.";
                    return false;
                }
                source.replacementStems.push_back(
                    replacement.get<std::string>());
            }
        }
        const auto cooked = definition.find("legacy_cooked_identities");
        if (cooked != definition.end()) {
            if (!cooked->is_array()) {
                outError = "legacy_cooked_identities must be an array.";
                return false;
            }
            for (const auto& identity : *cooked) {
                if (!identity.is_string() ||
                    identity.get_ref<const std::string&>().empty()) {
                    outError =
                        "legacy_cooked_identities contains an invalid value.";
                    return false;
                }
                source.legacyCookedIdentities.push_back(
                    identity.get<std::string>());
            }
        }
        catalog.retainedReviewSources.push_back(std::move(source));
    }

    outCatalog = std::move(catalog);
    return true;
}

bool validateWorkspace(
    const fs::path& projectRoot,
    const Catalog& catalog,
    std::string& outError) {
    if (!requireRegularFile(
            projectRoot,
            catalog.catalogPath,
            "Asset catalog",
            outError) ||
        !requireRegularFile(
            projectRoot,
            catalog.pokemonConfigPath,
            "Pokemon configuration",
            outError)) {
        return false;
    }
    for (const std::string& recipePath : catalog.recipePaths) {
        if (!requireRegularFile(
                projectRoot,
                recipePath,
                "Import recipe",
                outError)) {
            return false;
        }
    }
    std::set<std::string> nativeStems;
    for (const NativeModel& model : catalog.nativeModels) {
        nativeStems.insert(model.stem);
        if (!requireRegularFile(
                projectRoot,
                model.sourcePath,
                "Native model",
                outError) ||
            !requireRegularFile(
                projectRoot,
                model.animsetPath,
                "Native animation set",
                outError)) {
            return false;
        }
    }
    for (const AuthoredRuntimeSource& source :
         catalog.authoredRuntimeSources) {
        if (!requireRegularFile(
                projectRoot,
                source.sourcePath,
                "Authored runtime source",
                outError)) {
            return false;
        }
    }
    for (const EnvironmentResource& resource :
         catalog.environmentResources) {
        if (!requireRegularFile(
                projectRoot,
                resource.scenePath,
                "Cooked environment scene",
                outError) ||
            !requireRegularFile(
                projectRoot,
                resource.authoredScenePath,
                "Authored environment scene",
                outError) ||
            !requireDirectory(
                projectRoot,
                resource.cookedObjectRoot,
                "Cooked environment object root",
                outError)) {
            return false;
        }
    }
    for (const RetainedReviewSource& source :
         catalog.retainedReviewSources) {
        if (!requireRegularFile(
                projectRoot,
                source.sourcePath,
                "Retained review source",
                outError) ||
            !requireRegularFile(
                projectRoot,
                source.animsetPath,
                "Retained review animation set",
                outError)) {
            return false;
        }
        for (const std::string& replacement : source.replacementStems) {
            if (nativeStems.find(replacement) == nativeStems.end()) {
                outError =
                    "Retained source replacement is not catalogued: " +
                    replacement;
                return false;
            }
        }
    }
    return validateOwnedFiles(projectRoot, catalog, outError);
}

} // namespace tools::phlosion_asset_catalog
