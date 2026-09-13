# Flat Route 1 arena experiment

Status: Active
Type: Runbook
Last updated: 2026-09-12

This is a separate South Entrance experiment, not a replacement for the four
approved Route 1 arenas. Its 8x8 combat area and both reserve rows are level dirt,
with a one-tile lawn gap between each bench and the battlefield.
Overlapping props and encounter-grass beds were removed from the copy; the
surrounding Route 1 scenery remains. The original entrance retains its ledges,
ramps, grass, movement rules, and existing preview scenarios.

## Compare in the editor

Open `tools/environment/Preview Flat Dirt Experiment.cmd`, or select
**Route 1 - Flat Dirt Experiment** in Scene / location. The available scenarios are:

- **Planning**: place Pokemon on the unobstructed clearing.
- **Crowded Battle**: six Pokemon per side using the normal combat simulation.
- **Earthquake Comparison**: the same teams and starting positions, with a
  presentation-only effect sequence. One broad Earthquake starts after two
  seconds; two smaller overlapping effects start after eight seconds. This
  twelve-second sequence repeats while Play runs. Pause freezes effect time.

The original **Route 1 - South Entrance** also has **Crowded Battle** and
**Earthquake Comparison** with matching fixtures. Compare the ordinary fights
first, then effect readability. Restart the scenario to restore its teams and
effect clock. These level-five fixtures use normal stat validation and combat
rules, so Pokemon can faint. They are not a balance benchmark.

Earthquake adds no damage, target selection, immunity rules, camera shake,
navigation deformation, or permanent terrain changes. Pokemon continue fighting
normally. The preview is for judging scale, occlusion, composition and overlap
before choosing the battlefield design or implementing the move.

## Edit the environment

Use `tools/environment/Open Flat Dirt Experiment in Blender.cmd`.
The source is a separate file at
`D:/ProjectData/Games/PokemonAutochess/EnvironmentResearch/Route1/authoring/flat-experiment/Route1_FlatExperiment.blend`.
The ordinary Autochess tile and prop controls remain available.

The recipe is `config/environment/route1_flat_experiment.authoring.json`.
Board cells are source X=17..24, Z=-9..-2; reserves are Z=-11 and Z=0.
The complete arena footprint sits one tile south of its earlier placement.
Z=-10 and Z=-1 are lawn separators outside both gameplay footprints. Dirt ends
at the board and reserve boundaries; the row below the south bench (Z=1) is lawn.
The orange editor lines mark 0.35-tile clearance around each footprint.
Gameplay placement, reserve positions and rendered grids use the active arena's
bench spacing. Returning to an older arena restores that arena's spacing.

A three-tile-wide grassy ramp at X=22..24, Z=-12..-13 rises smoothly by one metre
to join the upper route behind the enemy bench. A full grass shelf at Z=-12
splits the rear bank into half-metre ledges. The farther rocky bank is also
lowered by half a metre, keeping its back face within the same height limit.
Four independently editable encounter-grass beds sit on the west, east,
upper-east and rear verges. Their
full-size LGPE blades and cover footprints stay outside the board and benches;
the rear bed is moved aside to leave the route exit open.
The adjacent lawn strip was lowered with the dirt floor to provide some visual
space around the board. Scenery beyond that band retains its elevations.
The bootstrap `create_route1_flat_experiment.py` refuses an existing output or
the input blend. Do not rerun it for routine edits.

```powershell
./tools/environment/export_route1_pilot.ps1 -Recipe config/environment/route1_flat_experiment.authoring.json
```

Publication validates and installs a distinct PHSC archive under
`content/phlosion/environment/flat-experiment`, with immutable source/archive
backups in the configured private depot. It does not activate a partial export.

## Effect boundary

`config/vfx/earthquake_experiment.json` controls the preview footprint and
vertical scale. The initial trial uses a seven-metre footprint and reduces
vertical scale to 65% so the raised floor competes less with small Pokemon.
The floor is anchored five millimetres above the flat arena to avoid coplanar
surface flicker. Restart the editor after changing effect configuration/assets.

The private `sampled_effect_clip` package contains evaluated mesh positions,
UVs, vertex colours, visibility and camera-facing card tracks. It is not a
screen-space recording: the editor can orbit and actual arena depth occludes
the effect. It retains 77 textured mesh layers and 312 accepted cards. The
99 sampled frames include the recovered active sequence and its empty tail.
The actual navigation surface stays flat underneath the animated visual slabs.

Source decoding and sampling remain in the separate Cipher Snagem research lab.
The added `--export-sampled-clip` command shares that lab's mesh/material
evaluators and the selected preview's card evaluator. Run source recipe checks
from the lab directory, where its existing private evidence lookup expects
the sibling research workspace. The lab's accepted visual interpretation is
unchanged. The game consumes only the resulting source-neutral package.

`SampledEffectClip` is an experimental game-side adapter, not a replacement for
the production Phlosion VFX pipeline. It validates bounded payloads and indices,
uses cached mesh samples, and constructs camera-facing cards through the shared
indexed batch path. There are no rendering-API branches or source-game parser
dependencies. Final move integration and broader reuse should promote the
appropriate reusable pieces into PhlosionVFX.

`config/vfx/earthquake_experiment_package.json` records the private package hashes.
The package is backed up under the private depot's ordinary runtime content
tree, so `tools/assets/sync_asset_depot.ps1` restores it without the research lab.
Missing preview assets fail explicitly; they do not substitute a generated effect.

## Qualification

```powershell
ctest --test-dir build -C Release -R 'sampled_effect_clip_contract|flat_arena_experiment_contract|editor_preview_catalog_contract|authored_arena_bundle_contract|movement_collision_regressions|arena_travel_contract' --output-on-failure
./tools/environment/check_flat_arena_experiment.ps1
./tools/housekeeping/check_editor_workflow.ps1 -Cases flat-unit-setup,flat-roundtrip
./tools/render_parity_matrix.ps1 -Config Release -Cases flat-detached-benches
```

The visual check includes both terrain layouts, ordinary combat at the matched
single/overlap frames, and both effect phases, on OpenGL, Vulkan and D3D12.
Existing image tolerances are unchanged. In addition to cross-API equality,
effect captures must visibly differ from the same-frame no-effect controls;
three missing effects cannot pass just by looking identical.

The new native tests reject malformed sampled geometry and unsafe texture paths,
check delayed/retired geometry, require all board/reserve floor probes to be dirt
without cover, verify two-way walking, detached bench placement and hit regions,
continuous ramp heights, half-metre perimeter faces (including ramp sides),
and switch back to the unchanged entrance.
The paired editor/plugin build and moving embedded preview captures are required
in addition to standalone screenshots. The comparison command checks both editor
scenes at a fixed seed as well. Local evidence is in `debug/flat-experiment/qualified`.

The southward placement and shorter rear terraces are qualified separately in
`debug/flat-south-shift`: eight relevant contracts pass in both Debug and Release;
Scene view, populated Game preview and repeated scene switches pass on all three
native APIs. Switching back matches a fresh scene load with zero changed pixels
in the environment comparison region. The standalone `flat-detached-benches`
case also passes on all three APIs with its reserve-unit content guards.

The experiment exposed a completed-view opacity bug in the engine's editor
composition. OpenGL, Vulkan and D3D12 now present the finished world image as
opaque, keeping material transparency inside the rendered scene. This prevents
the UI background from darkening grass, floor overlays and overlapping effects.
The original terrain sources and board presentation remain intact.
