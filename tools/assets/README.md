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
