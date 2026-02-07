# Release Build Checklist

This checklist documents the minimal release flow for packaged data builds.

## Release Steps

1. Build the game binaries (Release config).
2. Validate data with the cooker.
3. Pack data into `content_pak/content.pak`.
4. Ship the bundle alongside the executable.
5. Set `PAC_DATA_PACK=content_pak/content.pak` in the release launch config.

## Commands (Windows example)

    cmake --build build --config Release --target PokemonAutochess
    cmake --build build --config Release --target PAC_ValidateData
    cmake --build build --config Release --target PAC_PackData

## Artifacts to ship

- `PokemonAutochess.exe`
- `content_pak/content.pak`
- `assets/` (runtime assets)
- Any required runtime DLLs from your build environment

## Notes

- Dev builds typically skip packing for faster iteration.
- Use `PAC_DATA_ROOT` and `PAC_ASSET_ROOT` only if you need custom runtime roots.