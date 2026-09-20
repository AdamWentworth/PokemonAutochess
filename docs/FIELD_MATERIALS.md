# Field materials

Status: Active
Type: Reference
Last updated: 2026-09-19

Autochess owns the field-surface shader implementations and their tuning under
`src/game/render/materials/field`. The profile manifest is
`config/render/world_materials.json`. The game selects it before creating its
renderer; `phlosion.project.json` selects the same profile for editor previews.
Tile Tools remains disabled and is unrelated to these rendering materials.
The same profile composes the project-owned [character materials](CHARACTER_MATERIALS.md).

## Ownership and names

These profiles reproduce the recovered Pokemon Let's Go Pikachu/Eevee (LGPE)
field-material behavior used by the published Route 1 environments. Their
descriptive names identify rendering roles; they do not imply that the tuning
is a universal engine default. Recovery evidence and extraction tools remain
in the private `PokemonSwitchAssetResearch` workspace.

The recovered implementations are excluded from the repository's MIT grant;
see [licensing scope](LICENSING.md). Repository ownership here describes code
placement, not ownership of the recovered source material.

| Material role | Original material family | Preserved mode IDs |
| --- | --- | --- |
| Field ground and cliff | FieldGroundShader01 / FieldCliffShader01 | 4, 5 |
| Canopy | FieldTreeShader05 and reviewed foliage variants | 6, 21, 22, 25 |
| Tree trunk | FieldObjectShader tree-miki | 7 |
| Layered foliage | FieldTreeShader02 and reviewed foliage/shrub variants | 8, 19, 23, 24, 26 |
| Field grass | FieldGrassShader02 / FieldGrassShader01 | 9, 10 |
| Ground cover | FieldGrassShader04 / FieldGrassShader05 | 11, 12 |
| Ground overlays | Roadstone / rock-mask | 13, 14 |
| Flowers | FieldFlowerShader / build-model variant | 15, 20 |
| Rock | FieldRockShader | 16 |
| Painted surface | FieldObjectShader signboard | 17 |
| Encounter grass | FieldEncGrassShader01 | 18 |

Mode numbers retain compatibility with published game content. Source identity
strings remain in the published-environment adapter where they select these
profiles. The game also owns the CPU material oracles and their tests.

## Shader profile

OpenGL and D3D12 combine the engine's world interface with game declarations
and evaluation. Vulkan compiles six project artifacts: direct and indirect
vertices, and fragments with standard and dual-source blending.
`PAC_WorldMaterialShaders` builds these into `.phlosion/materials/world`;
game and editor-plugin builds depend on it.
Configuration also copies the OpenGL/D3D12 snippets there. Release bundles ship
this runtime directory alongside the manifest, without requiring a source checkout.

The manifest also owns the D3D12 scalar packing rules required by the fixed
world constant layout. Rules refer to named material fields and shader constants;
the engine validates them and copies current draw values, including the camera.
No field-material mode selection or packing rules remain in the engine.

All files are required for an activated profile. Missing files, unknown constant
names, incomplete Vulkan variants, and shader-interface version mismatches fail
explicitly. Shader changes require rebuilding the game artifacts; the editor
reloads the selected profile when reopening the project. Engine shader caches
for OpenGL and D3D12 include the composed source.

## Verification

The engine's semantic boundary has no field-material allowlist. Its independent
profile contracts cover default behavior, source ownership, dynamic parameter
mapping, required variants, and invalid manifests. The game's existing material
oracles and D3D12 constants tests retain their numerical expectations.

Rendering changes require the affected native and editor cases on OpenGL,
Vulkan, and D3D12. Vulkan's direct compatibility path must be exercised in
addition to indirect submission. See [the test plan](TEST_PLAN.md).
