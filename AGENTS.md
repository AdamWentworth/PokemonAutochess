# Project orientation

This repository owns the game. Reusable runtime, rendering and VFX components belong to PhlosionEngine;
game move presets and bindings belong here.

Read documentation progressively according to the task:

- Setup, build targets, and repository layout: [README](README.md).
- Game/engine/package ownership: [project boundaries](docs/PROJECT_BOUNDARIES.md).
- Editor and scene work: [editor scene model](docs/EDITOR_SCENE_MODEL.md).
- Renderer settings: [renderer configuration](docs/RENDERER_CONFIGURATION.md).
- Verification: [test plan](docs/TEST_PLAN.md), selecting the affected checks.

Start with the affected subsystem and expand as its dependencies require. Do not
load every linked document for each task. The renderer requirements below remain
mandatory for rendering changes.

# Renderer parity

The project renderer enrollment in `phlosion.project.json` is authoritative.
Windows qualifies Vulkan (preferred) and Direct3D 12; Linux targets Vulkan.
OpenGL is shelved and excluded from game release gates. Verify affected behavior
on every enrolled API for the qualified host, including editor preview when relevant.

- Keep gameplay, visibility, animation timing, scene content, materials, and UI
  consistent. Prefer shared presentation code; backend optimizations must preserve
  the same result. Missing features or a silent fallback are failures.
- Add affected scenes/phases to the visual parity matrix and run those cases on
  all enrolled APIs at matching gameplay frames, with expected-content checks.
- Run the relevant CPU contract tests too. A build, startup PASS, or a partial set of API
  screenshots alone cannot establish visual parity. Report unverified coverage
  explicitly; do not loosen thresholds to hide a regression.
- Follow [the parity contract](docs/RENDERER_PARITY_CONTRACT.md) and
  [the test plan](docs/TEST_PLAN.md). Full GPU qualification remains local until
  a representative GPU runner is available.
