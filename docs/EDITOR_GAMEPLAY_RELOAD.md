# Gameplay reload in the Phlosion Editor

Status: Active
Type: Runbook
Last updated: 2026-09-13

The daily launcher uses Development (`RelWithDebInfo`): optimized code, symbols
and assertions. Automatic gameplay builds use that same configuration; switching
to Debug or Release requires reopening the matching editor.

Saving C++ gameplay source now starts an incremental build while the editor
stays open. Auto Reload is enabled in `phlosion.project.json`, including the
south entrance pilot launcher, which copies that descriptor.

1. Save changes under `src`, `tools`, or in `CMakeLists.txt`. The watcher checks
   C/C++ headers and sources and CMake files every half second, then waits for
   1.2 seconds without another detected save.
2. The viewport reports **Building gameplay**. The current game module remains
   loaded and usable during compilation. Compiler output appears in Console.
3. A successful, current build replaces the gameplay module. The selected scene,
   preview preset, editor window/layout, scene camera, game camera, and saved
   placements remain. The preview resets to its starting state in Edit mode;
   press Play to run the updated code.

Use **Gameplay > Auto Reload on Save** to toggle watching for this session, or
**Gameplay > Rebuild Gameplay** for a manual build. Turning automatic reload off
does not cancel a compilation already underway. Build duration depends on the
changed files; a widely included header can take substantially longer than one
movement implementation file.

If compilation fails, the current module and running preview remain intact.
Fix the code and save again, or use the manual rebuild command. Failed revisions
do not retry indefinitely. Saves during a build queue another build; a build
that predates those saves is not applied.

The editor loads a uniquely named copy of the gameplay DLL, leaving the normal
linker output writable. It checks the rebuilt plugin's existing ABI contract
before unloading the working runtime. If the replacement fails to initialize,
it attempts to reopen the exact previous module bytes. Copies are removed when
their loaded libraries are released. Closing the project/editor cancels its
outstanding build process and descendants.

This is currently a Windows workflow for the project's C++ game module. Engine
and editor C++ changes still require an editor rebuild/restart. Existing supported
layout edits inside the editor continue to apply immediately. External Blender
exports, arbitrary asset files, and Lua saves are not handled by this C++ watcher.
Transient battle state and scene undo history are reset when the module reloads;
saved scene and unit placement overrides are read back from disk.

The optional `gameplay_reload` descriptor section supplies `build_directory`,
`target`, `watch`, `enabled`, and `debounce_ms`. It is read by the editor host and
does not change the project/package DLL ABI. The build directory must already
be configured; CMake is resolved from its cache. Only the project module target
is built. A gameplay reload is not a replacement for the full editor/plugin
paired-build proof used after engine changes. Avoid starting a separate build
of the same build directory while the editor's build is running.

Hidden automation disables automatic builds unless `--auto-reload` is supplied.
`--no-auto-reload` also disables automatic builds in a normal editor launch.
Build logs are stored in `.phlosion/gameplay-build-<editor-pid>.log`.

## Verification

Engine contract tests cover save bursts, saves during compilation, failures,
manual retry, disabled auto reload, source additions/deletions, ignored outputs,
module-copy lifetime, child process exit codes, and argument/log handling.

With other editor windows closed, run:

```powershell
./tools/housekeeping/test_editor_gameplay_reload.ps1
```

This opens an isolated hidden south entrance Battle preview, temporarily changes
a compiled preview label, verifies the changed label after an automatic reload,
introduces an intentional compile error, then restores the original source and
verifies recovery. Both successful reloads happen in one editor process. Exact
source bytes are restored on exit. Logs and a machine-readable result are written
to `debug/editor-gameplay-reload`.
