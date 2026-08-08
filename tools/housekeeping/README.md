# Housekeeping tools

`inventory_workspace.ps1` creates a headless, read-only baseline of the live
Pokemon Autochess workspace and the sibling Phlosion Engine repository. It
does not build, cook, launch, move, or delete anything. Its only writes are the
requested JSON and Markdown reports under the ignored `artifacts/` tree by
default.

From the game repository root:

```powershell
.\tools\housekeeping\inventory_workspace.ps1
```

The default full scan verifies native payload declarations and exact cooked
duplicates with SHA-256. For a faster classification pass that uses declared
native hashes and cooked file-size candidates:

```powershell
.\tools\housekeeping\inventory_workspace.ps1 -Fast
```

Native payload reporting includes manifest count, physical payload count,
total/unique/redundant bytes, and content-addressed versus legacy manifest
counts. Its duplicate-byte budget is zero: any physical duplicate or legacy
stem reference returns a warning finding. Use
`tools/assets/validate_native_model_payloads.ps1` when the budget must fail the
command rather than appear as a read-only inventory finding.

Payload declarations are read from the bounded manifest header and each shared
physical payload is hashed at most once per inventory. This keeps `-Fast`
useful even though native manifests contain large animation tables.

Use `-OutputDirectory` to select a report location, or `-GameRoot` and
`-EngineRoot` when the repositories use a different layout. The JSON report is
the complete machine-readable record. The Markdown report summarizes
provenance, existing build artifacts, the configured headless test catalog,
active configuration, recipes, cook drift, GLB/animset disposition, duplicate
storage, workspace sizes, and findings. `inventory.sha256` pins the JSON report
bytes for later comparison. Internal consistency is validated before any
report is written.

When a valid schema-2 cook manifest matches the asset catalog, cooked object
classification uses its exact object directories. A directory with a known
logical stem but a different generation key is reported as
`superseded_cooked_candidate`; it is never silently counted as an active or
staged object.

Classifications such as `legacy_model_candidate`, `unclassified_cooked`, and
`review` are evidence for the next investigation. They never mean that the
tool considers an asset safe to delete.

## Cleanup planning

`cleanup_workspace.ps1` inventories a fixed allowlist of historical game build
trees, game/engine caches, loose debug output, and local plugin output. Its
default is report-only:

```powershell
.\tools\housekeeping\cleanup_workspace.ps1
.\tools\housekeeping\cleanup_workspace.ps1 -Scope Caches
```

The plan resolves every target to an absolute path, requires it to be one
direct child of the expected workspace root, rejects reparse points, and
records exact file/directory/byte totals. Active `build/`, `artifacts/`,
`assets/`, `content/`, configuration, source, tests, and documentation are not
in the allowlist.

Actual removal requires both `-Execute` and `-ConfirmDeletion`. Immediately
before removing anything, the tool recalculates every target and rejects a
stale plan. It also refuses execution while the game, editor, Forge, or test
process is active. Use execution only after reviewing the generated Markdown
and JSON plan and proving the active build/editor-plugin workflow.

## Paired editor/plugin proof

`build_editor_pair.ps1` builds the Engine-owned editor and CLI compatibility
probe together with the game-owned project plugin. It does not start the
editor, game, a renderer, or any other GUI process. With no configuration
argument it proves both Debug and Release:

```powershell
.\tools\housekeeping\build_editor_pair.ps1
```

Each successful configuration writes an ignored stable proof beside its
plugin in `.phlosion/editor/<configuration>/editor_pair_proof.json` and a
reviewable report under `artifacts/housekeeping/`. The proof binds the exact
editor, probe, and plugin hashes to the relevant game and engine source
fingerprints. It also confirms that the game build tree points at the same
engine checkout used to build the editor.

Revalidate those hashes, source fingerprints, and the binary ABI without
building:

```powershell
.\tools\housekeeping\build_editor_pair.ps1 -VerifyOnly
```

Verification fails if either repository's relevant source changes, an
artifact is replaced, a configuration is mixed, the compiler ABI differs, a
public plugin structure changes size/alignment, or a required runtime callback
is absent. Re-run the normal build command to publish a new proof.

## Retiring obsolete game-local editor artifacts

`PhlosionEditor.exe` is owned and built by the Engine repository. The game
build owns only `PokemonAutochessEditorProject.dll`. Historical game build
trees can retain an editor executable, PDB, intermediate directory, or
generated project even after `PHLOSION_BUILD_EDITOR` is disabled.

Plan the exact fixed allowlist without deleting anything:

```powershell
.\tools\housekeeping\retire_game_editor_artifacts.ps1
```

Actual removal requires both confirmation switches. The command verifies that
the active game cache contains `PHLOSION_BUILD_EDITOR=OFF`, rejects reparse
points and paths outside active `build/`, and refuses to run while the editor
is active:

```powershell
.\tools\housekeeping\retire_game_editor_artifacts.ps1 -Execute -ConfirmDeletion
```

The normal inventory reports `game-local-editor-artifacts` if any allowlisted
artifact returns. Use `build_editor_pair.ps1` to build or verify the real
Engine-owned editor and matching game plugin.

## Hidden visual and performance baselines

`capture_editor_baseline.ps1` produces the fixed review set used before render
or performance work: Inspector Low, Medium, High, and Ultra plus Route 1 on
OpenGL, D3D12, and Vulkan. It first verifies the paired editor/plugin proof,
then runs every capture with a genuinely hidden SDL window, isolated editor
state, fixed 1/60-second time, disabled vsync, redirected logs, and a bounded
frame count. It neither raises a window nor touches the user's normal editor
layout.

```powershell
.\tools\housekeeping\capture_editor_baseline.ps1
```

The ignored `artifacts/baselines/editor-<UTC>/` output contains 15 PNGs, per-run
engine metrics and logs, amplified Low-versus-Ultra heatmaps, a
machine-readable `baseline.json`, and a reviewable `baseline.md`. The command
rejects renderer fallback, missing/invalid PNGs, wrong Inspector quality state,
a non-Route-1 startup scene, byte-identical quality captures within a backend,
a Low-to-Ultra visual difference that is too small, stale paired artifacts,
and nonempty output directories. Use `-Backends`, `-Qualities`, or
`-AssetQuery` only when creating an explicitly scoped comparison run.
