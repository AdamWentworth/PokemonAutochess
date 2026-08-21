# Asset depot sync

`sync_asset_depot.ps1` copies local runtime payloads from the private sibling
asset depot into this ignored game workspace. It never mirrors or deletes
destination files.

Default layout:

```text
../PhlosionAssets/
  pokemon-autochess/runtime/assets/
  pokemon-autochess/runtime/content/phlosion/
```

Use a different depot location with:

```powershell
$env:PHLOSION_ASSET_DEPOT = "E:\Private\PhlosionAssets"
.\tools\assets\sync_asset_depot.ps1
```

Use `-VerifyOnly` to validate paths and report file counts without copying.

## Versioned depot backup

`backup_asset_depot.ps1` creates a non-mirroring, versioned snapshot of the
authoritative `source`, `derived`, `runtime`, `evidence`, and `legacy` depot
sections. It excludes transient `artifacts`, `debug`, and `scratch` data,
records repository provenance and the Kanto control plane, and SHA-256 verifies
every destination file before removing the `INCOMPLETE` marker and publishing
the final directory. The default invocation is report-only:

```powershell
.\tools\assets\backup_asset_depot.ps1
.\tools\assets\backup_asset_depot.ps1 -Apply
```

## Project asset catalog

`config/assets/asset_catalog.json` is the tracked authority for materialized
project assets. The external model package describes the canonical assets
published to this workspace; the catalog assigns them to active or staged
scope. It also owns authored Poke Ball/Growl GLBs, Route 1, and explicitly
retained legacy-review models.

Validate ownership without cooking:

```powershell
.\build\Debug\PhlosionForge.exe validate-catalog
```

Validation fails when a physical `.phmodel`, `.animset.json`, or `.glb` has no
catalog owner, when a packaged model is missing, or when active Pokemon
configuration points outside the catalog. The read-only housekeeping report
at `tools/housekeeping/inventory_workspace.ps1` combines this ownership data
with the current schema-2 cook manifest and reports superseded outputs without
deleting them.

## Strict cooked Pokemon runtime

Pokemon configuration requires explicit `.phmodel` identities for every base
and appearance variant. Those files are offline canonical/import products:
gameplay resolves the identity to its PHLO object and fails with the expected
object path if the cook is missing. It never decodes `.phmodel` or falls back
to a legacy `.pacmdl` cache. The Inspector model viewer likewise accepts the
cooked `.phlo` path directly.

Poke Ball and the nine Growl mesh identities remain explicit GLB compatibility
exceptions pending replacement decisions. They already have catalogued PHLO
objects and pass strict-cooked loading, but their source-shaped runtime IDs and
legacy fallback remain until that deferred migration is authorized.

Route 1 has no GLB or loose LGPE-cache runtime fallback. Its canonical
environment loader mounts `content/phlosion/scenes/route1.phscene`, then serves
the composition manifest, board registration, geometry, materials, and
textures exclusively from the PHSC virtual store. The
`route1_cooked_environment_contract` test exposes only that PHSC to the host
loader and rejects every other read. Project-owned board/layout documents may
still be applied afterward when present; they are authoring deltas, not source
environment caches.

## External model package

Source-game import recipes, extraction orchestration, visual audits, gender and
form policy, and material research are maintained in the private companion
workspace at `D:/Projects/Research/PokemonSwitchAssetResearch`. They are not
part of the game repository or its CI contract.

Pokemon Autochess owns only the published package declarations:

- `config/assets/kanto_native_model_package.json` enumerates all retained native
  model and animation-set stems;
- `config/assets/kanto_model_promotions.json` selects accepted gameplay models;
- `config/assets/asset_catalog.json` assigns cook and depot ownership.

Validate the package against the private asset view, active Pokemon
configuration, and current cook manifest with:

```powershell
.\tools\assets\validate_kanto_model_promotions.ps1
```

The model package must be regenerated and reviewed in the companion repository
before a source promotion reaches this repository. See
`docs/EXTERNAL_ASSET_RESEARCH.md` for the ownership protocol.

