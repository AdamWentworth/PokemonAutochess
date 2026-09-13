# Editor performance

Status: Active
Type: Runbook
Last updated: 2026-09-13

Double-click **Open Phlosion Editor.cmd** for everyday work. It checks/builds a
matching **Development** editor and gameplay module, builds the standalone game,
and opens the flat arena with Stats enabled. Existing environment preview
shortcuts use this launcher too. The first Development build takes longer;
subsequent builds are incremental.

Development uses CMake `RelWithDebInfo`: optimized C++ with debugging symbols,
assertions and the release runtime ABI. The editor, gameplay DLL and standalone
game use the same configuration. Debug remains available for diagnosis; Release
remains the shipping-performance reference. The launcher's `-Mode EngineDebug`
and `-Mode Release` select those explicitly. A profile change requires closing
the current editor. Source reload keeps the current profile.

Select the location and scenario, open **Game**, enable **Stats** beside Play,
then press **Play**. The top-right panel reports actual editor FPS, frame and GPU
time, Game/Scene rendering CPU time, simulation CPU time, draws, triangles and
viewport resolution. Stats continue updating while simulation is stopped or
paused. The build and native renderer appear on the first line.

Frame and GPU numbers include the editor UI and presentation. Game CPU isolates
the viewport rendering callback; it is not the cost of the complete executable.
The old game HUD depends on the standalone host's timing service. The editor
provides its own host statistics so those costs have an accurate scope.

## Record and run

Click **Record 30s** beside Stats, or **Performance > Record 30 Seconds**.
Recording excludes two seconds of warmup, then saves average, p95 and worst
frame time, viewport/simulation CPU time, valid GPU samples, and per-frame data
to `.phlosion/performance`. The report opens automatically; **Last Report**
reopens it. It records the scene, scenario, build, API, GPU, viewport dimensions,
starting camera and unit placements. Changing the scene, scenario, view,
resolution or play state, rebuilding code, or minimizing cancels the recording
so incompatible measurements are not mixed. Camera movement and combat during
a recording are part of that recording's workload.

**Play > Run Game (Standalone)** incrementally builds the current configuration
before launching a fresh game from the main menu using the same native API.
The current battle is not transferred. A failed build leaves the editor intact;
compiler output is in `.phlosion/standalone-build.log`. Gameplay reload and
standalone builds cannot run concurrently. After launch, embedded play pauses
and the editor minimizes; minimized interactive editors suspend rendering and
simulation. Restore the editor window when finished with standalone play.

## Repeatable editor test

Close other running editor/game windows, then:

```powershell
./tools/housekeeping/build_editor_pair.ps1 -Configuration All
./tools/benchmark_editor_preview.ps1 -Configurations RelWithDebInfo,Release
```

The benchmark checks the editor/plugin pair, uses all three native APIs, disables
VSync and automatic reload, isolates editor state and retains the same paused
twelve-Pokemon setup. It excludes warmup samples and captures no screenshots.
`debug/editor-performance/benchmark/report.json` and `report.csv` contain mean and
p95 frame time, GPU time, viewport CPU time, visible units and actual resolution.
This measures rendering/editor overhead. It is not an active-combat benchmark.
Use `-Scenario ''` to measure Scene view or select another named starting setup.

For gameplay/animation costs and shipping performance, retain the standalone
Release benchmark in [the test plan](TEST_PLAN.md). Compare identical scenes,
rosters, animation state, render resolution, quality, VSync and frame caps; do
not compare a smaller paused editor viewport directly with a larger animated
game window or with historical FPS from a different environment.

The visual gate is separate:

```powershell
./tools/housekeeping/check_editor_workflow.ps1 -Cases flat-game-stats,flat-scene-stats,flat-performance-recording,flat-roundtrip
```

It checks visible HUD content and live timing availability on OpenGL, Vulkan
and D3D12, while the ordinary scene roundtrip still requires exact environment
equality. No image thresholds are widened for performance work.

## September 12 local baseline

