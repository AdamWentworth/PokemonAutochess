# Repo assessment

Status: Archived
Type: Assessment
Last updated: 2026-09-08

Archived on 2026-09-19. This records the assessment and priorities at the time;
[current REPO_ASSESSMENT.md](../REPO_ASSESSMENT.md) owns the present status. Paths, measurements and
completion claims below are historical, not current build instructions.

The current foundation is a working prototype, approximately 7/10 for continued
iteration. This is a qualitative planning judgment, not a production-readiness
score. The prior 8.7/10 assessment is retained in the archive as historical context.

## Current strengths

- The approved Blender-authored south entrance has independently editable tiles
  and props, retained source materials, a checked export, and depot backups.
- Deterministic movement reservations have collision and queue regressions.
- The editor can rebuild gameplay without closing; failures retain the working module.
- Engine/game ownership is separate, and paired builds verify the editor/plugin ABI.
- Authored patch preparation and ground sampling have dedicated runtime owners.
- Source-preservation rules for old route references have an explicit legacy owner.

## Current limits

- The original entrance and clearing still require substantial legacy terrain
  reconstruction. Isolating it is not the same as deleting it. Retire it only after
  reference/blueprint preservation and a replacement for every remaining caller.
- Authored map data describes cells, elevation connections, and encounter regions;
  directional ledges, camouflage, and flyer exceptions remain gameplay work.
- Private assets remain necessary for full qualification. Hosted assetless CI
  exercises synthetic contracts, not the complete approved scene.
- OpenGL is the qualified arena preview path. The documented D3D material issue
  and broader renderer parity/performance qualification remain separate work.
- Coordinated dependency commits must be published before a remote clean checkout
  can fetch them. Local checkout and local clean-source proofs do not prove remote availability.

## Working baseline

Use `tools/environment/check_south_entrance.ps1` for the current focused workflow.
Use its optional Blender and capture checks after environment authoring changes.
`tools/full_check.ps1` remains the broader build, documentation, data, and test gate.
Verification reports describe what ran; a clean Git status alone is not a test result.

The next gameplay slice is one-way ledge traversal on the existing south entrance,
followed by visibility/targeting rules for encounter grass. The south clearing
remains deferred. See `BLENDER_ARENA_PILOT.md` and `AUTHORED_ARENA_MAP.md`.
