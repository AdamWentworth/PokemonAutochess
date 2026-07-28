# Engine Rules (Non-Negotiables)

Status: Active
Type: Rule
Last updated: 2026-07-27

These rules protect engine reuse and keep the project maintainable.

- Strict layering: `game` can depend on `engine_*`, never the other way around.
- Headless core: `engine_core` must not depend on SDL/OpenGL/GLAD.
- No mutable runtime globals or singletons.
- Game-owned loop and composition root.
- Renderer API routing is selected once in `GameSession`; gameplay states consume route helpers from `GameServices` instead of querying backend-specific preferences directly.
- One authoritative update graph (scheduler).
- Lua interacts through a narrow ScriptAPI, not raw pointers.
- Runtime reads cooked content through an asset store.
- LGPE source formats are decoded by reproducible offline tools; proprietary
  source parsing and conversion must not be hidden in the shipping frame loop.
- Preserve source attributes and unknown evidence through the import boundary;
  a cache or renderer limitation is not permission to discard them.
- LGPE environment changes are limited by
  `LGPE_ENVIRONMENT_FIDELITY_CONTRACT.md`.
- Large subsystems live in their own .cpp (avoid mega .inl includes).
