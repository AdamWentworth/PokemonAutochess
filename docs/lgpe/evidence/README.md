# LGPE Direct-Source Evidence

Status: Active
Type: Evidence
Last updated: 2026-07-29

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
program. A later synchronous guest capture closes the otherwise omitted
`lightColor` upload: the canonical 17,556/40,896/17,556 polygon-group index
sequence identifies the three Route 1 draws, and generated pixel shader 2421
reads distinct `tree001` and `tree002` values from `c5.data[4..6]`. Material
mode 6 now carries those exact material-correlated uploads across OpenGL,
D3D12, Vulkan direct, and Vulkan indirect. `tree006` retains its separately
serialized source value rather than borrowing either captured Route 1 value.

`route1_field_object_tree_miki_report.json` records the seventh-pass trunk
slice. It isolates the one `FieldObjectShader` variant used by six trunk
groups rather than applying one generic interpretation to the route's four
different object materials. Exact canonical BNSH matches recover Texture01
RGB/alpha on UV0, the same bark texture's highlight alpha on UV1, the toon
table, source rim and shadow colors, vertex color, and output order. Material
mode 7 implements that local surface across all four renderer paths. The
native ten-tap projected shadow term remains fixed to fully lit and is
explicitly not claimed as receive-shadow parity.

`route1_field_tree_shader02_04_report.json` records the eighth-pass remaining
canopy slice. It restores the two `FieldTreeShader02` groups with their exact
five sampled roles, source vertex RGBA tint blend, two toon tables, five
material colors, cutout, rim, and directional-light order. Material mode 8
implements that recovered local surface across all four renderer paths. The
nearby `FieldTreeShader04` group reuses material mode 6 because its decoded
fragment SPIR-V is byte-identical to the implemented `tree002`
`FieldTreeShader05` program and its source explicitly supplies its own
`lightColor`. No local canopy wind is inferred because neither recovered
vertex program contains one.

`route1_foliage_presentation_review_report.json` records the later foliage
presentation pass. The recovered local modes 6, 8, and 19 remain intact; known
Route 1 foliage families select reviewed modes 21 through 26 around those
oracles. The reviewed layer carries the accepted Blender/gameplay family color
calibrations, the tree005 dark-conifer palette operation, the restrained 25%
stationary cloud projection, and the display-context translation. It also
locks the canonical-row finding exposed by the flower/sign work: reviewed
foliage samples raw source UV0 because the cache upload is already top-down,
while the exact local modes retain the decoded source-program convention.

`route1_buildmodel_vegetation_shader_rig_report.json` records the later
flower and low-shrub refinement. It separates the exact six-sampler
`grass02` program and shared five-joint vertex-program evidence from the
explicitly reconstructed CPU-side joint driver; all 54 source placements
remain unchanged.

`route1_field_grass_shader01_02_report.json` records the ninth-pass ordinary
grass/lip/decal slice. It restores the three source materials used by 16
polygon groups with their exact shared vertex program, eight-sampler fragment
contract, UV0/UV1 split, normalized source color, toon, highlight, decoration
blend, mip bias, rim or `OnGameColor`, and discard/output order. Material
modes 9 and 10 implement the recovered local surfaces across all four renderer
paths. The later shared-light pass adds the exact captured projected cloud;
the ten-tap depth-shadow matrix/frame state remains bounded. The decoded
`grass01_com` BC1 base level is fully opaque, so the
authored rectangular atlas/decal geometry must be matched to its lawn underlay;
the runtime does not invent a luminance alpha mask to hide unresolved seams.

`route1_field_grass_shader04_05_report.json` records the tenth-pass remaining
small-grass slice. It restores `grass_s03` and `grass_s04`, used by two polygon
groups and 1,744 triangles, from their decoded vertex and fragment programs.
The report also records the BNSH reflection dictionary, shader-location table,
and decoded sampler remap: source binding order is not shader slot order for
either program. Material modes 11 and 12 implement the resulting layered
cutout, two-atlas blend, decoration, vertex-color, toon, and `OnGameColor`
operations across all four renderer paths. The Shader04 routing is additionally
locked to the captured fragment binding evidence:
`mix(Texture01(UV0), Texture02(UV1), Texture03(UV1).r)`. This avoids the
rejected pale-card result produced when decoded sampler-slot order is mistaken
for material semantic-name order. The captured fixed-light basis is now
transformed back into canonical Y-up Route 1 coordinates and both shaders
sample the authored 1024x1024 `cloud01` texture with Repeat wrap. Shader04
uses `min(toon, projectedCloud)`; Shader05 uses the projected cloud directly
because the decoded `shadowtable02_t` base level is uniformly white. The
recovered effect is a layered fragment texture and fixed world-light treatment;
neither vertex program contains local wind. The shared ten-tap depth-shadow
PCF remains bounded separately.

