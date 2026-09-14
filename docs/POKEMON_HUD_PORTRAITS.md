# Pokemon card artwork and HUD portraits

Status: Active
Type: Runbook
Last updated: 2026-09-13

Shop and starter cards and the selected-unit HUD share original TCG illustrations
for all 151 base Kanto species. The HUD keeps its 520 x 80 reference layout above
the fixed Team Types roster, with a separately framed 52-pixel square portrait.
Board and bench selections use the same presentation code.

## Sources and framing

`config/ui/pokemon_card_art.json` is the authority for source selection and crops.
BinderLedger's local catalog supplies 150 original scans: Base Set contributes
69 species, Jungle adds 45 and Fossil adds 36. Mew uses the original Wizards
[Black Star Promo #8 scan](https://pkmncards.com/card/mew-wizards-black-star-promos-8/).
The manifest records the catalog commit, exact local source filename or download
URL, collector number, dex ID, species key, dimensions, byte size and SHA-256.
BinderLedger is only an import source; the game does not access it during play.

Each original full-card scan stays byte-for-byte unchanged. Separate source-pixel
windows select the card illustration (`art: [x,y,w,h]`) and square portrait
(`portrait: [x,y,size]`). Illustration bounds and excluded evolution badges keep
printed card labels out of both windows. The card renderer covers its frame
opening without stretching the art as the viewport changes. Both views reuse
the same original texture through UVs, avoiding duplicated image payloads.

All 151 card and portrait crops were reviewed in contact sheets. Multiple-headed
Pokemon retain a recognizable grouping; Staryu uses its central feature. Scan
resolution and print texture vary in the source catalog. These are original
illustrations, without generated replacements or enhancement. Appearance variants
share the base species art; the two Nidoran species have distinct entries. This
asset coverage does not add unconfigured Pokemon to the gameplay roster.

`src/game/runtime/ui/PokemonArtworkData.h` is generated from the manifest.
`PokemonArtwork.h` handles aliases and shared framing. Known Pokemon cards use
the selected catalog art even when an older snapshot carries a legacy image
path. Items and unknown custom cards retain their explicit image/atlas behavior.

## Restore and edit

Restore from the private depot, or import from a BinderLedger checkout:

```powershell
python tools/assets/restore_pokemon_card_art.py --depot-runtime D:/ProjectData/Games/PokemonAutochess/Assets/pokemon-autochess/runtime
python tools/assets/restore_pokemon_card_art.py --binderledger-root D:/Projects/Apps/BinderLedger
```

Only a missing Mew scan requires a web download when it is absent from the depot.
Offline verification also checks that the generated table matches the manifest:

```powershell
python tools/assets/restore_pokemon_card_art.py --verify-only
```

After editing crop windows, regenerate the table and review both views:

```powershell
python tools/assets/restore_pokemon_card_art.py --verify-only --write-header
./tools/assets/preview_pokemon_card_art.ps1 -View Art
./tools/assets/preview_pokemon_card_art.ps1 -View Portrait
```

Use `-View Source` to inspect complete scans. Review sheets go under
`debug/tcg-art/gallery` and do not modify runtime art. Rebuild the game and editor
plugin after changing the table. The manifest, code and tools are versioned;
payloads live in ignored `assets/ui/pokemon/tcg` and the matching private depot
directory. `--depot-runtime` also publishes or verifies that second copy.
Restoration rejects changed scan hashes, missing/duplicate species, unexpected
source/destination paths and crops outside the illustration or across badges.

## Performance and validation

Only configured roster species are prewarmed. TCG scans are ordinary cached UI
textures, shared by cards and portraits, without the legacy card proxy pipeline.
No network access, extraction or image processing happens during play.

All three renderers use one bilinear texture level for UI sprites. Vulkan must
not automatically generate world-texture mip chains for them: that softened
portraits and type icons relative to OpenGL and D3D12 at small HUD sizes.

CPU contracts cover all 151 identities, portrait bounds, card aspect preservation
at multiple sizes, legacy path replacement, item behavior, prewarming and the
fixed HUD layout. The restoration verifier checks original files and authoring
metadata. Native GPU cases cover selected Bulbasaur and bench Rattata, compact
HUD/shop cards and wide starter cards. Editor cases cover playing and stopped
inspection and starter cards. Qualification reports live under
`debug/tcg-art/{native-final,editor-final}`.

```powershell
./tools/render_parity_matrix.ps1 -Config RelWithDebInfo -Cases hud-inspection,hud-inspection-compact,hud-inspection-rattata,hud-starter-wide,hud-shop-compact -OutputDir debug/tcg-art/native-final
./tools/housekeeping/check_editor_workflow.ps1 -Cases hud-inspection,hud-inspection-stopped,hud-starter-cards -OutputDirectory debug/tcg-art/editor-final
```

Run affected cases on OpenGL, Vulkan and D3D12 after visual changes. Expected
content guards check the TCG portrait and retain checks for type icons, stats,
fixed Team Types, frame fill and visible scenes. Blank or stale HOME portrait
content must fail the TCG portrait guard.
