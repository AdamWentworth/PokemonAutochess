#include <sstream>
#include <string>

#include "engine/utils/LogSink.h"
#include "game/runtime/session/SessionBackendAssetBridge.h"

bool test_session_backend_asset_bridge_contract(std::string& outFail) {
    {
        game::runtime::session_backend_asset_bridge::State state;
        std::ostringstream info;
        std::ostringstream err;
        engine::log::Sink sink("TEST", &info, &err);

        auto* first =
            game::runtime::session_backend_asset_bridge::ensureBackendMeshLoaded(
                state,
                "",
                sink);
        auto* second =
            game::runtime::session_backend_asset_bridge::ensureBackendMeshLoaded(
                state,
                "",
                sink);

        if (first != nullptr || second != nullptr) {
            outFail =
                "SessionBackendAssetBridge should return null when backend mesh loading fails.";
            return false;
        }
        if (info.str().find("[Render][ModelCache] Unable to render model ''") ==
                std::string::npos ||
            info.str().find('\n') == std::string::npos) {
            outFail =
                "SessionBackendAssetBridge should log backend mesh load failures through the session console sink.";
            return false;
        }
        if (info.str().find('\n', info.str().find('\n') + 1u) != std::string::npos) {
            outFail =
                "SessionBackendAssetBridge should only report a backend mesh load failure once per model path.";
            return false;
        }
    }

    {
        game::runtime::session_backend_asset_bridge::State state;
        const auto first =
            game::runtime::session_backend_asset_bridge::loadModelForStartupPrewarm(state, "");
        const auto second =
            game::runtime::session_backend_asset_bridge::loadModelForStartupPrewarm(state, "");

        if (!first.loadedFresh || first.mesh != nullptr || first.error.empty()) {
            outFail =
                "SessionBackendAssetBridge should expose fresh startup model-load failures on the first lookup.";
            return false;
        }
        if (second.loadedFresh || second.mesh != nullptr || second.error != first.error) {
            outFail =
                "SessionBackendAssetBridge should reuse cached startup model-load failures on repeated lookups.";
            return false;
        }
    }

    {
        game::runtime::session_backend_asset_bridge::State state;
        auto* proc =
            game::runtime::session_backend_asset_bridge::ensureBackendTextureLoaded(
                state,
                "__proc:plus",
                false);
        auto* procAgain =
            game::runtime::session_backend_asset_bridge::ensureBackendTextureLoaded(
                state,
                "__proc:plus",
                false);

        if (!proc || !proc->valid || procAgain != proc) {
            outFail =
                "SessionBackendAssetBridge should reuse the session texture cache for repeated backend texture lookups.";
            return false;
        }
    }

    {
        game::runtime::session_backend_asset_bridge::State state;
        game::runtime::render_model::MeshData mesh;
        const bool first =
            game::runtime::session_backend_asset_bridge::prewarmAnimRolesForStartupPrewarm(
                state,
                "assets/models/Testmon.phmodel",
                mesh);
        const bool second =
            game::runtime::session_backend_asset_bridge::prewarmAnimRolesForStartupPrewarm(
                state,
                "assets/models/Testmon.phmodel",
                mesh);

        if (!first || second) {
            outFail =
                "SessionBackendAssetBridge should only count backend anim-role prewarm work once per model path.";
            return false;
        }
    }

    return true;
}
