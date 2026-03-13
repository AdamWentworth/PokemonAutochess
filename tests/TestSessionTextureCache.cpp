#include "game/runtime/session/SessionTextureCache.h"

#include <string>

bool test_session_texture_cache_contract(std::string& outFail) {
    game::runtime::session_texture_cache::TextureCache cache;

    auto* white =
        game::runtime::session_texture_cache::ensureTextureLoaded(cache, "", false);
    if (!white || !white->valid || white->width != 1 || white->height != 1 ||
        white->rgba.size() != 4u) {
        outFail =
            "SessionTextureCache should synthesize a 1x1 white texture for the empty texture key.";
        return false;
    }

    auto* proc =
        game::runtime::session_texture_cache::ensureTextureLoaded(cache, "__proc:plus", false);
    if (!proc || !proc->valid || proc->width != 64 || proc->height != 64 ||
        proc->rgba.empty()) {
        outFail =
            "SessionTextureCache should rasterize procedural texture identifiers into cached RGBA textures.";
        return false;
    }

    auto* procAgain =
        game::runtime::session_texture_cache::ensureTextureLoaded(cache, "__proc:plus", false);
    if (procAgain != proc) {
        outFail =
            "SessionTextureCache should reuse cached entries for repeated texture lookups.";
        return false;
    }

    auto* missing = game::runtime::session_texture_cache::ensureTextureLoaded(
        cache,
        "assets/textures/does_not_exist.png",
        false);
    if (missing != nullptr) {
        outFail =
            "SessionTextureCache should return null when a texture file cannot be loaded.";
        return false;
    }

    return true;
}