GTX 1070, flat arena, twelve paused Pokemon, 845x513 viewport, VSync off,
1,200 frames with 120 warmup samples excluded. Values are mean frame
milliseconds; lower is better. This is a local diagnostic baseline.

| Native API | Debug editor | Release editor | Release GPU |
| --- | ---: | ---: | ---: |
| OpenGL | 30.75 | 14.12 | 10.61 |
| Vulkan | 57.53 | 9.98 | 1.50 |
| D3D12 | 19.18 | 6.18 | 1.96 |

Reports are under `debug/editor-performance/benchmark`. A separate same-binary
D3D12 comparison measured 6.49 ms with Stats off and 6.20 ms with Stats on;
this showed no HUD slowdown, not a speed improvement. Earlier shorter captures
were faster, so do not describe a single instantaneous FPS reading as a stable
performance guarantee.

The standalone Release run used the same resolution and initial twelve-unit
fixture, but progressed into combat despite the initial Planning metadata.
Snapshot pinning prevents script transitions; it does not freeze combat updates.
It measured about 60/71/136 FPS on OpenGL/Vulkan/D3D12, respectively, and must not
be compared directly with the paused rows above. Logs confirm native APIs,
GPU clip skinning and zero CPU mesh rewrite batches. Projected Pokemon
preparation averaged 0.47-0.58 ms; the remaining frame cost needs further
profiling, including submission/waits and combat effects. These measurements
do not establish that all historical performance targets are retained.

## Environment runtime optimization

Published Blender scenery retains its geometry, textures, density, lighting,
wind and contact rustling. Static foliage and encounter-grass instance lists
are retained across animation updates; only their existing skin palettes are
updated in place. Scene loads and placement edits still rebuild the instances.
This removes redundant CPU composition and allocation without an asset-quality
tradeoff. CPU foliage tests and matching native API captures guard that contract.

## Development qualification, September 13

GTX 1070, the same paused twelve-Pokemon flat-arena fixture, 845x513 viewport,
VSync off. The first comparison used 1,200 frames with 120 warmup samples.
Values are mean frame milliseconds; lower is better.

| API | Previous Release | Updated Release | Development |
| --- | ---: | ---: | ---: |
| OpenGL | 14.58 | 14.54 | 15.07 |
| Vulkan | 9.84 | 10.19 | 11.84 |
| D3D12 | 6.53 | 3.47 | 7.04 |

A longer repeat used 2,400 frames with 240 warmup samples:

| API | Release mean | Development mean | Release p95 | Development p95 |
| --- | ---: | ---: | ---: | ---: |
| D3D12 | 3.56 | 3.66 | 10.28 | 10.13 |
| Vulkan | 9.94 | 10.28 | 17.20 | 17.94 |

Development approaches Release performance in the longer repeat while retaining
symbols and assertions. Run-to-run variation is substantial, especially on
D3D12; these measurements do not isolate the foliage change's contribution or
establish an active-combat FPS guarantee. The consistent workflow benefit is
using optimized code during everyday editing. OpenGL remains relatively GPU
heavy; Vulkan spends much more time in viewport CPU work and presentation than
in GPU execution. These are separate targets for subsequent profiling.

Evidence is under `debug/development-editor/{before,after,repeat}`. Qualification
also includes 11 editor cases on all three native APIs, native game captures,
and exactly matching before/after environment comparison regions on each API
(in both the game and editor). Wind, contact motion, density and shadows remain
covered. No visual thresholds or asset quality settings were reduced.

The 270-test Debug suite passed 269 cases on the first run; its remaining test
still expected the pre-existing starter route. Updating that expectation to the
flat arena and rerunning both mode-flow tests passed. All 13 engine checks and
the affected Development CPU checks passed. The automatic reload test verified
successful reload, intentional compile failure, recovery and camera preservation.
The D3D12 standalone integration test verified build failure rejection, correct
Development executable selection, launch and rendered scene content.

Reproduce the integration tests with:

```powershell
./tools/housekeeping/test_editor_gameplay_reload.ps1
./tools/housekeeping/test_editor_standalone.ps1
```
