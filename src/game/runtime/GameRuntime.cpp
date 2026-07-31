// src/game/GameRuntime.cpp

#include "game/runtime/GameRuntime.h"

// Keep GameRuntime minimal: it owns the game loop entrypoints and delegates to GameSession.
// Composition/wiring lives in GameBootstrap.
#include <memory>

#include "engine/core/GameContext.h"
#include "engine/input/InputEvent.h"

#include "game/runtime/GameBootstrap.h"
#include "game/runtime/session/GameSession.h"

struct GameRuntime::Impl {
    std::unique_ptr<game::GameSession> session;

    void init(GameContext& ctx) { session = game::GameBootstrap::create(ctx); }
    void handleEvent(const InputEvent& event) { if (session) session->handleEvent(event); }
    void fixedUpdate(float dt) { if (session) session->fixedUpdate(dt); }
    void render(int drawableW, int drawableH) { if (session) session->render(drawableW, drawableH); }
    bool activateEditorPreview(
        const std::string& state,
        const std::string& gameMode,
        const std::string& snapshotPath,
        std::string* outError) {
        if (!session) {
            if (outError) {
                *outError = "Game runtime is not initialized.";
            }
            return false;
        }
        return session->activateEditorPreview(
            state,
            gameMode,
            snapshotPath,
            outError);
    }
    void shutdown() { if (session) session->shutdown(); session.reset(); }
};

GameRuntime::GameRuntime()
    : impl_(std::make_unique<Impl>()) {}

GameRuntime::~GameRuntime() = default; // out-of-line: Impl is complete here

GameRuntime::GameRuntime(GameRuntime&&) noexcept = default;
GameRuntime& GameRuntime::operator=(GameRuntime&&) noexcept = default;

void GameRuntime::init(GameContext& ctx) { impl_->init(ctx); }
void GameRuntime::handleEvent(const InputEvent& event) { impl_->handleEvent(event); }
void GameRuntime::fixedUpdate(float dt) { impl_->fixedUpdate(dt); }
void GameRuntime::render(int drawableW, int drawableH) { impl_->render(drawableW, drawableH); }
bool GameRuntime::activateEditorPreview(
    const std::string& state,
    const std::string& gameMode,
    const std::string& snapshotPath,
    std::string* outError) {
    return impl_->activateEditorPreview(
        state,
        gameMode,
        snapshotPath,
        outError);
}
void GameRuntime::shutdown() { impl_->shutdown(); }

