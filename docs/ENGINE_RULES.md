# Engine Rules (Non-Negotiables)

These rules protect engine reuse and keep the project maintainable.

- Strict layering: `game` can depend on `engine_*`, never the other way around.
- Headless core: `engine_core` must not depend on SDL/OpenGL/GLAD.
- No mutable runtime globals or singletons.
- Game-owned loop and composition root.
- One authoritative update graph (scheduler).
- Lua interacts through a narrow ScriptAPI, not raw pointers.
- Runtime reads cooked content through an asset store.
- Large subsystems live in their own .cpp (avoid mega .inl includes).
