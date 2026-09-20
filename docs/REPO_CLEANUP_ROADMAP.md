# Repository cleanup roadmap

Status: Active
Type: Roadmap
Last updated: 2026-09-19

This is the current maintenance scope for Pokemon Autochess. The detailed
[earlier housekeeping audit](archive/2026-09-08-repository-housekeeping-roadmap.md)
preserves historical storage measurements, implementation slices and closeouts.
Those observations are not a new deletion plan or an active renderer backlog.

## Completed ownership and presentation work

- Board rendering, battle feed, health semantics and game-specific diagnostics
  belong to the game. Generic engine services no longer name those game systems.
- Field, character, eye and layered-fire material behavior is supplied explicitly
  by the game through the generic world-material profile. The former material
  ownership phase is complete; see [its verification](CHARACTER_MATERIALS.md#boundary-verification-2026-09-19).
- Source-game extraction and qualification belong to the private research
  workspace. Maintained Blender arena authoring remains game-owned.
- The project does not mount the shelved Tile Tools package.
- The README uses current branding, stack badges and the Flat Dirt Experiment
  with the promoted starter trio. [Media capture](DEMO_MEDIA_CAPTURE.md) owns its
  repeatable settings and provenance.

## Current cleanup items

| Item | State | Scope |
| --- | --- | --- |
| Reconcile active docs and navigation | Complete | Current assessments use dated evidence; earlier planning is preserved in the archive. |
| Clean gameplay showcase and combat animation | Complete | README shows an inline eight-second gameplay GIF linked to a native 1080p still, with performance diagnostics hidden; see [capture verification](DEMO_MEDIA_CAPTURE.md#clean-hud-verification-2026-09-19). |
| Private Tile Tools build dependency | Open decision | Make package building opt-in independently of the game editor plugin; the requirement is in this repository's CMake setup. |
| Code licence | Open decision | Select terms for code the author owns and explicitly identify excluded third-party material. |
| Public website refresh | Deferred | Align the Phlosion showcase with the current repository when website work resumes. |
| Tagged review milestone / release | Deferred | Revisit after an accepted source checkpoint; do not publish private runtime payloads. |

## Retained technical work

[Outstanding issues](OUTSTANDING_ISSUES.md) records current constraints and
[technical debt](TECH_DEBT.md) records longer-term risks. Existing source terrain,
asset compatibility and broad render coordination remain explicit maintenance
boundaries. Their presence does not authorize deleting reference data or starting
another renderer redesign.

Further optimization needs a fresh, comparable profile and a concrete workload.
[Renderer restructuring](RENDER_RESTRUCTURING_OUTSTANDING.md) describes the
conditions for that future work. OpenGL, Vulkan and Direct3D 12 remain equal
supported targets, including relevant editor previews.

## Completion evidence

A cleanup change should leave accurate public instructions, published dependency
pins, a compatible editor/plugin pair where affected, and the checks selected by
[the test plan](TEST_PLAN.md). Report unverified coverage explicitly. Private assets,
cooked content, build outputs and local editor state remain outside Git.
