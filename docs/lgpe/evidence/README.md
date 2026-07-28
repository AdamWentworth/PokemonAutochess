# LGPE Direct-Source Evidence

Status: Active
Type: Evidence
Last updated: 2026-07-28

`route1_direct_source_manifest.json` is the promoted output of the first Route
1 direct-import pass. It was generated from the user's locally unpacked
GFBMDL and BNTX files; no DAE, GLB, Blender file, or PACMDL cache participates
in the canonical read path.

The manifest contains metadata and hashes, not proprietary vertex or texture
payloads. It proves the current decoder can deterministically enumerate:

- 38 meshes, 65 polygon groups, and 63 bones;
- 21 materials with source shader groups, parameters, samplers, and flags;
- 39 required textures with unambiguous BNTX ownership;
- 88,618 source triangle records;
- 88,480 unique material-indexed triangles and 138 retained exact duplicate
  records.

The duplicate count explains the complete polygon-count difference from the
earlier DAE/Blender extraction. It is recorded explicitly rather than silently
discarded.

Reproduce and validate from the repository root:

```powershell
.\tools\lgpe_importer\export_lgpe_source_manifest.ps1 `
  -OutputPath .\docs\lgpe\evidence\route1_direct_source_manifest.json

.\tools\lgpe_importer\validate_lgpe_source_manifest.ps1 `
  -ManifestPath .\docs\lgpe\evidence\route1_direct_source_manifest.json
```

See `tools/lgpe_importer/README.md` for local input defaults, overrides, and
the boundaries of this pass.

`route1_canonical_scene_report.json` is the promoted result of the second
pass. It records hashes and sizes for the locally generated transparent
canonical directory without committing its proprietary geometry or texture
payloads. The corresponding engine inspection proves that the compiled loader
accepts the real Route 1 output and recovers the same source counts.

Reproduce that evidence with:

```powershell
.\tools\lgpe_importer\cook_route1_canonical_scene.ps1
.\tools\lgpe_importer\validate_lgpe_canonical_scene.ps1
.\build-vs2022\Debug\PAC_LgpeInspect.exe cache/lgpe/route1
```

`route1_world_scene_report.json` is the promoted result of the third pass. It
records the deterministic canonical-to-`WorldScene` adaptation: all 65
polygon groups remain registered, while the six groups using the two authored
`SkipMainRendering` shadow-only materials are excluded from the main-pass
frame. It also records the declared secondary vertex channels and exact
material-family distribution.

`PAC_LgpeInspect` performs both the canonical load and this WorldScene
adaptation against the real local cache. The proprietary payload remains under
`cache/` and is not committed.

`route1_field_ground_shader01_report.json` records the fourth-pass vertical
slice for Route 1's shared lawn/ledge material. The exact vertex/fragment BNSH
pair, material metadata, and independent reflection establish a six-texture
surface stack using UV0, UV2, vertex color, and `Alpha_light`. Material mode 4
implements that surface contract across OpenGL, D3D12, Vulkan direct, and
Vulkan indirect rendering.

The canonical adapter now preserves every valid array-0/depth-0 authored mip
level and all three rendering backends upload those exact levels. The real
Route 1 cache reports 365 uploaded mip levels across 39 textures, with as many
as 11 levels on one texture. The six `FieldGroundShader01` roles carry
9, 9, 10, 10, 11, and 9 authored levels respectively.

`route1_field_cliff_shader01_report.json` records the fifth-pass ledge-face
slice. Both Route 1 cliff materials use byte-identical embedded vertex and
fragment programs. Their recovered surface contract adds source UV1 to the GPU
stream and binds the authored cliff face, two ground textures, 0.3-scale
ground mixer, and UV2 border atlas. The atlas alpha selects the cliff-to-grass
transition while its RGB tints the result; source rim constants restore the
view-dependent cliff contribution. Material mode 5 implements this contract
across the same four renderer paths.

`route1_field_tree_shader05_report.json` records the sixth-pass Route 1 foliage
slice. Its two source materials bind six authored roles, use UV0 and UV1, and
recover the alpha cutout, toon, rim, secondary-direction, and directional
highlight operations from decoded BNSH. The `tree002` fragment program is
byte-identical after decoding to the nearby `FieldTreeShader04` `tree006`
program, which explicitly supplies the otherwise omitted `lightColor`.
Material mode 6 implements the local surface across the same four renderer
paths. The report keeps that inherited source value isolated and does not
mislabel it as a captured `FieldTreeShader05` runtime upload.

`route1_field_object_tree_miki_report.json` records the seventh-pass trunk
slice. It isolates the one `FieldObjectShader` variant used by six trunk
groups rather than applying one generic interpretation to the route's four
different object materials. Exact canonical BNSH matches recover Texture01
RGB/alpha on UV0, the same bark texture's highlight alpha on UV1, the toon
table, source rim and shadow colors, vertex color, and output order. Material
mode 7 implements that local surface across all four renderer paths. The
native ten-tap projected shadow term remains fixed to fully lit and is
explicitly not claimed as receive-shadow parity.

`PAC_LgpeQualification` is the isolated visual gate for these four slices. It
loads the canonical cache directly and renders only material modes 4, 5, 6,
and 7 from fixed cameras. The resulting frames show the lawn, actual brown
ledge faces, authored grass lip, source tree foliage, and source trunk material
while no Blender/GLB backdrop or uninterpreted preview material can be mistaken
for source-backed parity:

```powershell
$env:PAC_BACKEND_SCREENSHOT_FRAME = "0"
$env:PAC_BACKEND_SCREENSHOT_PATH = "artifacts/lgpe_qualification/route1_ground_cliff_middle.png"
.\build-vs2022\Debug\PAC_LgpeQualification.exe cache/lgpe/route1 middle
```

The `south`, `middle`, and `north` presets each draw the same seven canonical
ground polygon groups (4,926 triangles), nine canonical cliff polygon groups
(9,187 triangles), and three canonical `FieldTreeShader05` groups (25,336
triangles), plus six canonical tree-miki groups (13,121 triangles). The
focused `tree` and `trunk` presets provide the foliage and trunk review gates.
The cliff roles carry 10, 10, 10, 9, and 11 authored mip levels; the tree
roles carry 9, 9, 9, 10, 11, and 5; the trunk roles carry 8, 8, 9, and 5.
Generated captures stay local under
`artifacts/lgpe_qualification/` because they contain decoded source texture
content.

This evidence deliberately separates the implemented surface stack and mip
sampling from the still-unqualified native lighting stage. Toon-table
lighting is now present for the tree and trunk slices; projected cloud,
depth-shadow PCF, fog, native post-processing, and exact capture-backed tree
global-light upload remain listed as open work rather than being represented
by a generic PBR substitute.
