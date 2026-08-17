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

## Project asset catalog

`config/assets/asset_catalog.json` is the tracked authority for materialized
project assets. Import recipes describe what can be produced; the catalog
selects which recipe outputs are actually owned by this workspace and assigns
them to active or staged scope. It also owns authored Poke Ball/Growl GLBs,
Route 1, and explicitly retained legacy-review models.

Validate ownership without cooking:

```powershell
.\build\Debug\PhlosionForge.exe validate-catalog
```

Validation fails when a physical `.phmodel`, `.animset.json`, or `.glb` has no
catalog owner, when a selected recipe output is missing, or when active Pokemon
configuration points outside the catalog. The read-only housekeeping report
at `tools/housekeeping/inventory_workspace.ps1` combines this ownership data
with the current schema-2 cook manifest and reports superseded outputs without
deleting them.

## Game Freak Pokemon importer

`import_gamefreak_pokemon.ps1` is the repeatable offline boundary from a
catalogued Game Freak resource set to Phlosion's canonical native model IR.
It resolves species/form/gender from `pokemon-variants.json`, stages rare
materials without modifying the private extraction, runs the isolated
headless decoder, validates mesh/material/skeleton/animation output, publishes
the `.phmodel` package to the private depot and ignored game workspace, and can
optionally cook the result into `.phlo` runtime resources. A successful cook is
also published back to the private depot, keeping it as the reproducible source
for later workspace syncs.

```powershell
$env:PHLOSION_ASSET_DEPOT = "D:\ProjectData\Games\PokemonAutochess\Assets"
.\tools\assets\import_gamefreak_pokemon.ps1 -PlanOnly
.\tools\assets\import_gamefreak_pokemon.ps1 -SpeciesId 1,2,3 -Force -Cook
```

The tracked `gamefreak_pokemon_imports.json` recipe contains only asset
identities and output names. Proprietary inputs, canonical generated models,
textures, and cooked PHLO payloads remain in the private/ignored asset roots.

### Trinity GFPAK sources (Legends: Arceus)

Legends: Arceus stores each Pokemon in a three-part `pm####_##_##` GFPAK.
`stage_gamefreak_gfpak_trinity_sources.ps1` expands the corresponding
`Pkmn.txt` hash templates, decompresses Oodle payloads through an external
Ooz executable, preserves the native model/material/animation graph, and
updates the private catalog consumed by `import_gamefreak_pokemon.ps1`:

```powershell
.\tools\assets\stage_gamefreak_gfpak_trinity_sources.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_pla.json `
  -SpeciesId 46,47 -Force

.\tools\assets\import_gamefreak_pokemon.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_pla.json `
  -SpeciesId 46,47 -Force -Cook
```

The Oodle decoder may be supplied with `-OodleDecoder` or
`PHLOSION_OOZ_DECODER`. Original archives, staged native resources, decoded
textures, canonical models, and cooked resources remain private and ignored.

### Extracted TRPAK sources (Legends: Z-A)

Legends: Z-A merged-game-file dumps may expose each TRPAK as a directory of
hash-named payloads. `stage_gamefreak_trpak_sources.ps1` resolves those names
with the versioned Z-A hash list in the private depot, preserves the full
native dependency graph, and emits the catalog consumed by
`import_gamefreak_pokemon.ps1`:

```powershell
.\tools\assets\stage_gamefreak_trpak_sources.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_za.json

.\tools\assets\import_gamefreak_pokemon.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_za.json `
  -Force -Cook
```

Synchronize the tracked Kanto recipe with every complete model/form tuple in
the versioned Z-A hash inventory before staging a newly expanded source dump:

```powershell
.\tools\assets\sync_za_kanto_import_recipe.ps1
.\tools\assets\sync_za_kanto_import_recipe.ps1 -Check
```

The synchronizer preserves hand-authored metadata on existing entries and
adds missing base, gender, regional, and Mega forms with regular and shiny
outputs. The asset catalog exposes all of those outputs as staged comparison
models; only paths selected by `config/pokemon_config.json` are classified as
active gameplay models. Comparison-only models can therefore be imported
without `-Cook`; cook them when they are promoted to runtime use.

The tracked Z-A recipe records exact package identities and the expected hash
list digest. The hash list, native resources, canonical imports, cooked model
data, and decoded textures remain private asset-depot content.

### Legacy Game Freak GFPAK importer

`import_gamefreak_gfpak_pokemon.ps1` performs the equivalent offline import
for the LGPE and Sword/Shield GFPAK/GFBMDL/GFBANM resource family. It extracts
the selected packages, preserves source material UV transforms and every
animation clip, exports regular/shiny and gender variants to canonical
`.phmodel` resources, validates them, and optionally cooks their `.phlo`
runtime objects. Source identity, depot layout, and animation provenance are
driven by the selected recipe. Sword recipes preserve their object-space
normal maps as native evidence without binding them to Phlosion's currently
tangent-space-only runtime normal slot. Native GFLX ambient masks are also
preserved and translated into grayscale runtime occlusion with Game Freak's
packed-channel formula; they must not be bound directly as red-channel glTF
occlusion maps.

```powershell
# Validate the default LGPE recipe.
.\tools\assets\import_gamefreak_gfpak_pokemon.ps1 -PlanOnly

