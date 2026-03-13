#include "game/runtime/backend_ui/BackendImagePath.h"

#include <string>

bool test_backend_image_path_contract(std::string& outFail) {
    using game::runtime::backend_images::normalizeNameForImage;
    using game::runtime::backend_images::resolvePokemonPortraitPath;

    if (normalizeNameForImage("Nidoran F") != "nidoran_f") {
        outFail = "normalizeNameForImage should lowercase and underscore spaces";
        return false;
    }
    if (normalizeNameForImage("Mr. Mime") != "mr_mime") {
        outFail = "normalizeNameForImage should strip punctuation";
        return false;
    }

    {
        const std::string explicitPath = resolvePokemonPortraitPath(
            "assets/images/charmander.png",
            "pikachu",
            "assets/images/item_placeholder.png");
        if (explicitPath != "assets/images/charmander.png") {
            outFail = "explicit existing path should win";
            return false;
        }
    }

    {
        const std::string fromName = resolvePokemonPortraitPath(
            "",
            "Charmander",
            "assets/images/item_placeholder.png");
        if (fromName != "assets/images/charmander.png") {
            outFail = "name-derived portrait path mismatch";
            return false;
        }
    }

    {
        const std::string fallback = resolvePokemonPortraitPath(
            "assets/images/does_not_exist.png",
            "also_missing",
            "assets/images/item_placeholder.png");
        if (fallback != "assets/images/item_placeholder.png") {
            outFail = "fallback path should be used when explicit/name paths are missing";
            return false;
        }
    }

    {
        const std::string noFallback = resolvePokemonPortraitPath(
            "",
            "not_real_species",
            "");
        if (!noFallback.empty()) {
            outFail = "missing portrait with no fallback should return empty path";
            return false;
        }
    }

    return true;
}

