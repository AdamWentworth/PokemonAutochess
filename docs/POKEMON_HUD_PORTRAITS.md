# Pokemon HUD portraits

Status: Active
Type: Runbook
Last updated: 2026-09-13

The selected-unit HUD uses the original Pokemon HOME images for all 151 base
Kanto species. It keeps the 520 x 80 reference layout above the fixed Team Types
roster. A 52-pixel square portrait shares the identity column with the name and
stacked type icons; health, energy, attack, movement speed and moves retain their
existing columns. Board and bench selections use the same presentation code.

## Artwork and framing

`config/ui/pokemon_home_portraits.json` is the source of truth. Each entry records
the dex ID, game species key, original PNG path, immutable PokeAPI mirror URL,
SHA-256, byte size and a square face crop `[x, y, size]` in source pixels. All
originals are 512 x 512 RGBA PNGs and remain unmodified. Framing is applied using
sprite UVs, so later framing changes do not degrade or overwrite the source art.

The 151 crops were reviewed as contact sheets. Pokemon with multiple heads keep
their recognizable grouping; Pokemon such as Staryu use their central feature.
This first set covers the regular base species. Shiny and gender appearance
variants share the species portrait; Nidoran female and male have distinct dex
IDs and distinct portraits. This does not add new species to the gameplay roster.

`src/game/runtime/ui/PokemonPortraitData.h` is a generated table, not a second
authoring surface. `PokemonPortraits.h` normalizes punctuation/case and the Nidoran
symbols, resolves the species and emits a shared DebugSprite. Unknown/ambiguous
names do not borrow another Pokemon's image.

## Restore and edit

Restore missing images and validate the complete set:

```powershell
python tools/assets/restore_home_portraits.py
```

Offline verification, including the generated table:

```powershell
python tools/assets/restore_home_portraits.py --verify-only
```

After editing face crops, regenerate the table and review the gallery:

```powershell
python tools/assets/restore_home_portraits.py --verify-only --write-header
./tools/assets/preview_home_portraits.ps1
```

Use `-FullImages` to inspect the complete source images. Review sheets are written
under `debug/home-portraits/gallery`; they do not modify runtime art. Rebuild the
game/editor plugin after changing the generated table. The portrait manifest and
tools are versioned; image payloads stay in the ignored assets directory and the
private depot's `runtime/assets/ui/pokemon/home`. `--depot-runtime <private-root>`
publishes or verifies the second copy. Restores reject changed hashes, unexpected
paths/URLs, duplicate/missing species and invalid crop bounds.

## Performance and validation

Only configured roster species are included in startup prewarming. HOME portrait
textures are ordinary UI sprites and must not go through the larger card-art
proxy pipeline. Selection reuses cached textures; no extraction, network access,
image processing, or scene rendering happens during play.

All three renderers use one bilinear texture level for UI sprites. Vulkan must
not automatically generate world-texture mip chains for them: that softened
portraits and type icons relative to OpenGL and D3D12 at small HUD sizes.

The CPU HUD contract covers all 151 mappings, square UV bounds, name aliases,
unknown names and compact layout containment. Startup tests cover configured
species prewarming and direct UI sprite loading. Native GPU cases
`hud-inspection`, `hud-inspection-compact` and `hud-inspection-rattata` check
portrait content alongside names/types/stats. Editor cases `hud-inspection` and
`hud-inspection-stopped` cover both play and stopped previews. Run all affected
cases on OpenGL, Vulkan and D3D12 after visual changes, as required by the renderer
parity contract.
