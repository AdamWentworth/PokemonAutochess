#include "game/arena/ArenaMapData.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace game::arena {
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int integer(const nlohmann::json &value, int minimum, int maximum) {
    require(value.is_number_integer(), "Arena cell values must be integers.");
    require(value >= minimum && value <= maximum, "Arena cell value is outside its supported range.");
    return value.get<int>();
}
std::pair<int, int> cell(const nlohmann::json &value) {
    require(value.is_array() && value.size() == 2, "Expected an X/Z cell.");
    return {integer(value[0], -100000, 100000), integer(value[1], -100000, 100000)};
}
} // namespace

float Tile::heightAt(float sourceXcm, float sourceZcm) const noexcept {
    const float u = std::clamp(sourceXcm / 100.0f - x, 0.0f, 1.0f);
    const float v = std::clamp(sourceZcm / 100.0f - z, 0.0f, 1.0f);
    const float rise[] = {0, 1 - v, u, v, 1 - u,
                          std::max(0.0f, u - v), std::min(1.0f, 1 + u - v),
                          std::max(0.0f, u + v - 1), std::min(1.0f, u + v),
                          std::max(0.0f, v - u), std::min(1.0f, 1 + v - u),
                          std::max(0.0f, 1 - u - v), std::min(1.0f, 2 - u - v)};
    return 50.0f * (height + rise[ramp]);
}

bool CoverRegion::contains(float x, float z) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(z)) return false;
    for (const auto &polygon : polygons) {
        bool positive = false, negative = false;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
            const float cross = (b[0] - a[0]) * (z - a[1]) - (b[1] - a[1]) * (x - a[0]);
            positive |= cross > 0.001f;
            negative |= cross < -0.001f;
        }
        if (!(positive && negative)) return true;
    }
    return false;
}

const Tile *ArenaMapData::tileAt(int x, int z) const noexcept {
    const auto found = tiles.find({x, z});
    return found == tiles.end() ? nullptr : &found->second;
}
std::vector<std::string> ArenaMapData::coverAt(float x, float z) const {
    std::vector<std::string> result;
    for (const auto &region : cover)
        if (region.contains(x, z)) result.push_back(region.id);
    return result;
}
std::array<float, 2> ArenaMapData::edgeHeightDelta(Cell from, Cell to) const {
    const auto *a = tileAt(from.x, from.z), *b = tileAt(to.x, to.z);
    require(a && b && std::abs(from.x - to.x) + std::abs(from.z - to.z) == 1, "Expected a cardinal edge between existing tiles.");
    float x = static_cast<float>(from.x), z = static_cast<float>(from.z);
    if (to.x != from.x) x += to.x > from.x ? 1.0f : 0.0f;
    else z += to.z > from.z ? 1.0f : 0.0f;
    const auto delta = [&](float px, float pz) { return b->heightAt(px * 100, pz * 100) - a->heightAt(px * 100, pz * 100); };
    return {delta(x, z), delta(x + (to.x == from.x ? 1.0f : 0.0f), z + (to.x != from.x ? 1.0f : 0.0f))};
}