`route1_field_overlay_report.json` records the eleventh-pass roadstone and
rock-mask overlay slice. These materials use only 78 triangles but cover ten
large source quads, so their prior opaque preview interpretation produced some
of the largest rectangles in the route. Exact named BNSH matches recover both
premultiplied fragment programs, the sampler remaps, UV0/UV1 use, toon
lighting, two-soil rock-mask blend, authored opacity, and the deliberate
rock-mask omission of vertex alpha. Material modes 13 and 14 implement the
contracts across all four renderer paths. Decoded base alpha independently
confirms that 80.87 percent of the roadstone texture is fully transparent.

`route1_remaining_vegetation_report.json` records the twelfth-pass flower and
grass-covered-rock slice. Exact named BNSH programs recover the flower's
0.85 alpha cutout and the rock's five-surface border blend, directional
highlight, toon, rim, vertex-color, and `OnGameColor` operations. The decoded
512-wide `lighttable01_t` has a long zero region followed by a specific
54-texel transition; modes 15 and 16 reproduce its linear sampling exactly
across all four render paths without an inferred curve. Neither recovered
vertex program contains wind or billboard deformation. At that restore point,
the signboard was the only Route 1 material still on diagnostic preview
rendering.

`route1_signboard_report.json` records the thirteenth-pass signboard slice.
The exact named `FieldObjectShader` programs recover the sign's source
`Texture01`, shadow and light toon tables, directional highlight, automatic
shadow window, rim, vertex color, and output order. Material mode 17 implements
that contract across all four render paths. The source `ShadowColor` and
`OnGameColor` are both white, so those enabled terms are retained as proven
neutral operations rather than approximated tints. This pass removes the final
diagnostic preview material: all 21 Route 1 material records are now either
source-backed main-pass surfaces or the two authored `SkipMainRendering`
shadow-only records. The sign report also locks the canonical atlas
orientation: the cooker emits top-down RGBA rows, so runtime mode 17 samples
canonical UV0 without applying the source program's vertical convention a
second time. The front panel's authored UV island consequently selects the
lower-left sign artwork instead of the upper-left back-face artwork.

`PAC_LgpeQualification` is the isolated visual gate for these source-backed
slices. It loads the canonical cache directly and renders only material modes
4 through 17 from fixed cameras. The resulting frames show the
lawn, actual brown ledge faces, authored grass lip and ordinary vegetation,
small grass, roadstone marks, rock-mask overlay, source flowers,
grass-covered rocks, source tree foliage, and source trunk material while no
Blender/GLB backdrop or uninterpreted preview material can be mistaken for
source-backed parity:

```powershell
$env:PAC_BACKEND_SCREENSHOT_FRAME = "0"
$env:PAC_BACKEND_SCREENSHOT_PATH = "artifacts/lgpe_qualification/route1_ground_cliff_middle.png"
.\build-vs2022\Debug\PAC_LgpeQualification.exe cache/lgpe/route1 middle
```

The same qualification executable also provides an interactive engine review:

```powershell
.\build\Debug\PAC_LgpeQualification.exe `
  cache/lgpe/route1 middle - 0.3 --interactive
