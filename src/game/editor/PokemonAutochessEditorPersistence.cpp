#include "game/editor/PokemonAutochessEditorPersistence.h"

#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace game::editor::persistence {
namespace {

namespace route1 =
    game::runtime::route1_environment;
using PreviewUnitTransform =
    layout_transactions::PreviewUnitTransform;

nlohmann::json defaultPreviewUnitDocument() {
    return {
        {"schema_version", 1},
        {"previews", nlohmann::json::object()}};
}

bool readJsonVec3(
    const nlohmann::json& value,
    std::array<float, 3>& out) {
    if (!value.is_array() || value.size() != 3u) {
        return false;
    }
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
        if (!value[axis].is_number()) {
            return false;
        }
        out[axis] = value[axis].get<float>();
        if (!std::isfinite(out[axis])) {
            return false;
        }
    }
    return true;
}

const nlohmann::json* savedPreviewUnitOverride(
    const nlohmann::json& document,
    std::string_view previewId,
    std::string_view unitKey) {
    if (!document.is_object()) {
        return nullptr;
    }
    const auto previews = document.find("previews");
    if (previews == document.end() || !previews->is_object()) {
        return nullptr;
    }
    const auto preview = previews->find(std::string(previewId));
    if (preview == previews->end() || !preview->is_object()) {
        return nullptr;
    }
    const auto units = preview->find("units");
    if (units == preview->end() || !units->is_object()) {
        return nullptr;
    }
    const auto unit = units->find(std::string(unitKey));
    return unit == units->end() || !unit->is_object()
        ? nullptr
        : &*unit;
}

struct WriteMessages {
    const char* createDirectory = nullptr;
    const char* openTemporary = nullptr;
    const char* writeTemporary = nullptr;
    const char* replaceDestination = nullptr;
};

bool writeTemporaryReplacement(
    const std::filesystem::path& destination,
    std::string_view contents,
    const WriteMessages& messages,
    std::string* outError) {
    const std::filesystem::path temporary =
        destination.string() + ".editor-tmp";
    std::error_code error;
    std::filesystem::create_directories(
        destination.parent_path(), error);
    if (error) {
        if (outError) {
            *outError =
                std::string(messages.createDirectory) +
                error.message();
        }
        return false;
    }
    {
        std::ofstream output(
            temporary,
            std::ios::binary | std::ios::trunc);
        if (!output) {
            if (outError) {
                *outError = messages.openTemporary;
            }
            return false;
        }
        output << contents;
        output.flush();
        if (!output) {
            if (outError) {
                *outError = messages.writeTemporary;
            }
            return false;
        }
    }
    std::filesystem::copy_file(
        temporary,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error);
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);
    if (error) {
        if (outError) {
            *outError =
                std::string(messages.replaceDestination) +
                error.message();
        }
        return false;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

constexpr WriteMessages kPreviewUnitWriteMessages{
    .createDirectory =
        "Could not create the game-preview layout directory: ",
    .openTemporary =
        "Could not open the temporary game-preview layout file.",
    .writeTemporary =
        "Could not write the game-preview layout file.",
    .replaceDestination =
        "Could not replace the game-preview layout file: "};

constexpr WriteMessages kBoardWriteMessages{
    .createDirectory =
        "Could not create the board-layout directory: ",
    .openTemporary =
        "Could not open the temporary board-layout manifest.",
    .writeTemporary =
        "Could not write the temporary board-layout manifest.",
    .replaceDestination =
        "Could not replace the board-layout manifest: "};

constexpr WriteMessages kAuthoredSceneWriteMessages{
    .createDirectory =
        "Could not create the layout manifest directory: ",
    .openTemporary =
        "Could not open the temporary layout manifest.",
    .writeTemporary =
        "Could not write the temporary layout manifest.",
    .replaceDestination =
        "Could not replace the project layout manifest: "};

std::string serializeAuthoredScene(
    const engine::assets::phlosion::AuthoredSceneDocument& document) {
    if (!document.nodes.empty()) {
        return engine::assets::phlosion::
            serializeAuthoredSceneDocument(document);
    }
    // Preserve the promoted, source-identical empty checkpoint byte for byte
    // after the last authored edit is restored.
    return
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"kind\": \"phlosion_authored_scene\",\n"
        "  \"scene_id\": \"routes/route1\",\n"
        "  \"base_environment_asset_id\": \"environments/route1\",\n"
        "  \"coordinate_system\": \"source_centimetres_xyz_y_up\",\n"
        "  \"nodes\": []\n"
        "}\n";
}

} // namespace

struct Store::Impl {
    std::filesystem::path projectRoot;
    nlohmann::json previewUnitOverrides =
        defaultPreviewUnitDocument();
};

Store::Store()
    : impl_(std::make_unique<Impl>()) {}

Store::Store(std::filesystem::path projectRoot)
    : Store() {
    setProjectRoot(std::move(projectRoot));
}

Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;

void Store::setProjectRoot(
    std::filesystem::path projectRoot) {
    impl_->projectRoot = std::move(projectRoot);
}

