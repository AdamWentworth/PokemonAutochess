# Build Targets — Data Packaging

This repo ships a small data cooker for release packaging. These targets help keep the workflow explicit and reproducible.

## Targets

- `PAC_ValidateData` — runs the cooker in validation-only mode.
- `PAC_PackData` — builds `content_pak/content.pak` from `config/` and `scripts/`.

## Examples

    cmake --build build --config Debug --target PAC_ValidateData
    cmake --build build --config Debug --target PAC_PackData

## Release flow (recommended)

1. Run `PAC_ValidateData` in CI to fail fast on bad content.
2. Run `PAC_PackData` for release builds to generate the packaged data bundle.
3. Ship the bundle alongside the game and set `PAC_DATA_PACK=content_pak/content.pak` in the release launch config.