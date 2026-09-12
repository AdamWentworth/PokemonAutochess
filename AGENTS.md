# Renderer parity

OpenGL, Vulkan, and Direct3D 12 are equal supported targets. This is a standing
user requirement. Rendering work is incomplete until the affected behavior is
verified on all three native APIs, including the editor preview when relevant.

- Keep gameplay, visibility, animation timing, scene content, materials, and UI
  consistent. Prefer shared presentation code; backend optimizations must preserve
  the same result. Missing features or a silent fallback are failures.
- Add affected scenes/phases to the visual parity matrix and run those cases on
  all three APIs at matching gameplay frames, with expected-content checks.
- Run the relevant CPU contract tests too. A build, startup PASS, or one/two API
  screenshots alone cannot establish visual parity. Report unverified coverage
  explicitly; do not loosen thresholds to hide a regression.
- Follow [the parity contract](docs/RENDERER_PARITY_CONTRACT.md) and
  [the test plan](docs/TEST_PLAN.md). Full GPU qualification remains local until
  a representative GPU runner is available.
