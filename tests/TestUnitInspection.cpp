#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/runtime/ui/UnitDetailsHud.h"
#include "game/runtime/session/SessionLoopBridge.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/arena/AuthoredCombatMap.h"
#include <glm/gtc/matrix_transform.hpp>
#include <string>

bool test_unit_inspection_contract(std::string &fail) {
    GameConfigData config;
    GameWorld world(config);
    world.setRenderEnabled(false);
    PokemonInstance ally;
    ally.id = 101;
    ally.name = "bulbasaur";
    ally.types = {"grass", "poison"};
    ally.position = world.gridToWorld(2, 5);
    world.getPokemons().push_back(ally);
    PokemonInstance enemy = ally;
    enemy.id = 102;
    enemy.name = "rattata";
    enemy.side = PokemonSide::Enemy;
    enemy.position = world.gridToWorld(5, 2);
    world.getPokemons().push_back(enemy);
    Camera3D camera(45, 1280.0f / 720, .1f, 100);
    camera.setPosition({0, 12, 14});
    camera.lookAt({0, 0, 0});
    UnitInteractionSystem interaction(&camera, &world, 1280, 720);
    const auto click = [&](const glm::vec3 &position, InputEvent::MouseButton button) {
        const auto screen = glm::project(position, camera.getViewMatrix(), camera.getProjectionMatrix(), glm::vec4(0, 0, 1280, 720));
        InputEvent event;
        event.type = InputEvent::Type::MouseDown;
        event.mouseButtonId = button;
        event.mouseX = static_cast<int>(std::round(screen.x));
        event.mouseY = static_cast<int>(std::round(720 - screen.y));
        interaction.handleInput(event);
    };
    world.setBoardInteractionLocked(true);
    click(enemy.position, InputEvent::MouseButton::Right);
    if (!world.inspectedUnit() || world.inspectedUnit()->id != enemy.id || world.isUnitDragActive()) {
        fail = "Right-click must inspect a visible enemy during combat without picking it up.";
        return false;
    }
    game::arena::ArenaMapData map;
    for (int z = 0; z < config.rows; ++z)
        for (int x = 0; x < config.cols; ++x) {
            map.tiles[{x, z}] = {x, z, 0, 0, 0};
            map.playableCells.insert({x, z});
        }
    map.cover.push_back({"brush", {{{{500, 100}, {800, 100}, {800, 600}, {500, 600}}}}});
    world.setCombatMapRules(std::make_shared<game::arena::AuthoredCombatMap>(map, game::arena::Cell{0, 0}));
    if (world.inspectedUnit()) {
        fail = "A selected enemy entering cover must stop revealing live stats.";
        return false;
    }
    game::runtime::session_loop_runtime::PauseState pause;
    game::runtime::session_loop_bridge::handleEvent(
        InputEvent::KeyDownEvent(InputEvent::Key::Num0), {.pauseState = &pause, .gameWorld = &world});
    world.findUnitById(enemy.id)->coverRevealRemainingSec = 1;
    if (world.inspectedUnit()) {
        fail = "Clear selection must also forget a concealed selection before it becomes visible again.";
        return false;
    }
    world.findUnitById(enemy.id)->coverRevealRemainingSec = 0;
    click(enemy.position, InputEvent::MouseButton::Right);
    if (world.inspectedUnit()) {
        fail = "Hidden enemies cannot be selected through the inspection picker.";
        return false;
    }
    world.findUnitById(enemy.id)->coverRevealRemainingSec = 1;
    click(enemy.position, InputEvent::MouseButton::Right);
    if (!world.inspectedUnit()) {
        fail = "An enemy revealed by attacking can be inspected.";
        return false;
    }
    click(ally.position, InputEvent::MouseButton::Left);
    if (!world.inspectedUnit() || world.inspectedUnit()->id != ally.id || world.isUnitDragActive()) {
        fail = "A locked board still permits inspecting an ally without changing its position.";
        return false;
    }
    world.findUnitById(ally.id)->hp = 42;
    if (world.inspectedUnit()->hp != 42) {
        fail = "The selected stats must reflect live HP.";
        return false;
    }
    world.getBenchPokemons().push_back(world.getPokemons().front());
    world.getPokemons().erase(world.getPokemons().begin());
    if (!world.inspectedUnit() || world.inspectedUnit()->id != ally.id) {
        fail = "Selection must follow the unit ID when it moves to the bench.";
        return false;
    }
    world.findUnitById(ally.id)->alive = false;
    if (world.inspectedUnit()) {
        fail = "Fainted units must stop exposing the stats panel.";
        return false;
    }
    world.getBenchPokemons().clear();
    if (world.inspectedUnit()) {
        fail = "Removing the selected unit must not leave a stale pointer.";
        return false;
    }
    world.inspectUnit(enemy.id);
    world.resetForNewGame();
    if (world.inspectedUnit()) {
        fail = "A new game must clear inspection.";
        return false;
    }
    world.getPokemons().push_back(ally);
    click(ally.position, InputEvent::MouseButton::Left);
    if (!world.inspectedUnit() || world.inspectedUnit()->id != ally.id || !world.isUnitDragActive()) {
        fail = "Planning pickup must keep working while opening the unit's stats.";
        return false;
    }
    if (game::runtime::unit_details_hud::fraction(20, 0) != 0 || game::runtime::unit_details_hud::fraction(200, 100) != 1) {
        fail = "HP and energy bars must handle empty and overfilled limits.";
        return false;
    }
    return true;
}
