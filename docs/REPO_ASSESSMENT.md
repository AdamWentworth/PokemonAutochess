# Repository assessment

Status: Active
Type: Assessment
Last updated: 2026-09-19

Pokemon Autochess is a working game and runtime-systems portfolio prototype.
This assessment records implemented behavior and verification limits; it does
not assign a subjective readiness score. The [September 8 assessment](archive/2026-09-08-repo-assessment.md)
is retained as history.

## Current baseline

- The game owns board/combat UI, gameplay diagnostics, field materials and
  character/fire material programs. Phlosion Engine provides generic runtime,
  rendering and editor services; Phlosion VFX owns reusable effect primitives.
  [Project boundaries](PROJECT_BOUNDARIES.md) describes the enforced split.
- The editor starts in the Blender-authored Flat Dirt Experiment. South Entrance,
  South Clearing, North Terraces and North Entrance are also in its scene catalog.
  [Editor scene model](EDITOR_SCENE_MODEL.md) records the current locations.
- Authored arena bundles, directed traversal, encounter-grass concealment,
  attack reveals and search behavior have game-owned implementations and contracts.
  See [authored map data](AUTHORED_ARENA_MAP.md), [movement](COMBAT_MOVEMENT.md)
  and [encounter grass](ENCOUNTER_GRASS.md).
- The editor can rebuild gameplay without closing, and paired builds verify the
  editor/plugin ABI and source/artifact provenance.
- The September 19 material extraction passed its listed cases on OpenGL,
  Vulkan and Direct3D 12, including editor material previews and Vulkan direct
  submission. The 45 editor crops and 30 native character regions matched their
  same-renderer baselines exactly. See the [verification record](CHARACTER_MATERIALS.md#boundary-verification-2026-09-19)
  for CPU checks, benchmarks and coverage limits.

## Remaining limits

- A public source checkout does not contain a playable asset bundle. Runtime
  content and full visual qualification need the private asset depot.
- The standalone public build uses `PAC_BUILD_EDITOR=OFF`. Enabling the game
  editor plugin still makes its CMake configuration require private PhlosionPackages,
  although Tile Tools is not mounted. That game-side build dependency remains
  an open cleanup item; it is not a requirement imposed by the generic editor host.
- The original route reconstruction remains live for legacy/reference paths.
  It cannot be deleted solely because the editor starts in a newer arena.
- Hosted CI covers the source build and asset-independent contracts. Full GPU,
  private-content and editor qualification remains local. The material results
  above do not establish coverage for every arena, model, GPU or driver.
- Existing profiling identifies render preparation/submission as a candidate
  for further measurement. There is no controlled GPU performance gate in CI.
- Balancing, content progression and the release experience remain prototype work.
  There is no packaged public release or declared repository code licence yet.

## Review and maintenance

Start with [development](DEVELOPMENT.md), [the test plan](TEST_PLAN.md), and
[the current cleanup roadmap](REPO_CLEANUP_ROADMAP.md). Source and material
boundary changes have been published; use GitHub Actions for the actual status
of a specific commit rather than treating a clean worktree as verification.

The current focus is repository presentation and reproducibility. New gameplay,
renderer restructuring and additional engine features are future decisions.
