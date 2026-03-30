# Asset Layout

Runtime-resolved assets should live in the canonical folder that code and
config already point to:

- `assets/models/` for unit/model content
- `assets/meshes/` for runtime-resolved effect meshes
- `assets/textures/` for runtime-resolved textures
- `assets/shaders/` for runtime shaders

`assets/vfx/` is for reusable/reference/staging VFX content and organization.
Do not treat it as the default runtime destination when an effect already
expects concrete paths under `assets/meshes/` or `assets/textures/`.
