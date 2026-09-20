# Outstanding issues

Status: Active
Type: Tracker
Last updated: 2026-09-20

This register separates observed constraints from future engineering candidates.
The [August issue register](archive/2026-08-20-outstanding-issues.md) preserves
prior file-level observations and resolved work. Engine and VFX paths in that
record predate their current repository ownership.

## Current constraints

| Issue | Current behavior | Owner / next decision |
| --- | --- | --- |
| Existing Pidgey editor content guard | `combat-target-focus` fails its `engaged-pidgey` appearance guard on the unchanged `67c519cd` baseline and the HUD update. The captured battlefield pixels are identical; the threshold remains unchanged. | Visual qualification: review the current promoted Pidgey appearance and its dated fixture before claiming this case passes. |
| Private package required by editor configuration | `PAC_BUILD_EDITOR=ON` fetches and requires PhlosionPackages even though `editor_packages` is empty. The standalone game build avoids this dependency. | Game build setup: separate optional Tile Tools from the editor plugin build. |
| Full demo and GPU checks require private content | Public source builds and synthetic tests do not provide the displayed Pokemon/arena payloads. | Game content: maintain depot restore and capture instructions. |
| No controlled GPU performance gate in CI | Local benchmarks and visual matrices exist; hosted Windows is not the representative performance machine. | Verification: evaluate a GPU runner if the project resumes sustained performance work. |
| Legacy route reconstruction remains live | Original route/reference paths retain source terrain behavior alongside the newer Blender arenas. | Game environments: retire callers only after replacement and reference preservation. |
| No public release checkpoint | Original code is Apache 2.0 licensed with [third-party exclusions](LICENSING.md), but there is no packaged playable distribution. | Review milestone remains deferred. |

## Retained maintenance risks

- GameSession and GameRunner remain composition points. Shared projected model
  preparation/submission spans many helpers. Additional extraction should follow
  measured pain, not file size alone.
- Generic backend interfaces, renderer families and model internals belong to
  Phlosion Engine. Their future restructuring is tracked with that repository;
  Pokemon Autochess owns the adapter and gameplay consequences.
- Logging still uses LogBus, the engine LogSink and direct stream output.
- Existing loose VFX compatibility paths and content identities require an
  explicit migration before cleanup; native model materials use cooked resources.
- VFX preview composition and incomplete effect phases remain prototype work.
  Reusable implementations belong to Phlosion VFX; game previews remain here.

These risks are not new failing tests or a commitment to implement the historical
backlog now. Use fresh evidence when choosing a specific follow-up.

## Closed boundary issues

Board/combat UI semantics, game profiling/debug structures and recovered material
programs now have game ownership. Generic engine and reusable VFX boundaries have
separate verification. See [project boundaries](PROJECT_BOUNDARIES.md) and the
[September material verification](CHARACTER_MATERIALS.md#boundary-verification-2026-09-19).
