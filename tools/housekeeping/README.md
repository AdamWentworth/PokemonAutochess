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
