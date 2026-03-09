#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"

#include <string>

bool test_shared_projected_world_scene_helpers_contract(std::string& outFail) {
    using game::runtime::shared_projected_scene::resolveModelMesh;

    {
        GameDataDb dataDb;
        PokemonInstance unit;
        unit.name = "missingno";
        unit.backendModelPath = "assets/models/cached_backend_path.glb";

        game::runtime::backend_model::MeshData mesh;
        mesh.indices = {0u, 1u, 2u};

        std::string seenPath;
        const auto* resolved = resolveModelMesh(
            unit,
            dataDb,
            [&](const std::string& modelPath) -> game::runtime::backend_model::MeshData* {
                seenPath = modelPath;
                return &mesh;
            });
        if (resolved != &mesh || seenPath != unit.backendModelPath) {
            outFail = "resolveModelMesh should prefer cached backendModelPath";
            return false;
        }
    }

    {
        GameDataDb dataDb;
        PokemonInstance unit;
        unit.name = "missingno";
        unit.animIndexCacheSourceModelPath = "assets/models/cached_anim_source.glb";

        game::runtime::backend_model::MeshData mesh;
        mesh.indices = {0u, 1u, 2u};

        std::string seenPath;
        const auto* resolved = resolveModelMesh(
            unit,
            dataDb,
            [&](const std::string& modelPath) -> game::runtime::backend_model::MeshData* {
                seenPath = modelPath;
                return &mesh;
            });
        if (resolved != &mesh || seenPath != unit.animIndexCacheSourceModelPath) {
            outFail = "resolveModelMesh should fall back to cached animation source model path";
            return false;
        }
    }

    {
        GameDataDb dataDb;
        PokemonInstance unit;
        unit.name = "missingno";
        unit.backendAnimDurationsSourceModelPath = "assets/models/cached_duration_source.glb";

        game::runtime::backend_model::MeshData mesh;
        mesh.indices = {0u, 1u, 2u};

        std::string seenPath;
        const auto* resolved = resolveModelMesh(
            unit,
            dataDb,
            [&](const std::string& modelPath) -> game::runtime::backend_model::MeshData* {
                seenPath = modelPath;
                return &mesh;
            });
        if (resolved != &mesh || seenPath != unit.backendAnimDurationsSourceModelPath) {
            outFail = "resolveModelMesh should fall back to cached backend animation duration model path";
            return false;
        }
    }

    return true;
}
