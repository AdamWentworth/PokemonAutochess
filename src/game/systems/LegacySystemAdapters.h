// src/game/systems/LegacySystemAdapters.h
#pragma once

#include <functional>
#include <utility>

#include "engine/core/Updatable.h"
#include "engine/core/ecs/ISystem.h"

namespace game {

// Adapter to run legacy Updatable systems through the ECS scheduler.
class UpdatableSystemAdapter final : public engine::ecs::ISystem {
public:
    explicit UpdatableSystemAdapter(Updatable* target, const char* debugName = "updatable")
        : target_(target), debugName_(debugName) {}

    const char* debugName() const override { return debugName_; }

    void update(engine::ecs::World& /*world*/, float dt) override {
        if (target_) target_->update(dt);
    }

private:
    Updatable* target_ = nullptr; // non-owning
    const char* debugName_ = "updatable";
};

// Adapter for callback-based updates (state manager, world, UI).
class CallbackSystemAdapter final : public engine::ecs::ISystem {
public:
    explicit CallbackSystemAdapter(std::function<void(float)> fn,
                                   const char* debugName = "callback")
        : fn_(std::move(fn)), debugName_(debugName) {}

    const char* debugName() const override { return debugName_; }

    void update(engine::ecs::World& /*world*/, float dt) override {
        if (fn_) fn_(dt);
    }

private:
    std::function<void(float)> fn_;
    const char* debugName_ = "callback";
};

} // namespace game