# Import the Sword/Shield Caterpie family.
.\tools\assets\import_gamefreak_gfpak_pokemon.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_sword.json `
  -RomFsRoot "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Sword_v1.3.2_Merged_RomFS" `
  -SpeciesId 10,11,12 -Force -Cook
```

The tracked `gamefreak_pokemon_imports_lgpe.json` and
`gamefreak_pokemon_imports_sword.json` recipes describe reproducible source
and output identities. Original GFPAKs, extracted files, generated textures,
canonical models, and cooked resources remain private and ignored.

### Pokemon eye-expression animation

Native eye expressions stay bound to the animation clips that authored them.
The GFPAK exporter converts GFBANM eye-material value/vector tracks into the
canonical `UVScaleOffset` transform and retains the full eye atlas. Trinity
imports accept an eye material's unnumbered `UVScaleOffset` channel or, when
that material has no unnumbered channel, its lowest numbered channel such as
`UVScaleOffset1`. `UVScaleOffsetNormal` is never treated as a color-eye atlas.

Forge stores the neutral transform and the four animated scale/offset
components in PHAN. The Inspector, game renderer, and VFX preview sample them
from the selected model clip and its time rather than from the unrelated
continuous-material clock used by effects such as fire. This keeps the normal
neutral eyes at rest while allowing blinks, stern eyes, closed eyes, and other
authored shapes to appear during their source clips.

Each clip-bound eye track also stores its sampling semantics. Smooth,
high-precision pupil curves remain `linear`; compact rational atlas selectors
use `hold_source_frame` so fractional render times cannot sample the gaps
between expression cells. Classification is per track, not per game or
Pokemon, because some sources contain both representations. Run the complete
manifest/variant audit with:

```powershell
.\tools\assets\audit_kanto_eye_handling.ps1 `
  -OutputDirectory .\artifacts\eye-audit
```

The report covers every local regular, shiny, and female native manifest,
classifies atlas, skeletal-eyelid, mesh-visibility, and static/embedded eye
paths, and fails if a regular/shiny pair has inconsistent eye channels.

When importer behavior changes but the canonical `.phmodel` files do not need
to be extracted again, plan and run a bounded headless recook with:

```powershell
.\tools\assets\recook_material_animation_models.ps1 `
  -SourceTags LGPE,PLA,SV,Sword,ZA -ThrottleLimit 4 -PlanOnly

.\tools\assets\recook_material_animation_models.ps1 `
  -SourceTags LGPE,PLA,SV,Sword,ZA -ThrottleLimit 4
```

Use `-NumberedOnly` to select eye materials that specifically require a
numbered fallback, `-ModelNames` to constrain a repair to named manifests, and
`-AllMatching` when a cooker/material-ABI change requires every model selected
by the source tags rather than only models with animated eye UV tracks. The
script validates its roots, cooks without opening the editor, verifies one safe
object identity per model, and atomically replaces the corresponding
private-depot object directory.

## Shared native payload store

Both Game Freak importers publish geometry/animation bytes by SHA-256 under
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

After changing recipe or catalog ownership, review and then apply the two
workspace-source pruners. The native pruner only classifies canonical
Game-Freak-import stems; the review pruner only classifies numbered Pokemon
GLB/animation-set pairs that no longer have a catalog owner:

```powershell
.\tools\assets\prune_unreferenced_native_imports.ps1
.\tools\assets\prune_unreferenced_native_imports.ps1 -Apply
.\tools\assets\prune_unreferenced_review_models.ps1
.\tools\assets\prune_unreferenced_review_models.ps1 -Apply
```

Both are report-only by default, enforce model-root containment, reject reparse
points, and have synthetic dry-run/apply/idempotence contract tests.

The complete original-151 visible sex-difference checklist lives in
`kanto_gender_model_policy.json`. Validate the selected recipes after every
family addition or source promotion:

```powershell
.\tools\assets\validate_kanto_gender_models.ps1
```

`PhlosionForge finalize-cook` snapshots current model/runtime objects and reuses
the environment section from the current schema-2 manifest. It still runs full
strict validation before atomic publication. This lets a model-only or resumed
cook be finalized after regenerable Route 1 source cache has been removed;
`cook-route1` and `cook-all` remain the commands that intentionally rebuild
Route 1 from source.
