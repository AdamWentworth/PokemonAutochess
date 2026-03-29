// CameraSystem.cpp

#include "CameraSystem.h"
#include "game/systems/CameraPanClamp.h"
#include "engine/input/InputEvent.h"
#include "engine/core/IAssetStore.h"
#include "game/GameServices.h"
#include "engine/core/Paths.h"

#include <iostream>
#include <algorithm>

namespace {
std::string normalizeVirtualPath(std::string path) {
    std::string root = engine::paths::dataRoot();
    std::replace(root.begin(), root.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    if (!root.empty() && path.rfind(root + "/", 0) == 0) {
        path = path.substr(root.size() + 1);
    }
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.erase(path.begin());
    }
    return path;
}

bool loadLuaFromStore(sol::state& lua, const std::string& path, engine::IAssetStore& assets, std::string& outErr) {
    std::string text;
    std::string err;
    const std::string virt = normalizeVirtualPath(path);
    if (!assets.readText(virt, text, &err)) {
        outErr = err.empty() ? ("Failed to read " + virt) : err;
        return false;
    }
    sol::load_result chunk = lua.load(text);
    if (!chunk.valid()) {
        sol::error e = chunk;
        outErr = e.what();
        return false;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        outErr = e.what();
        return false;
    }
    return true;
}
} // namespace

static const char* kCameraLua = "scripts/systems/camera.lua";

static int toButtonNumber(InputEvent::MouseButton b) {
    // Keep numeric convention stable for Lua scripts (SDL-style 1..5),
    // but without exposing SDL headers/types in gameplay.
    switch (b) {
        case InputEvent::MouseButton::Left:   return 1;
        case InputEvent::MouseButton::Middle: return 2;
        case InputEvent::MouseButton::Right:  return 3;
        case InputEvent::MouseButton::X1:     return 4;
        case InputEvent::MouseButton::X2:     return 5;
        default: return 0;
    }
}

CameraSystem::CameraSystem(Camera3D* cam, GameServices& svc)
    : camera(cam), services(svc)
{
    // Initialize Lua VM
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    // Expose camera controls to Lua
    lua.set_function("cam_move", [this](float dx, float dy, float dz) {
        camera->move({ dx, dy, dz });
        const auto panBounds = game::camera_pan_clamp::buildBoardSafeBounds(
            services.config.cols,
            services.config.rows,
            services.config.cellSize);
        game::camera_pan_clamp::clampCameraPan(*camera, panBounds);
    });
    lua.set_function("cam_pan_screen", [this](float dx, float dy, float scale) {
        camera->panPlanar(dx, dy, scale);
        const auto panBounds = game::camera_pan_clamp::buildBoardSafeBounds(
            services.config.cols,
            services.config.rows,
            services.config.cellSize);
        game::camera_pan_clamp::clampCameraPan(*camera, panBounds);
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
    std::string loadError;
    if (!loadLuaFromStore(lua, kCameraLua, services.assets, loadError)) {
        std::cerr << "[CameraSystem] load error: " << loadError << "\n";
        ok = false;
        return;
    }

    if (sol::function init = lua["camera_init"]; init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            sol::error initError = ir;
            std::cerr << "[CameraSystem] camera_init error: " << initError.what() << "\n";
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
    // Centralized InputEvent -> Lua routing (explicit input wiring).
    switch (event.type) {
        case InputEvent::Type::MouseDown:
            onMouseDown(event.mouseX, event.mouseY, toButtonNumber(event.mouseButtonId));
            break;
        case InputEvent::Type::MouseUp:
            onMouseUp(event.mouseX, event.mouseY, toButtonNumber(event.mouseButtonId));
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
