// src/engine/core/ecs/World.h

#pragma once
#include "engine/core/ecs/Entity.h"
#include "engine/core/ecs/ComponentStorage.h"
#include "engine/core/ecs/View.h"
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <cstdint>

namespace engine { struct CoreServices; }

namespace engine::ecs {

// World owns entity lifetimes and component storages.
// This is a starter design intended for correctness + headless tests first.
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

    // Convenience iteration: each component instance of T.
    // Passes (Entity, T&) to fn.
    template <class T, class Fn>
    void each(Fn&& fn) {
        auto& store = components<T>();
        for (auto& kv : store.raw()) {
            const std::uint32_t id = kv.first;
            Entity e{ id, id < generations_.size() ? generations_[id] : std::uint8_t{0} };
            fn(e, kv.second);
        }
    }

    // Convenience iteration: join of two components (A,B) by entity id.
    // Passes (Entity, A&, B&) to fn.
    template <class A, class B, class Fn>
    void each2(Fn&& fn) {
        auto& a = components<A>().raw();
        auto& bStore = components<B>();

        for (auto& kv : a) {
            const std::uint32_t id = kv.first;
            Entity e{ id, id < generations_.size() ? generations_[id] : std::uint8_t{0} };
            if (auto* b = bStore.get(Entity{id, 0}); b) {
                fn(e, kv.second, *b);
            }
        }
    }

    // Canonical query API (starter): iterate entities that have all requested components.
    // This replaces ad-hoc raw-map access patterns over time.
    template <class... Cs, class Fn>
    void for_each(Fn&& fn) {
        detail::view_tuple<Cs...> tup{ std::tuple<detail::storage_ref<Cs>...>{ detail::storage_ref<Cs>{ &components<Cs>() }... } };
        View<Cs...> v(tup);
        v.each([&](Entity e, Cs&... comps) {
            // Optional safety: skip dead entities if callers stored stale ids.
            if (!alive(e)) return;
            fn(e, comps...);
        });
    }

private:
    struct IStoreHolder {
        virtual ~IStoreHolder() = default;
        virtual void removeEntity(std::uint32_t id) = 0;
    };

    template <class T>
    struct StoreHolder final : IStoreHolder {
        ComponentStorage<T> store;
        void removeEntity(std::uint32_t id) override { store.removeById(id); }
    };

    void removeAllComponentsFor(std::uint32_t id);

    engine::CoreServices* services_ = nullptr;

    // Entity bookkeeping
    std::vector<std::uint8_t> generations_;
    std::vector<std::uint32_t> freeIds_;

    // Type-erased component stores
    std::unordered_map<std::type_index, std::unique_ptr<IStoreHolder>> stores_;
};

} // namespace engine::ecs