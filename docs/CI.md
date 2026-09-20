# CI

Status: Active
Type: Runbook
Last updated: 2026-09-20

CI is correctness-first and Windows-first. It separates a secret-free contract
job that runs for every pull request from a trusted full-build job that owns the
private dependency checkout. Hosted CI never cooks content, never reaches for a
GPU, and never substitutes for local private-content or three-API qualification.

## Jobs

### Source contracts (every push to `master`, every pull request, manual)

No repository secret is read in this job, so forks and Dependabot pull requests
receive the same checks as trusted branches:

- PowerShell/Python syntax for every tracked *and* newly added script
  (`tools/check_script_syntax.ps1` enumerates untracked, non-ignored files too,
  so a new script cannot reach `master` unparsed).
- Workflow trust, pin, scope, and evidence contract
  (`tools/test_ci_workflow.ps1`).
- Coverage-manifest contract, including zero-test and shrunken-scope rejection
  (`tools/test_ci_coverage_contract.ps1`).
- Runtime content preflight contract on synthetic fixtures
  (`tools/test_private_content_preflight.ps1`).
- Phlosion Engine/VFX/Packages commit-pin contract
  (`tools/test_phlosion_dependency_pins.ps1`).
- Catalog and promotion contract on synthetic fixtures
  (`tools/assets/test_kanto_model_promotions.ps1`).
- clang-format check over the C++ lines this change touches
  (`tools/format_check.ps1`).

Forks and Dependabot pull requests get a `CI coverage incomplete` warning and a
job-summary entry. That warning is the honest statement that the full Windows
build, CTest, and data validation were skipped: the pinned private Phlosion VFX
checkout needs a repository-scoped deploy key, and untrusted events must not
receive secrets.

Docs hygiene stays in the trusted job because its cross-repo engine and VFX
path checks resolve through the configured build tree; a secret-free checkout
would report those documentation paths as missing references.

### Windows build and tests (trusted events only)

Runs on `master` pushes, manual runs, and same-repository pull requests that are
not Dependabot:

1. Check out the repository and the pinned, read-only private Phlosion VFX
   dependency (`persist-credentials: false`).
2. Restore the vcpkg binary cache and install the pinned vcpkg executable.
3. Configure the standalone graph:
   `-DPAC_ENABLE_PRIVATE_ASSET_TESTS=OFF -DPAC_BUILD_EDITOR=OFF -DBUILD_TESTING=ON`.
   The private suite is an explicit opt-in, so the hosted job states its scope
   instead of shrinking silently.
4. Print `build/ci/coverage-manifest.json`, the machine-readable record of what
   this configuration selected and omitted.
5. Build and run the `fast` arena contracts before the full Debug build.
6. Build Debug, then run CTest through `tools/run_ctest_ci.ps1 -ExpectedMode source`.
   The wrapper refuses a selection with no tests, publishes failed test names as
   annotations, and keeps the raw CTest log.
7. Run `tools/check_docs_hygiene.ps1` and build `PAC_ValidateData`.
8. Always upload public-safe evidence: the coverage manifest, the CTest log, the
   CTest summary, and CTest's failed-test ledger.

Official Actions stay pinned to immutable release commits, the vcpkg executable
commit matches `vcpkg.json`'s builtin baseline, and a fresh upstream vcpkg commit
cannot change CI behavior between identical runs. The vcpkg registry clone keeps
its history so manifest overrides such as Lua 5.4.8 remain resolvable.

`PAC_Tools.phlosion_dependency_pins` compares the Engine and VFX commits in
`CMakeLists.txt` with local sibling checkout HEADs whenever that workspace is
available. That keeps local builds from silently validating newer sibling APIs
while a clean clone fetches the pinned revisions.

## Explicit test scopes

`CMakeLists.txt` declares private-content suites with per-suite prerequisites,
so a retired asset can no longer gate suites that never read it. The previous
blanket sentinel that keyed off a single mesh file is gone.

| Option | Scope | Behaviour |
| --- | --- | --- |
| `-DPAC_ENABLE_PRIVATE_ASSET_TESTS=OFF` (default) | Source suite | Every omitted private contract is enumerated in the coverage manifest with the suites and prerequisites it needs. |
| `-DPAC_ENABLE_PRIVATE_ASSET_TESTS=ON` | Content suite | Registers every intended content contract and fails configuration with the exact missing identities instead of registering fewer tests. |

`PAC_PRIVATE_CONTENT_ROOT` selects the root that private prerequisites resolve
against. Each configure writes `build/ci/coverage-manifest.json`; the CI CTest
wrapper (`tools/run_ctest_ci.ps1`) and the content qualification entrypoint both
read it. A configuration that selects nothing fails, and content mode refuses to
report a shrunken suite.

