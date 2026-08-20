#pragma once

#include "game/editor/PokemonAutochessEditorLayoutTransactions.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace engine::assets::phlosion {
struct AuthoredSceneDocument;
}

namespace game::editor::persistence {

struct PreviewUnitRecord {
    std::string speciesName;
    bool playerSide = false;
    bool benchUnit = false;
    int boardColumn = 0;
    int boardRow = 0;
    int benchSlot = 0;
    layout_transactions::PreviewUnitTransform transform;
};

class Store {
public:
    Store();
    explicit Store(std::filesystem::path projectRoot);
    ~Store();

    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    void setProjectRoot(std::filesystem::path projectRoot);

    bool loadPreviewUnitOverrides(std::string* outError = nullptr);
    bool hasPreviewUnitOverride(
        std::string_view previewId,
        std::string_view unitKey) const;
    bool applyPreviewUnitOverride(
        std::string_view previewId,
        std::string_view unitKey,
        layout_transactions::PreviewUnitTransform& inOutTransform) const;
    bool savePreviewUnitOverride(
        std::string_view previewId,
        std::string_view unitKey,
        const PreviewUnitRecord& record,
        std::string* outError = nullptr);
    bool resetPreviewUnitOverride(
        std::string_view previewId,
        std::string_view unitKey,
        std::string* outError = nullptr);

    bool saveBoardRegistration(
        bool sceneMounted,
        const game::runtime::lgpe_route1_runtime::BoardLayoutTransform&
            layout,
        std::string* outError = nullptr) const;
    bool saveAuthoredScene(
        bool sceneMounted,
        const std::filesystem::path& destination,
        const engine::assets::phlosion::AuthoredSceneDocument& document,
        std::string* outError = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::editor::persistence