```

This continuously renders the canonical source scene through the same OpenGL
material path used by the fixed qualification frames. Left-mouse drag pans;
right- or middle-mouse drag orbits; Shift+middle-mouse drag also pans; and the
mouse wheel zooms. `WASD` pans, `Q`/`E` moves vertically, the arrow keys orbit,
`1` through `4` select the south, middle, north, and canopy viewpoints, `R`
restores the launch viewpoint, and `Esc` closes the viewer. Encounter grass
advances its accepted four-second reconstructed motion while the window is
open. A `gameplay` camera preset now reproduces the promoted gameplay framing
without changing the source-centimetre scene.

`route1_buildmodel_placements.json` closes the previously missing static
build-model vegetation layer. It is regenerated directly from
`field/placement/001 C135E084B8176A95.bin`, whose expected SHA-256 is
`56D70BBC2AD79FA01044730B171105F0F908CE4EE227F026F0B9AB4ED57F1F71`.
The decoder identifies the one Route 1 table by its game-authored model and
collision paths, then preserves all 9 `grass02`, 30 `flowers02`, and 15
`flowers04` translations and Y rotations in source centimetres. No screenshot
or Blender placement is used. Reproduce the metadata and the three local
canonical model caches with:

```powershell
python .\tools\lgpe_importer\export_route1_buildmodel_placements.py
.\tools\lgpe_importer\cook_route1_buildmodel_vegetation.ps1
```

The current qualification gate requires all 54 placements. Together with the
road model, a full Route 1 frame contains 11 `FieldTreeShader02` draws and 46
flower draws. The build-model shrubs retain their exact 0.418742657 cutout and
0.01 shadow-bias metadata; the two flower models retain their exact pasted
material name and 0.001 shadow-bias variant. Their proprietary geometry and
textures remain local under `cache/lgpe/`, while the committed cook reports
record hashes and counts.

An optional fourth argument limits diagnostic rendering to one exact source
material, for example `grass01_com_001`.

The `south`, `middle`, `north`, and focused `canopy` presets each draw the
same source-backed
ground polygon groups (4,926 triangles), nine canonical cliff polygon groups
(9,187 triangles), two `FieldTreeShader02` groups (3,780 triangles), one
`FieldTreeShader04` group (2,124 triangles), three canonical
`FieldTreeShader05` groups (25,336 triangles), and six canonical tree-miki
groups (13,121 triangles), plus 15 `FieldGrassShader02` groups (5,929
triangles), one `FieldGrassShader01` group (1,984 triangles), one
`FieldGrassShader04` group (1,542 triangles), and one `FieldGrassShader05`
group (202 triangles), plus nine roadstone groups (18 triangles) and one
rock-mask group (60 triangles), one flower group (792 triangles), and one
grass-covered-rock group (1,645 triangles), and one signboard group
(462 triangles). The focused `tree`, `canopy`, `trunk`, `vegetation`, and
`sign` presets provide the foliage, trunk, ordinary-grass, small-grass, and
signboard review gates. Exact material filters provide the roadstone,
rock-mask, flower, grass-covered-rock, and signboard gates.
The cliff roles carry 10, 10, 10, 9, and 11 authored mip levels; the tree
02 roles carry 11, 11, 10, 10, and 5; the tree 04/05 roles carry 9, 9, 9,
10, 11, and 5; the trunk roles carry 8, 8, 9, and 5; both ordinary-grass
families carry 10, 10, 10, 9, 10, and 9; small-grass shader 04 carries 8, 8,
9, 9, and 11; small-grass shader 05 carries 9, 9, 10, 10, 9, and 11; roadstone
carries 10 and 9; rock-mask carries 9, 9, 10, 9, 9, and 9; flower carries
9 and 9; grass-covered rock carries 9, 10, 10, 9, 11, and 9; and the
signboard carries 9 and 9.
Generated captures stay local under
`artifacts/lgpe_qualification/` because they contain decoded source texture
content.

`route1_shared_projected_lighting_report.json` now records the completed
shared-light and native projected-shadow pass. A synchronous gameplay capture
exposes the guest-written 2048x2048 depth target, all 151 writer events, all
172 pixel-shader consumers, and one common projection matrix in
`c3.data[80..95]`. The runtime preserves that exact orthographic basis, scale,
depth span, ten-tap Poisson kernel, comparison filtering, per-material
`ShadowSampingScale`/`ShadowBias`, and the source
`min(toon * projectedShadow, projectedCloud)` composition. The eighth
world-material binding carries a source-depth atlas on OpenGL, D3D12, Vulkan
direct, and Vulkan indirect. `CastShadow` and `ReceiveShadow` are retained
independently: the two hidden `FieldShadowOnlyShader` groups participate only
in the shadow pass, tree trunks cast, and canopy alpha cards do not. This is
the source-backed fix for the former black/green foliage rectangles.

`route1_ground_cliff_shared_lighting_report.json` records the follow-up
ground/cliff shared-light pass. Directly decoded BNSH proves that both families
apply `mix(Shadow_Color, white, min(toon * projectedShadow,
projectedCloud))` after their recovered local surface expression. Both bind
the same `shadowtable02_t`; every decoded byte in all nine authored mips is
opaque white. Material modes 4 and 5 now combine that toon input with the
recovered native projected-depth result and stationary cloud projection across
OpenGL, D3D12, Vulkan direct, and Vulkan indirect without changing their
surface textures, UVs, mip chains, Alpha_light, border blend, or rim response.

`route1_render_completeness_report.json` records the composition-first pass
that precedes further shared-light refinement. It adds the two separately
stored `FieldEncGrassShader01` source models, all six Route 1 placement
records, four collision footprints, and the accepted half-tile outer ring for
164 submitted encounter-grass modules. It also reconciles two sampler-role
errors exposed by the decoded textures and the combined render: opaque
`greenblend01_com` is the ground pair-variation input while
`glassmask01_com` supplies the UV2 dirt/lawn paint, and
`grassdecoration01_com` is the UV1 RGBA ordinary-vegetation atlas rather than
an opaque lawn base. The corrected roles restore the authored dirt path and
replace rectangular vegetation carriers with their source silhouettes across
OpenGL, D3D12, Vulkan direct, and Vulkan indirect.

`route1_encounter_grass_runtime_parity_report.json` records the follow-up
encounter-grass material and motion pass. Material mode 18 implements the
recovered `FieldEncGrassShader01` equation across OpenGL, D3D12, Vulkan
direct, and Vulkan indirect: source Texture01 and Color0, the exact
`0.632317066` discard, Texture02-gated rim, shadow-to-white lighting, and the
fixed projected cloud. The 164-instance protected half-tile composition,
source geometry, UVs, bone IDs, weights, and authored mip chains remain
unchanged. Motion now uses weighted rotations around the exact source DAE
controller pivots instead of translated cards. The vertex programs prove
joint-matrix skinning but do not contain wind; the four-second cycle,
amplitudes, and phase coefficients therefore remain the accepted
capture-bounded reconstruction rather than claimed game code.

`route1_floor_foliage_carrier_report.json` resolves the last conspicuous
rectangular floor card in the north qualification view. The exact
`road001_00_grass00_000` source mesh is only four vertices and two triangles,
uses `grass01_com_001`, and was already classified by the accepted Blender
evidence as an opaque matching-lawn carrier plus irregular foliage selected by
`grassdecoration01_com red < 0.85`. Applying the general ordinary-vegetation
alpha interpretation retained about 69 percent of this card's rectangular UV
island; the documented red mask retains about 2.3 percent and agrees with
538 of 540 high-alpha foliage samples. A route-and-mesh-scoped prepared marker
now selects that mask on OpenGL, D3D12, Vulkan direct, and Vulkan indirect,
while the submitted source lawn supplies the visually equivalent carrier
background. No other ordinary vegetation, floor tint, encounter-grass
placement, geometry, or UV is changed.

`route1_native_final_color_report.json` records the recovered native
post equation and its final renderer staging. The decoded
`gamma_correction.bnsh` fragment programs contain
only the standard piecewise linear-to-sRGB transfer (plus a 1/255 alpha
discard in the cutout variant); they contain no exposure, ACES, filmic curve,
LUT, or color matrix. A protected-frame target comparison identifies resource
68469 as the linear scene candidate and resource 68630 as its encoded
counterpart: 84.5 percent of all 2,764,800 RGB channels land within one byte
of the decoded transfer, while every other exported 1280x720 candidate is
more than 57 bytes away on average. The complete 3D world is now composed in
a shared RGBA8 UNORM linear scene target on OpenGL, D3D12, Vulkan direct, and
Vulkan indirect. The recovered equation runs once after world composition and
before display-space UI. The static-world three-backend image matrix passes,
and generic engine PBR behavior outside the bracket remains selectable.

`route1_native_fog_report.json` records why Route 1 remains fog-free. The
source `fog.bnsh` contains linear, exponential, exponential-squared, and
lookup-table fragment variants; every one reads fragment coordinates and
discards depth samples outside its configured interval. The protected Route 1
frame contains 36 pixel shaders that read fragment coordinates and none
contains a discard, while the exact gamma shader is present twice. Therefore
the captured Route 1 frame dispatches gamma but not fog. Fog is deliberately
off rather than replaced by guessed distance fog. Other maps still require an
activating capture to select a variant and recover constants.

`route1_gameplay_integration_report.json` records the first canonical gameplay
registration and the now-retired first editor override proof.
`config/lgpe/route1_board_layout.json` owns the complete global transform from
source centimetres into the gameplay world. Project-owned object edits now
belong to `scenes/route1.scene.json`, whose promoted source-faithful baseline
contains no nodes. The historical flower-suppression record remains evidence
that stable source identity and recook guards were qualified; it is not current
runtime authoring state. The Route 1 open-road session submits the
canonical road scene, both encounter-grass sources, all 164 accepted encounter
modules, the complete visible 54-record source placement inventory, the
source-backed material stack, and projected shadows
alongside the game-owned board, units, VFX, and UI. The previous Route 1
environment model is not layered underneath it.

The integration also resolves a composed-source metadata contradiction that
was not present in the isolated road-only review. The placed `grass02`
companion material `pasted__shadow` identifies
`FieldShadowOnlyShader`, `CastShadow=true`, but
`SkipMainRendering=false`. Treating its shader family as authoritative keeps
the geometry in the shadow frame while excluding it from the color frame,
removing the white and green shrub-card silhouettes without deleting their
projected shadows.

This evidence now qualifies the implemented surface stack, authored mip
sampling, toon and global tree lighting, projected cloud, shared projected
depth shadow, and native final-color stage through gameplay submission on
OpenGL, Vulkan, and native D3D12. Source encounter-grass motion remains the
documented capture-bounded reconstruction because the decoded vertex programs
prove skinning but do not contain their upstream animation driver.
