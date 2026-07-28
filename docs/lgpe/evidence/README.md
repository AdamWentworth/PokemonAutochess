# LGPE Direct-Source Evidence

Status: Active
Type: Evidence
Last updated: 2026-07-27

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

`PAC_LgpeQualification` is the isolated visual gate for this slice. It loads
the canonical cache directly and renders only material mode 4 from three
fixed cameras, so no Blender/GLB backdrop or uninterpreted preview material can
be mistaken for source-backed ground parity:

```powershell
$env:PAC_BACKEND_SCREENSHOT_FRAME = "1"
$env:PAC_BACKEND_SCREENSHOT_PATH = "artifacts/lgpe_qualification/route1_ground_middle.png"
.\build-vs2022\Debug\PAC_LgpeQualification.exe cache/lgpe/route1 middle
```

The `south`, `middle`, and `north` presets each draw the same seven canonical
ground polygon groups (4,926 triangles). Generated captures stay local under
`artifacts/lgpe_qualification/` because they contain decoded source texture
content.

This evidence deliberately separates the implemented surface stack and mip
sampling from the still-unqualified native lighting stage. Toon-table
lighting, projected cloud, depth-shadow PCF, fog, and native post-processing
remain listed as open work rather than being represented by a generic PBR
substitute.