bool Store::loadPreviewUnitOverrides(std::string* outError) {
    impl_->previewUnitOverrides = defaultPreviewUnitDocument();
    const std::filesystem::path path =
        impl_->projectRoot /
        "config/editor/game_preview_layouts.json";
    if (!std::filesystem::is_regular_file(path)) {
        if (outError) {
            outError->clear();
        }
        return true;
    }
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("could not open the file");
        }
        nlohmann::json parsed;
        input >> parsed;
        if (!parsed.is_object()) {
            throw std::runtime_error(
                "the document root is not an object");
        }
        if (!parsed.contains("previews") ||
            !parsed.at("previews").is_object()) {
            parsed["previews"] = nlohmann::json::object();
        }
        parsed["schema_version"] = 1;
        impl_->previewUnitOverrides = std::move(parsed);
    } catch (const std::exception& error) {
        if (outError) {
            *outError =
                "Could not load game-preview unit placements: " +
                std::string(error.what());
        }
        return false;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

bool Store::hasPreviewUnitOverride(
    std::string_view previewId,
    std::string_view unitKey) const {
    return savedPreviewUnitOverride(
        impl_->previewUnitOverrides,
        previewId,
        unitKey) != nullptr;
}

bool Store::applyPreviewUnitOverride(
    std::string_view previewId,
    std::string_view unitKey,
    PreviewUnitTransform& inOutTransform) const {
    const auto* saved = savedPreviewUnitOverride(
        impl_->previewUnitOverrides,
        previewId,
        unitKey);
    if (!saved) {
        return false;
    }
    const auto position = saved->find("position_world");
    const auto rotation = saved->find("rotation_degrees");
    if (position == saved->end() ||
        !readJsonVec3(*position, inOutTransform.position)) {
        return false;
    }
    if (rotation != saved->end()) {
        readJsonVec3(
            *rotation,
            inOutTransform.rotationDegrees);
    }
    return true;
}

bool Store::savePreviewUnitOverride(
    std::string_view previewId,
    std::string_view unitKey,
    const PreviewUnitRecord& record,
    std::string* outError) {
    const nlohmann::json previous = impl_->previewUnitOverrides;
    try {
        auto& saved =
            impl_->previewUnitOverrides["previews"]
                [std::string(previewId)]["units"]
                [std::string(unitKey)];
        saved = {
            {"species", record.speciesName},
            {"side", record.playerSide ? "player" : "enemy"},
            {"placement", record.benchUnit ? "bench" : "board"},
            {"board_cell", {
                record.boardColumn,
                record.boardRow}},
            {"bench_slot", record.benchSlot},
            {"position_world", record.transform.position},
            {"rotation_degrees", record.transform.rotationDegrees}};
        const std::string contents =
            impl_->previewUnitOverrides.dump(2) + '\n';
        if (writeTemporaryReplacement(
                impl_->projectRoot /
                    "config/editor/game_preview_layouts.json",
                contents,
                kPreviewUnitWriteMessages,
                outError)) {
            return true;
        }
    } catch (const std::exception& error) {
        if (outError) {
            *outError =
                "Could not write the game-preview layout file: " +
                std::string(error.what());
        }
    }
    impl_->previewUnitOverrides = previous;
    return false;
}

bool Store::resetPreviewUnitOverride(
    std::string_view previewId,
    std::string_view unitKey,
    std::string* outError) {
    const nlohmann::json previous = impl_->previewUnitOverrides;
    try {
        auto& units =
            impl_->previewUnitOverrides["previews"]
                [std::string(previewId)]["units"];
        if (units.is_object()) {
            units.erase(std::string(unitKey));
        }
        const std::string contents =
            impl_->previewUnitOverrides.dump(2) + '\n';
        if (writeTemporaryReplacement(
                impl_->projectRoot /
                    "config/editor/game_preview_layouts.json",
                contents,
                kPreviewUnitWriteMessages,
                outError)) {
            return true;
        }
    } catch (const std::exception& error) {
        if (outError) {
            *outError =
                "Could not write the game-preview layout file: " +
                std::string(error.what());
        }
    }
    impl_->previewUnitOverrides = previous;
    return false;
}

bool Store::saveBoardRegistration(
    bool sceneMounted,
    const route1::BoardLayoutTransform& layout,
    std::string* outError) const {
    if (impl_->projectRoot.empty() || !sceneMounted) {
        if (outError) {
            *outError =
                "The gameplay board cannot be saved before Route 1 is mounted.";
        }
        return false;
    }
    return writeTemporaryReplacement(
        impl_->projectRoot / route1::kBoardLayoutManifestPath,
        route1::serializeBoardLayoutTransform(layout),
        kBoardWriteMessages,
        outError);
}

bool Store::saveAuthoredScene(
    bool sceneMounted,
    const std::filesystem::path& destination,
    const engine::assets::phlosion::AuthoredSceneDocument& document,
    std::string* outError) const {
    if (impl_->projectRoot.empty() || !sceneMounted) {
        if (outError) {
            *outError =
                "Route 1 layout cannot be saved before the project scene is mounted.";
        }
        return false;
    }
    if (destination.empty()) {
        if (outError) {
            *outError =
                "The active scene has no authored document path.";
        }
        return false;
    }
    return writeTemporaryReplacement(
        destination,
        serializeAuthoredScene(document),
        kAuthoredSceneWriteMessages,
        outError);
}

} // namespace game::editor::persistence
