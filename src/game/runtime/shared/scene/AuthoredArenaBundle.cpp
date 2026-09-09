#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/AuthoredGroundSurface.h"
#include "game/arena/EncounterGrassFootprint.h"
#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"
#include <nlohmann/json.hpp>
#include <set>
#include <cmath>
#include <algorithm>
#include "game/runtime/shared/scene/BoardLayoutDocument.h"
#include <stdexcept>

namespace game::runtime::authored_arena {
namespace {
namespace ph = engine::assets::phlosion;
constexpr const char *kRecipe = "arena/recipe.json";
constexpr const char *kPaths[] = {"scene_path", "terrain_path", "gameplay_map_path", "board_path", "composition_path"};
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
nlohmann::json read(const engine::IAssetStore &store, const std::string &path) {
    std::string text, error;
    require(store.readText(path, text, &error), error);
    return nlohmann::json::parse(text);
}
void validateRecipe(const nlohmann::json &recipe) {
    require(recipe.at("kind") == "pokemon_autochess_arena_authoring" && recipe.at("schema_version") == 1, "Unsupported arena recipe.");
    std::set<std::string> paths{kRecipe};
    for (const auto *key : kPaths) {
        const std::string path = recipe.at(key).get<std::string>();
        require(!path.empty() && path[0] != '/' && path.back() != '/' && path.find('\\') == std::string::npos && path.find(':') == std::string::npos,
                "Arena paths must be project-relative.");
        std::size_t start = 0;
        while (start < path.size()) {
            const auto end = path.find('/', start);
            const auto part = path.substr(start, end == std::string::npos ? end : end - start);
            require(!part.empty() && part != "." && part != "..", "Unsafe arena path.");
            if (end == std::string::npos) break;
            start = end + 1;
        }
        require(paths.insert(path).second, "Duplicate arena path.");
    }
}
} // namespace

bool Bundle::load(const engine::IAssetStore &host, const std::string &path, std::string *error) {
    std::vector<std::uint8_t> bytes;
    return host.readBytes(path, bytes, error) && loadBytes(bytes, error);
}
bool Bundle::loadBytes(const std::vector<std::uint8_t> &bytes, std::string *error) {
    Bundle next;
    if (!next.store.loadBytes(bytes, error) || !next.validate(error)) return false;
    *this = std::move(next);
    return true;
}
bool Bundle::validate(std::string *error) {
    try {
        const auto recipe = read(store, kRecipe);
        validateRecipe(recipe);
        require(store.fileCount() == 6, "An arena archive must contain exactly its five inputs and recipe.");
        std::string detail, mapText;
        for (const auto *key : kPaths)
            require(store.exists(recipe.at(key).get<std::string>()), "Incomplete arena archive.");
        require(ph::loadAuthoredSceneDocument(store, recipe.at("scene_path").get<std::string>(), scene, &detail), detail);
        require(store.readText(recipe.at("gameplay_map_path").get<std::string>(), mapText, &detail) && map.load(mapText, &detail), detail);
        require(scene.sceneId == store.sceneId() && scene.sceneId == map.sceneId && recipe.at("scene_id") == scene.sceneId,
                "Arena scene identities disagree.");
        require(recipe.at("base_environment_asset_id") == scene.baseEnvironmentAssetId, "Arena source environment disagrees with its recipe.");
        boardPath = recipe.at("board_path").get<std::string>();
        const auto board = read(store, boardPath);
        const auto &registration = board.at("board_registration");
        const auto bounded = [](const nlohmann::json &v, int low, int high) {
            require(v.is_number_integer() && v >= low && v <= high, "Arena registration exceeds supported limits.");
            return v.get<int>();
        };
        const int ox = bounded(registration.at("terrain_grid_origin").at(0), -100000, 100000);
        const int oz = bounded(registration.at("terrain_grid_origin").at(1), -100000, 100000);
        const int cols = bounded(registration.at("board_cells").at(0), 1, 256);
        const int rows = bounded(registration.at("board_cells").at(1), 1, 256);
        bounded(registration.at("bench_slots"), 1, 256);
        bounded(registration.at("bench_gap_cells"), 0, 64);
        route1_environment::BoardLayoutTransform layout;
        require(route1_environment::loadBoardLayoutTransform(store, boardPath, layout, &detail), detail);
        require(map.playableCells.size() == static_cast<std::size_t>(cols) * rows, "Arena board footprint is stale.");
        for (int z = oz; z < oz + rows; ++z)
            for (int x = ox; x < ox + cols; ++x)
                require(map.playableCells.contains({x, z}), "Arena board footprint differs from its registration.");
        std::set<std::pair<int, int>> reserve;
        const auto addBench = [&](auto origin) {
            for (std::uint32_t slot = 0; slot < layout.benchSlots; ++slot)
                reserve.emplace(origin[0] + static_cast<int>(slot), origin[1]);
        };
        if (layout.northBench) addBench(route1_environment::northBenchTerrainGridOrigin(layout));
        if (layout.southBench) addBench(route1_environment::southBenchTerrainGridOrigin(layout));
        require(reserve == map.reserveCells, "Arena reserve footprint differs from its registration.");
        const std::string terrainPath = recipe.at("terrain_path").get<std::string>();
        std::size_t patchCount = 0;
        for (const auto &node : scene.nodes) {
            if (!node.meshPatch) continue;
            ++patchCount;
            require(node.enabled && node.id == recipe.at("terrain_node_id").get<std::string>() && node.meshPatch->assetPath == terrainPath && node.transform &&
                        node.transform->translation == std::array<float, 3>{0, 0, 0} &&
                        node.transform->rotationDegrees == std::array<float, 3>{0, 0, 0} &&
                        node.transform->scale == std::array<float, 3>{1, 1, 1},
                    "Arena terrain must use its source-coordinate authored mesh.");
        }
        require(patchCount == 1, "An arena requires one complete terrain patch.");
        ph::EnvironmentPatchDocument patch;
        require(ph::loadEnvironmentPatchDocument(store, terrainPath, patch, &detail), detail);
        require(route1_environment::route1EnvironmentProfilesCompatible(patch.source.profileId, layout.sourceProfileId),
                "Arena terrain and board source profiles disagree.");
        const auto composition = read(store, recipe.at("composition_path").get<std::string>());
        const auto sceneJson = read(store, recipe.at("scene_path").get<std::string>());
        std::map<std::string, nlohmann::json> grassRecords;
        for (const auto &record : composition.at("encounter_grass").at("records")) {
            grassRecords.emplace("encounter-grass/" + record.at("model").get<std::string>() + "/record-" +
                                     std::to_string(record.at("record_index").get<int>()),
                                 record);
        }
        std::size_t coverCount = 0;
        for (const auto &node : sceneJson.at("nodes")) {
            const auto &components = node.at("components");
            if (!node.at("enabled").get<bool>() || !components.contains("prefab_instance")) continue;
            const auto &prefab = components.at("prefab_instance");
            if (!prefab.at("prototype_node_id").get<std::string>().starts_with("encounter-grass/")) continue;
            ++coverCount;
            const auto id = node.at("id").get<std::string>();
            const auto region = std::find_if(map.cover.begin(), map.cover.end(), [&](const auto &r) { return r.id == id; });
            require(region != map.cover.end(), "Arena encounter grass is missing its gameplay footprint.");
            const auto &record = grassRecords.at(prefab.at("prototype_node_id").get<std::string>());
            std::set<std::pair<int, int>> core;
            for (const auto &cell : record.at("core_cells_source_xz"))
                core.emplace(cell.at(0).get<int>(), cell.at(1).get<int>());
            const auto centers = game::arena::encounterGrassCenters(core);
            require(!centers.empty() && centers.size() == region->polygons.size(), "Arena cover polygon count is stale.");
            const auto &transform = components.at("transform");
            const auto &rotation = transform.at("rotation_degrees");
            require(std::abs(rotation.at(0).get<double>()) <= .001 && std::abs(rotation.at(2).get<double>()) <= .001,
                    "Arena cover requires upright props.");
            const double angle = rotation.at(1).get<double>() * 3.14159265358979323846 / 180;
            constexpr int corners[4][2] = {{-50, -50}, {50, -50}, {50, 50}, {-50, 50}};
            for (std::size_t i = 0; i < centers.size(); ++i)
                for (std::size_t j = 0; j < 4; ++j) {
                    const double x = (centers[i][0] + corners[j][0]) * transform.at("scale")[0].get<double>();
                    const double z = (centers[i][1] + corners[j][1]) * transform.at("scale")[2].get<double>();
                    const double px = transform.at("translation")[0].get<double>() + std::cos(angle) * x + std::sin(angle) * z;
                    const double pz = transform.at("translation")[2].get<double>() - std::sin(angle) * x + std::cos(angle) * z;
                    require(std::abs(region->polygons[i][j][0] - px) <= .002 && std::abs(region->polygons[i][j][1] - pz) <= .002,
                            "Arena rendered grass and gameplay cover disagree.");
                }
        }
        require(coverCount == map.cover.size(), "Arena contains cover for absent encounter grass.");
        authored_environment::GroundSurface ground;
        const auto material = static_cast<std::uint32_t>(bounded(recipe.at("ground_material_index"), 0, 65535));
        for (const auto &mesh : patch.meshes)
            for (const auto &group : mesh.materialGroups) {
                if (group.materialIndex != material) continue;
                for (std::size_t i = 0; i + 2 < group.indices.size(); i += 3) {
                    authored_environment::GroundSurface::Triangle triangle;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto &p = mesh.vertices.at(group.indices[i + corner]).position;
                        triangle[corner] = {p[0], p[1], p[2]};
                    }
                    require(ground.add(triangle), "Arena floor geometry exceeds spatial limits.");
                }
            }
        // Interior probes leave room for cosmetic rounded caps at cell edges.
        for (const auto &[key, tile] : map.tiles)
            for (float u : {.3f, .5f, .7f})
                for (float v : {.3f, .5f, .7f}) {
                    const float x = (tile.x + u) * 100, z = (tile.z + v) * 100;
                    float height = 0;
                    require(ground.sample(x, z, height) && std::abs(height - tile.heightAt(x, z)) <= .12f,
                            "Arena logical height and rendered floor disagree at tile " + std::to_string(key.first) + "," + std::to_string(key.second) + " (rendered " + std::to_string(height) + ", logical " + std::to_string(tile.heightAt(x, z)) + ")");
                }
        if (error) error->clear();
        return true;
    } catch (const std::exception &exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool encode(const engine::IAssetStore &inputs, const std::string &recipePath, std::vector<std::uint8_t> &bytes, std::string *error) {
    try {
        const auto recipe = read(inputs, recipePath);
        validateRecipe(recipe);
        std::vector<ph::SceneArchiveFile> files;
        for (const auto *key : kPaths) {
            ph::SceneArchiveFile file;
            file.virtualPath = recipe.at(key).get<std::string>();
            require(inputs.readBytes(file.virtualPath, file.bytes, error), error ? *error : "Missing arena input.");
            if (std::string_view(key) != "terrain_path") {
                // Git checkout line endings and JSON indentation must not
                // change the revision of an otherwise identical arena.
                const auto canonical = nlohmann::json::parse(file.bytes).dump();
                file.bytes.assign(canonical.begin(), canonical.end());
            }
            files.push_back(std::move(file));
        }
        const auto text = recipe.dump();
        files.push_back({kRecipe, {text.begin(), text.end()}});
        require(ph::encodeSceneArchive(recipe.at("scene_id").get<std::string>(), std::move(files), bytes, error), error ? *error : "Arena encoding failed.");
        Bundle check;
        return check.loadBytes(bytes, error);
    } catch (const std::exception &exception) {
        if (error) *error = exception.what();
        return false;
    }
}

} // namespace game::runtime::authored_arena
