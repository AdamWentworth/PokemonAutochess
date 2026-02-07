# Architecture Rules (Engine-first, Game-owned loop)

This file is a concise rule list. Detailed targets are in `TARGET_ARCHITECTURE.md`.
Current progress tracking is in `ROADMAP.md`.

## Non-negotiables

1. **Strict layering**
   - `game` may depend on `engine_*`.
   - `engine_*` must never include or depend on `game` code.

2. **No mutable globals in runtime**
   - No `getInstance()`, no `setActive()`, no default singleton service state in `engine_core` or `game`.
   - Allowed: `constexpr`, pure functions, immutable tables.
   - Tooling (`engine_tools`) may use process-lifetime objects.

3. **Headless-capable core**
   - `engine_core` must not depend on SDL/OpenGL/GLAD/etc.
   - Core simulation must run in tests without window/renderer.

4. **ECS + scheduler**
   - ECS types live in `engine_core`.
   - Game configures system list and ordering (engine provides the scheduler).

5. **Lua boundary**
   - Lua scripts talk to a stable `ScriptAPI` (commands/events).
   - Lua must not get direct pointers/references to internal engine/game objects.

6. **Assets**
   - Runtime reads cooked packages via `IAssetStore`.
   - JSON remains the authoring format; cooker validates + packs.

7. **No optional service dependencies in runtime**
   - Gameplay states/systems require dependencies via references (no nullable service pointers).
   - No fallback constructors or local config loads in runtime.

## Composition root
- The **game** owns the loop and constructs services.
- A "convenience runner" may exist, but it must not be required by engine clients.
- Any convenience runner must live outside `engine_core` and may depend on platform/render.
