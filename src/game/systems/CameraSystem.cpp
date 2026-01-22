// CameraSystem.cpp

#include "CameraSystem.h"
#include "engine/input/InputEvent.h"

#include <iostream>

static const char* kCameraLua = "scripts/systems/camera.lua";

CameraSystem::CameraSystem(Camera3D* cam)
    : camera(cam)
{
    // Initialize Lua VM
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    // Expose camera controls to Lua
    lua.set_function("cam_move", [this](float dx, float dy, float dz) {
        camera->move({ dx, dy, dz });
    });
    lua.set_function("cam_zoom", [this](float delta) {
        camera->zoom(delta);
    });
    lua.set_function("cam_orbit", [this](float yawDeltaRad, float pitchDeltaRad) {
        camera->orbit(yawDeltaRad, pitchDeltaRad);
    });

    loadScript();
}

void CameraSystem::loadScript() {
    sol::load_result chunk = lua.load_file(kCameraLua);
    if (!chunk.valid()) {
        sol::error err = chunk;
        std::cerr << "[CameraSystem] load error: " << err.what() << "\n";
        ok = false;
        return;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error err = r;
        std::cerr << "[CameraSystem] exec error: " << err.what() << "\n";
        ok = false;
        return;
    }

    if (sol::function init = lua["camera_init"]; init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            sol::error err = ir;
            std::cerr << "[CameraSystem] camera_init error: " << err.what() << "\n";
            ok = false;
            return;
        }
    }

    ok = true;
}

void CameraSystem::update(float dt) {
    if (!ok) return;

    if (sol::function f = lua["camera_update"]; f.valid()) {
        sol::protected_function_result r = f(dt);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[CameraSystem] camera_update error: " << err.what() << "\n";
            ok = false;
        }
    }
}

void CameraSystem::handleInput(const InputEvent& event) {
    // Centralized InputEvent -> Lua routing.
    // This removes the hidden dependency on a global EventManager singleton.
    switch (event.type) {
        case InputEvent::Type::MouseDown:
            onMouseDown(event.mouseX, event.mouseY, event.mouseButton);
            break;
        case InputEvent::Type::MouseUp:
            onMouseUp(event.mouseX, event.mouseY, event.mouseButton);
            break;
        case InputEvent::Type::MouseMove:
            onMouseMove(event.mouseX, event.mouseY);
            break;
        case InputEvent::Type::MouseWheel:
            onMouseWheel(event.wheelY);
            break;
        default:
            break;
    }
}

void CameraSystem::onMouseDown(int x, int y, int button) {
    if (!ok) return;
    if (auto f = lua["camera_mouse_down"]; f.valid()) {
        f(x, y, button);
    }
}

void CameraSystem::onMouseUp(int x, int y, int button) {
    if (!ok) return;
    if (auto f = lua["camera_mouse_up"]; f.valid()) {
        f(x, y, button);
    }
}

void CameraSystem::onMouseMove(int x, int y) {
    if (!ok) return;
    if (auto f = lua["camera_mouse_move"]; f.valid()) {
        f(x, y);
    }
}

void CameraSystem::onMouseWheel(int wy) {
    if (!ok) return;
    if (auto f = lua["camera_mouse_wheel"]; f.valid()) {
        f(wy);
    }
}
