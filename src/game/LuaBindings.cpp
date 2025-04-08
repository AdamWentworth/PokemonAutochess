// LuaBindings.cpp

#include "LuaBindings.h"
#include "GameWorld.h"
#include "../engine/render/Model.h"
#include "../engine/render/Camera3D.h"
#include <glm/glm.hpp>

void registerLuaBindings(sol::state& lua, GameWorld* world) {
    lua.set_function("spawnPokemon", [world](const std::string& name, float x, float y, float z) {
        world->spawnPokemon(name, glm::vec3(x, y, z));
    });

    // You can register more game-related Lua functions here later.
}
