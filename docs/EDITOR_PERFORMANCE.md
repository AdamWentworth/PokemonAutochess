# Editor performance

Status: Active
Type: Runbook
Last updated: 2026-09-12

Use the **Release** editor and matching Release project plugin for ordinary
environment/gameplay iteration. Debug is for debugging correctness; its frame
rate is not representative of the optimized game.

Select the location and scenario, open **Game**, enable **Stats** beside Play,
then press **Play**. The top-right panel reports actual editor FPS, frame and GPU
time, Game/Scene rendering CPU time, simulation CPU time, draws, triangles and
viewport resolution. Stats continue updating while simulation is stopped or
paused. The build and native renderer appear on the first line.

Frame and GPU numbers include the editor UI and presentation. Game CPU isolates
the viewport rendering callback; it is not the cost of the complete executable.
The old game HUD depends on the standalone host's timing service. The editor
provides its own host statistics so those costs have an accurate scope.

## Repeatable editor test

Close other running editor/game windows, then:

```powershell
./tools/housekeeping/build_editor_pair.ps1 -Configuration All
./tools/benchmark_editor_preview.ps1 -Configurations Debug,Release
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
./tools/housekeeping/check_editor_workflow.ps1 -Cases flat-game-stats,flat-scene-stats,flat-roundtrip
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