bool ArenaMapData::load(const std::string &text, std::string *error) {
    try {
        const auto doc = nlohmann::json::parse(text);
        require(doc.at("kind") == "pokemon_autochess_arena_map" && doc.at("schema_version") == 1,
                "Unsupported arena map schema.");
        require(doc.at("coordinate_system") == "source_centimetres_xyz_y_up" &&
                    doc.at("tile_size_cm") == 100 && doc.at("elevation_step_cm") == 50,
                "Unsupported arena coordinates.");
        ArenaMapData next;
        next.sceneId = doc.at("scene_id").get<std::string>();
        require(!next.sceneId.empty(), "Arena scene ID is empty.");
        const auto &rows = doc.at("cells");
        require(rows.is_array() && !rows.empty() && rows.size() <= 65536, "Arena tile count is invalid.");
        for (const auto &row : rows) {
            Tile tile{integer(row.at("x"), -100000, 100000), integer(row.at("z"), -100000, 100000),
                      integer(row.at("height"), 0, 8), integer(row.at("surface"), 0, 2), integer(row.at("ramp"), 0, Tile::kMaximumRampShape)};
            require(next.tiles.emplace(std::pair{tile.x, tile.z}, tile).second, "Duplicate arena tile.");
        }
        const auto readCells = [&](const char *key, auto &result) {
            require(doc.at(key).is_array() && doc.at(key).size() <= 65536, "Invalid cell list.");
            for (const auto &entry : doc.at(key)) {
                const auto xy = cell(entry);
                require(next.tiles.contains(xy) && result.insert(xy).second, "Missing or duplicate board/reserve cell.");
            }
        };
        readCells("playable_cells", next.playableCells);
        readCells("reserve_cells", next.reserveCells);
        require(!next.playableCells.empty(), "Arena has no playable cells.");
        for (const auto &c : next.reserveCells)
            require(!next.playableCells.contains(c), "Board and reserve overlap.");
        std::set<std::array<int, 4>> expected;
        for (const auto &[x, z] : next.playableCells) {
            for (const auto &[dx, dz] : std::array<std::pair<int, int>, 4>{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}}) {
                if (next.playableCells.contains({x + dx, z + dz})) expected.insert({x, z, x + dx, z + dz});
            }
        }
        require(doc.at("connections").is_array() && doc.at("connections").size() <= 262144, "Invalid connection list.");
        for (const auto &edge : doc.at("connections")) {
            const auto [x, z] = cell(edge.at("from"));
            const auto [tx, tz] = cell(edge.at("to"));
            require(expected.erase({x, z, tx, tz}) == 1, "Duplicate or non-cardinal arena connection.");
            const auto delta = next.edgeHeightDelta({x, z}, {tx, tz});
            const auto &actual = edge.at("edge_height_delta_cm");
            require(actual.is_array() && actual.size() == 2 && actual[0] == delta[0] && actual[1] == delta[1], "Stale edge height data.");
        }
        require(expected.empty(), "Arena is missing directed connections.");
        std::set<std::string> ids;
        require(doc.at("cover_regions").is_array() && doc.at("cover_regions").size() <= 65536, "Invalid cover list.");
        std::size_t polygonCount = 0;
        for (const auto &entry : doc.at("cover_regions")) {
            CoverRegion region;
            region.id = entry.at("id").get<std::string>();
            require(!region.id.empty() && ids.insert(region.id).second, "Duplicate or empty cover identity.");
            require(entry.at("polygons_source_xz_cm").is_array(), "Invalid polygon list.");
            for (const auto &row : entry.at("polygons_source_xz_cm")) {
                require(++polygonCount <= 65536, "Too many arena cover polygons.");
                require(row.is_array() && row.size() == 4, "Cover footprints require four corners.");
                std::array<std::array<float, 2>, 4> polygon;
                for (std::size_t i = 0; i < 4; ++i) {
                    require(row[i].is_array() && row[i].size() == 2, "Invalid cover corner.");
                    for (std::size_t axis = 0; axis < 2; ++axis) {
                        polygon[i][axis] = row[i][axis].get<float>();
                        require(std::isfinite(polygon[i][axis]) && std::abs(polygon[i][axis]) <= 1.0e7f, "Invalid cover coordinate.");
                    }
                }
                float winding = 0;
                for (std::size_t i = 0; i < 4; ++i) {
                    const auto &a = polygon[i], &b = polygon[(i + 1) % 4], &c = polygon[(i + 2) % 4];
                    const float cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
                    require(std::abs(cross) > 0.001f && (i == 0 || cross * winding > 0), "Degenerate or non-convex cover footprint.");
                    winding = cross;
                }
                region.polygons.push_back(polygon);
                require(region.polygons.size() <= 65536, "Too many cover polygons.");
            }
            require(!region.polygons.empty(), "Empty cover footprint.");
            next.cover.push_back(std::move(region));
        }
        *this = std::move(next);
        if (error) error->clear();
        return true;
    } catch (const std::exception &exception) {
        if (error) *error = exception.what();
        return false;
    }
}

} // namespace game::arena
