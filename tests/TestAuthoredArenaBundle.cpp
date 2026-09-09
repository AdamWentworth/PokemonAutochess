#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

bool test_authored_arena_bundle_contract(std::string &outFail) {
    namespace ph = engine::assets::phlosion;
    namespace arena = game::runtime::authored_arena;
    try {
        const auto check = [](bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); };
        game::assets::DevAssetStore host(engine::paths::dataRoot());
        std::string text;
        check(host.readText("config/environment/route1_south_entrance.authoring.json", text, &outFail), outFail);
        const auto recipe = nlohmann::json::parse(text);
        std::vector<ph::SceneArchiveFile> inputs;
        for (const auto *key : {"scene_path", "gameplay_map_path", "board_path", "composition_path"}) {
            ph::SceneArchiveFile file{recipe.at(key).get<std::string>(), {}};
            check(host.readBytes(file.virtualPath, file.bytes, &outFail), outFail);
            inputs.push_back(std::move(file));
        }
        game::arena::ArenaMapData map;
        check(map.load(std::string(inputs[1].bytes.begin(), inputs[1].bytes.end()), &outFail), outFail);
        // Synthetic planar/ramp geometry: no private mesh or renderer needed.
        ph::EnvironmentPatchDocument patch;
        patch.source = {"route1_environment_road001_00", std::string(64, '0'), std::string(64, '1'), "source_centimetres_xyz_y_up"};
        ph::EnvironmentPatchMesh mesh;
        mesh.id = "fixture";
        mesh.displayName = "Fixture floor";
        ph::EnvironmentPatchMaterialGroup group{19, {}};
        for (const auto &[key, tile] : map.tiles) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const auto &offset : std::array<std::array<int, 2>, 4>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}) {
                const float x = (tile.x + offset[0]) * 100.0f, z = (tile.z + offset[1]) * 100.0f;
                ph::EnvironmentPatchVertex vertex;
                vertex.position = {x, tile.heightAt(x, z), z};
                vertex.normal = {0, 1, 0};
                mesh.vertices.push_back(vertex);
            }
            for (auto i : {0u, 2u, 1u, 0u, 3u, 2u})
                group.indices.push_back(base + i);
        }
        mesh.materialGroups.push_back(std::move(group));
        patch.meshes.push_back(std::move(mesh));
        check(ph::validateEnvironmentPatchDocument(patch, &outFail), outFail);
        inputs.push_back({recipe.at("terrain_path").get<std::string>(), ph::serializeEnvironmentPatchBinary(patch)});
        const auto recipeText = recipe.dump();
        inputs.push_back({"arena/recipe.json", {recipeText.begin(), recipeText.end()}});
        const auto encode = [&](const auto &files) {
            std::vector<std::uint8_t> bytes;
            check(ph::encodeSceneArchive(map.sceneId, files, bytes, &outFail), outFail);
            return bytes;
        };
        const auto good = encode(inputs);
        arena::Bundle loaded;
        check(loaded.loadBytes(good, &outFail), outFail);
        std::vector<std::uint8_t> canonical, reformatted;
        check(arena::encode(loaded.store, "arena/recipe.json", canonical, &outFail), outFail);
        auto prettyInputs = inputs;
        for (auto &file : prettyInputs) {
            if (file.virtualPath == recipe.at("terrain_path").get<std::string>()) continue;
            const auto pretty = nlohmann::json::parse(file.bytes).dump(4);
            file.bytes.clear();
            for (char c : pretty) {
                if (c == '\n') file.bytes.push_back('\r');
                file.bytes.push_back(static_cast<std::uint8_t>(c));
            }
        }
        ph::SceneArchiveStore prettyStore;
        check(prettyStore.loadBytes(encode(prettyInputs), &outFail), outFail);
        check(arena::encode(prettyStore, "arena/recipe.json", reformatted, &outFail), outFail);
        check(canonical == reformatted, "JSON formatting or checkout line endings changed the arena revision.");
        const auto reject = [&](const auto &bytes, const char *message) {
            check(!loaded.loadBytes(bytes, &outFail), message);
            check(loaded.scene.sceneId == map.sceneId && loaded.map.tiles.size() == map.tiles.size(), "Failed load replaced the previous complete arena.");
        };
        auto corrupt = good;
        corrupt.back() ^= 1u;
        reject(corrupt, "Archive corruption was accepted.");
        corrupt.resize(corrupt.size() / 2);
        reject(corrupt, "Truncated archive was accepted.");
        auto missing = inputs;
        missing.erase(missing.begin());
        reject(encode(missing), "Incomplete arena was accepted.");
        const auto mutate = [&](std::size_t file, const auto &edit) {
            auto changed = inputs;
            auto document = nlohmann::json::parse(changed[file].bytes);
            edit(document);
            const auto value = document.dump();
            changed[file].bytes.assign(value.begin(), value.end());
            return encode(changed);
        };
        reject(mutate(1, [](auto &doc) { for (auto &cell : doc["cells"]) cell["height"] = cell["height"].template get<int>()+1; }),
               "Logically consistent heights with the previous rendered terrain were accepted.");
        reject(mutate(1, [](auto &doc) { for (auto &polygon : doc["cover_regions"][0]["polygons_source_xz_cm"]) for (auto &point : polygon) point[0] = point[0].template get<double>()+100; }),
               "Cover separated from its visible grass was accepted.");
        reject(mutate(1, [](auto &doc) { doc["scene_id"] = "routes/wrong"; }), "Mixed scene identity was accepted.");
        reject(mutate(2, [](auto &doc) { doc["board_registration"]["bench_gap_cells"] = 1; }), "Stale reserve registration was accepted.");
        check(loaded.loadBytes(good, &outFail), "A valid arena did not recover after failed reloads.");
        outFail.clear();
        return true;
    } catch (const std::exception &error) {
        outFail = error.what();
        return false;
    }
}