## Shared native payload store
The external import pipeline publishes geometry/animation bytes by SHA-256 under
`assets/models/_payloads/sha256/`. A regular, shiny, or sex-variant `.phmodel`
keeps its own source provenance, material document, animation metadata, and
logical file name; only an exactly byte-identical `.bin` is shared. Publication
copies and verifies the immutable payload before atomically switching the
manifest, removes a stem-named legacy payload only after it is unreferenced,
and garbage-collects unreferenced content-addressed payloads after the import.

Inspect an existing model root without changing it:

```powershell
.\tools\assets\migrate_native_model_payloads.ps1
```

Apply the resumable migration after reviewing the counts:

```powershell
.\tools\assets\migrate_native_model_payloads.ps1 -Apply
```

The private runtime depot uses the same relative identities. Pass its models
directory explicitly when auditing or migrating that mirror. Enforce the
normal zero-byte duplicate, zero-legacy-reference, and zero-orphan budgets with:

```powershell
.\tools\assets\validate_native_model_payloads.ps1
```

The PowerShell contract test uses synthetic manifests and payloads, so it runs
without private assets. It covers distinct regular/shiny material identity,
deduplicated publication, dry-run behavior, migration idempotence, and corrupt
immutable-payload rejection.

After a complete Forge cook/finalize, publish only manifest-owned runtime
content back to the private depot with the hash-aware, report-only-by-default
publisher. It skips identical files, removes stale files only inside
manifest-owned object/shared-dependency directories after successful copies,
and never changes unrelated depot content:

```powershell
.\tools\assets\publish_runtime_content_to_depot.ps1
.\tools\assets\publish_runtime_content_to_depot.ps1 -Apply
```

Its synthetic contract additionally proves report-only behavior, exact
manifest-owned dependency copying, idempotence, and preservation of unrelated
depot files.

Model PHMAT files reference immutable KTX2 payloads under
`content/phlosion/dependencies/ktx2/`. The identity combines encoded content
with color-space, role, sampler, and material interpretation, while the PHMAT
retains all reference-site metadata. Object cooks are staged and directory
swapped; finalization publishes the exact shared-dependency inventory and
prunes unreferenced/partial store files.

Review superseded and catalog-declared legacy cooked objects without changing
them, then apply the guarded removal if the plan contains only expected
generated directories:

```powershell
.\tools\assets\prune_unreferenced_cooked_objects.ps1
.\tools\assets\prune_unreferenced_cooked_objects.ps1 -Apply
```

The pruner preserves manifest-owned, environment, and unclassified review
objects; classifies superseded hashes, catalog-declared legacy identities, and
unowned canonical Game Freak import identities; rejects reparse points and
active game/editor/tool processes; and is covered by a synthetic
dry-run/apply/idempotence contract.

After changing package or catalog ownership, review and then apply the two
workspace-source pruners. The native pruner only classifies canonical imported
model stems; the review pruner only classifies numbered Pokemon GLB/animation-
set pairs that no longer have a catalog owner:

```powershell
.\tools\assets\prune_unreferenced_native_imports.ps1
.\tools\assets\prune_unreferenced_native_imports.ps1 -Apply
.\tools\assets\prune_unreferenced_review_models.ps1
.\tools\assets\prune_unreferenced_review_models.ps1 -Apply
```

Both are report-only by default, enforce model-root containment, reject reparse
points, and have synthetic dry-run/apply/idempotence contract tests.

`config/assets/kanto_model_promotions.json` freezes the accepted regular,
shiny, sex, and optional-form models for every original-151 species.
Non-promoted package outputs remain available for Inspector comparison and
future roster work, but they are not production model choices. Validate the
registry, active Pokemon configuration, external package, and current cooked-
object publication together after any package promotion:

```powershell
.\tools\assets\validate_kanto_model_promotions.ps1
```

`PhlosionForge finalize-cook` snapshots current model/runtime objects and reuses
the environment section from the current schema-2 manifest. It still runs full
strict validation before atomic publication. This lets a model-only or resumed
cook be finalized after regenerable Route 1 source cache has been removed;
`cook-route1` and `cook-all` remain the commands that intentionally rebuild
Route 1 from source.
