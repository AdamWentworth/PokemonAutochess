#pragma once

#include "game/runtime/EditorPreviewUnit.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>

struct GameContext;
struct InputEvent;

struct GameDataDb;

namespace game {

/**
 * Holds the running game instance (world/state/systems/ui/logging).
 * Constructed by GameBootstrap; driven by GameRuntime.
 *
 * Design goal: can be constructed in tests with a fake/Headless GameContext.
 */
class GameSession {
public:
    GameSession(GameContext& ctx, GameDataDb db);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    GameSession(GameSession&&) noexcept;
    GameSession& operator=(GameSession&&) noexcept;

    void handleEvent(const InputEvent& event);
    void fixedUpdate(float dt);
    void render(int drawableW, int drawableH);
    bool activateEditorPreview(
        const std::string& state,
        const std::string& gameMode,
        const std::string& snapshotPath,
        std::string* outError = nullptr);
    std::size_t editorPreviewUnitCount() const noexcept;
    bool editorPreviewUnit(
        std::size_t index,
        game::runtime::EditorPreviewUnit& outUnit);
    bool setEditorPreviewUnitTransform(
        int unitId,
        const std::array<float, 3>& position,
        const std::array<float, 3>& rotationDegrees,
        bool snapToGameplaySlot = true);
    void setEditorBoardCellSize(float cellSize);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game
