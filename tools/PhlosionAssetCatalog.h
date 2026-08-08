#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tools::phlosion_asset_catalog {

enum class NativeModelScope {
    ActiveGameplay,
    StagedImport,
};

struct NativeModel {
    std::string stem;
    std::string sourcePath;
    std::string animsetPath;
    std::string owner;
    std::string sourceGame;
    std::string purpose;
    NativeModelScope scope = NativeModelScope::StagedImport;
};

struct AuthoredRuntimeSource {
    std::string id;
    std::string sourcePath;
    std::string prefabKind;
    std::string purpose;
    std::string migration;
};

struct EnvironmentResource {
    std::string id;
    std::string scenePath;
    std::string authoredScenePath;
    std::string cookedObjectRoot;
};

struct RetainedReviewSource {
    std::string id;
    std::string sourcePath;
    std::string animsetPath;
    std::string disposition;
    std::vector<std::string> replacementStems;
    std::vector<std::string> legacyCookedIdentities;
};

struct Catalog {
    std::string catalogPath;
    std::string pokemonConfigPath;
    std::vector<std::string> recipePaths;
    std::vector<NativeModel> nativeModels;
    std::vector<AuthoredRuntimeSource> authoredRuntimeSources;
    std::vector<EnvironmentResource> environmentResources;
    std::vector<RetainedReviewSource> retainedReviewSources;

    std::vector<std::string> activeModelSources() const;
    std::vector<std::string> stagedModelSources() const;
    std::vector<std::string> authoredModelSources() const;
};

bool load(
    const std::filesystem::path& projectRoot,
    const std::string& catalogPath,
    Catalog& outCatalog,
    std::string& outError);

bool validateWorkspace(
    const std::filesystem::path& projectRoot,
    const Catalog& catalog,
    std::string& outError);

} // namespace tools::phlosion_asset_catalog
