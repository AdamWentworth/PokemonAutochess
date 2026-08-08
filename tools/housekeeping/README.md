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

Classifications such as `legacy_model_candidate`, `unclassified_cooked`, and
`review` are evidence for the next investigation. They never mean that the
tool considers an asset safe to delete.
