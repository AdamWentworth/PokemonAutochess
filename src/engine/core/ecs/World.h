// src/engine/core/ecs/World.h

#pragma once
#include "engine/core/ecs/Entity.h"
#include "engine/core/ecs/ComponentStorage.h"
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace engine { struct CoreServices; }

namespace engine::ecs {

// World owns entity lifetimes and component storages.
// This is a starter "good enough" design to begin migration + headless tests.
class World {
public:
    explicit World(engine::CoreServices* services = nullptr);

    Entity create();
    void destroy(Entity e);
    bool alive(Entity e) const;

    engine::CoreServices* services() { return services_; }
    const engine::CoreServices* services() const { return services_; }

    template <class T>
    ComponentStorage<T>& components() {
        const std::type_index key(typeid(T));
        auto it = stores_.find(key);
        if (it == stores_.end()) {
            auto ptr = std::make_unique<StoreHolder<T>>();
            auto* raw = &ptr->store;
            stores_.emplace(key, std::move(ptr));
            return *raw;
        }
        return static_cast<StoreHolder<T>*>(it->second.get())->store;
    }

private:
    struct IStoreHolder { virtual ~IStoreHolder() = default; };

    template <class T>
    struct StoreHolder final : IStoreHolder { ComponentStorage<T> store; };

    engine::CoreServices* services_ = nullptr;

    // Entity bookkeeping
    std::vector<std::uint8_t> generations_;
    std::vector<std::uint32_t> freeIds_;

    // Type-erased component stores
    std::unordered_map<std::type_index, std::unique_ptr<IStoreHolder>> stores_;
};

} // namespace engine::ecs