Use `ctest --test-dir build -N` for the actual registered count rather than a
stale fixed total.

## Private content and GPU qualification

These lanes stay local because they need the private corpus and representative
GPU hardware:

```powershell
# Publish-scope content qualification: preflight, Forge validation, content
# CTest with strict cooked assets, plus recorded identities and diagnostics.
.\tools\qualify_content.ps1 -BuildDir debug/ci_qualification/build-content -Config Debug

# Add the three-API renderer qualification and/or an existing editor build.
.\tools\qualify_content.ps1 -IncludeVisual
.\tools\qualify_content.ps1 -IncludeEditor -Config RelWithDebInfo

# Explicit depot sync is opt-in and requires the depot root; qualification
# itself never syncs, cooks, or mutates content.
.\tools\qualify_content.ps1 -SyncDepot -DepotRoot D:\PhlosionAssets
```

The run builds every default target (like the hosted build) before it grades
anything, so the contract binaries, the arena-logic suite, the game executable
used by the render matrix and the Forge validator are all freshly built. It then
runs `PhlosionForge validate` for the deep typed-object and dependency hash
integrity that existence checks cannot claim, keeps the existing promotion and
native-payload validators, and validates the data packs through
`PAC_ValidateData`.

`tools/qualify_content.ps1` refuses to run when:

- the asset catalog, promotion registry, cook manifest, authored scene, cooked
  objects, or cooked dependencies are missing or stale, or
- a requested GPU/editor scope has no suitable hardware or editor binary.

It always writes `debug/ci_qualification/content-qualification-report.json` with
the game revision, dependency pins, content identity, selected/excluded suites,
per-step status, and per-step logs. A missing prerequisite is an unqualified
run, never a green one.

Optional visuals reuse the existing tools instead of a hosted lane:

```powershell
.\tools\renderer_qualification.ps1 -BuildDir build -Config Release
.\tools\render_parity_matrix.ps1 -BuildDir build -Config Release
.\tools\runtime_visual_smoke.ps1 -BuildDir build -Config Debug
```

`tools/runtime_visual_smoke.ps1` now preflights the cooked bundle and the display
adapters, scopes the strict cooked-asset environment around the captured game
process, rejects a nonzero game exit and render/asset fallback diagnostics in
the captured logs. It fails early and explicitly instead of rendering on a
software adapter against a wall-clock timeout. Pass `-SkipPreflight` only for
unusual local diagnostics; that mode prints a diagnostic line rather than the
qualification `PASS` and must never be reported as a qualification run.

No GPU runner is registered, so there is no hosted GPU workflow. Registering a
representative private runner is a prerequisite before any hosted visual gate
can exist.

## What CI Does Not Run

- Private-content qualification. It needs the ignored corpus that is restored
  from the private asset depot.
- The three-API renderer qualification and parity matrix. These require a real
  GPU; the hosted lane is deliberately removed until a private runner exists.
- Release benchmark and perf gates. Hosted Windows runners are not
  representative enough for meaningful thresholds.
- Installer end-to-end smoke and merge-blocking perf/visual gates.
- Editor pairing and preview smoke, which need the first-party
  `PhlosionPackages` workspace.

A green source-suite run means the asset-independent C++ contracts, tooling
contracts, docs hygiene, data validation, and formatting passed. It does not
mean the private content tests, the runtime cooked checks, or three-API visual
parity passed. Run `tools/qualify_content.ps1` for those claims.

## Local Equivalent

The local source-suite equivalent is:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DPAC_BUILD_TOOLS=ON -DPAC_BUILD_EDITOR=OFF -DBUILD_TESTING=ON -DPAC_ENABLE_PRIVATE_ASSET_TESTS=OFF
cmake --build build --config Debug
.\tools\run_ctest_ci.ps1 -BuildDir build -Config Debug -ExpectedMode source
.\tools\check_docs_hygiene.ps1
cmake --build build --config Debug --target PAC_ValidateData
```

For a restored private corpus, use the content suite instead:

```powershell
.\tools\qualify_content.ps1
```

One-command local check:

```powershell
.\tools\full_check.ps1
```

`tools/full_check.ps1` configures the explicit content suite
(`-DPAC_ENABLE_PRIVATE_ASSET_TESTS=ON`) because it already requires the private
corpus for its promotion validation; pass `-SourceScope` to run only the
asset-independent suite instead. A fresh configure in the content scope fails
immediately when the corpus is incomplete instead of registering fewer tests.

Format check (changed files):

```powershell
.\tools\format_check.ps1
```

The format check uses `clang-format` from `PATH` when available and otherwise
discovers LLVM installed by Visual Studio. Set `PAC_CLANG_FORMAT` (or pass
`-ClangFormatPath`) to select a specific executable.
