// CameraSystem.cpp

#include "CameraSystem.h"
#include "../../engine/events/EventManager.h"
#include "../../engine/events/Event.h"
#include <iostream>

static const char* kCameraLua = "scripts/systems/camera.lua";

CameraSystem::CameraSystem(Camera3D* cam)
    : camera(cam)
{
    // Initialize Lua VM
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    // Expose camera controls to Lua
    lua.set_function("cam_move", [this](float dx, float dy, float dz){
        camera->move({dx, dy, dz});
    });
    lua.set_function("cam_zoom", [this](float delta){
        camera->zoom(delta);
    });

    loadScript();

    // If you centralize events elsewhere, delete this block and call onMouse* from there
    EventManager::getInstance().subscribe(EventType::MouseButtonDown,
        [this](const Event& e){
            const auto& ev = static_cast<const MouseButtonDownEvent&>(e);
            onMouseDown(ev.getX(), ev.getY());
        });

    EventManager::getInstance().subscribe(EventType::MouseButtonUp,
        [this](const Event& e){
            const auto& ev = static_cast<const MouseButtonUpEvent&>(e);
            onMouseUp(ev.getX(), ev.getY());
        });

    EventManager::getInstance().subscribe(EventType::MouseMoved,
        [this](const Event& e){
            const auto& ev = static_cast<const MouseMotionEvent&>(e);
            onMouseMove(ev.getX(), ev.getY());
        });
}

void CameraSystem::loadScript() {
    sol::load_result chunk = lua.load_file(kCameraLua);
    if (!chunk.valid()) {
        sol::error err = chunk; // <-- key change
        std::cerr << "[CameraSystem] load error: " << err.what() << "\n";
        ok = false; return;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error err = r; // <-- key change
        std::cerr << "[CameraSystem] exec error: " << err.what() << "\n";
        ok = false; return;
    }
    if (sol::function init = lua["camera_init"]; init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            sol::error err = ir; // <-- key change
            std::cerr << "[CameraSystem] camera_init error: " << err.what() << "\n";
            ok = false; return;
        }
    }
    ok = true;
}

void CameraSystem::update(float dt) {
    if (!ok) return;
    if (sol::function f = lua["camera_update"]; f.valid()) {
        sol::protected_function_result r = f(dt);
        if (!r.valid()) {
            sol::error err = r; // <-- key change
            std::cerr << "[CameraSystem] camera_update error: " << err.what() << "\n";
            ok = false;
        }
    }
}

void CameraSystem::handleZoom(const SDL_Event& event) {
    if (event.type == SDL_MOUSEWHEEL) onMouseWheel(event.wheel.y);
}

void CameraSystem::onMouseDown(int x, int y) { if (ok) if (auto f = lua["camera_mouse_down"]; f.valid()) f(x,y); }
void CameraSystem::onMouseUp  (int x, int y) { if (ok) if (auto f = lua["camera_mouse_up"  ]; f.valid()) f(x,y); }
void CameraSystem::onMouseMove(int x, int y) { if (ok) if (auto f = lua["camera_mouse_move"]; f.valid()) f(x,y); }
void CameraSystem::onMouseWheel(int wy)      { if (ok) if (auto f = lua["camera_mouse_wheel"]; f.valid()) f(wy); }
