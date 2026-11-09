# Oz-Lin upstream synchronization, 2026-11-09

## Scope and identities

This is a source synchronization checkpoint before RampBugFix integration. It is
not a production-ready build or a deployment. No remote push was performed.

- Original checkout: `../CS2Fixes`, branch `ozlin-dev`, commit
  `34aaed13c560204dd4c2131c4c748c032cd28ce5`.
- Isolated repository: `CS2Fixes-ozlin-sync`, branch
  `integration/ozlin-upstream-sync-20261109`.
- Public upstream: https://github.com/Source2ZE/CS2Fixes, release `v2.1` and
  fetched `main`, both `687f51acb2b717218f6e1e42710f14cfd58a1960`.
- Merge base: `62c37fb368bdd5eaadb6e93a3a6645bb190cb9bf`.
- SDK: `30c1a859fe846846081ce0e6125c23e497a9a186`.
- SDK manifests: `1600a504c89d81e15938719336b7f750b8417d47`.

The independent local clone does not use hardlinks. It was created from committed
history only. The original checkout's untracked `vendor/protobuf-3.21.8/` was not
copied, cleaned, or staged. The RampBugFix checkout was not modified. The original
Oz-Lin tip is the merge's first parent, so historical customizations remain
available even when current implementations are superseded by upstream.

## Conflict decisions

The initial merge reported 22 conflicted paths. Every source conflict was compared
with both the net fork delta since the merge base and the public release. This was
not a replacement of the original checkout or a reset of its history.

1. `.github/workflows/build.yml`, `Dockerfile`: use the release's SteamRT build
   containers and dependency setup rather than retaining the old Bullseye/Clang
   installation steps.
2. `AMBuilder`, `CS2Fixes.vcxproj`, `CS2Fixes.vcxproj.filters`: use the release's
   KHook/SDK source lists, including VScript helpers. Remove obsolete SDK headers
   and duplicate TopDefender entries created by the merge.
3. `sdk`: use the exact release pin above rather than the older fork SDK.
4. `src/cs2fixes.cpp`, `src/cs2fixes.h`, `src/common.h`: retain the release's
   lifecycle and KHook migration. Do not reinsert old SourceHook declarations or
   hook bodies now implemented in `virtual_hooks.cpp`.
5. `src/cs2_sdk/entity/globaltypes.h`: remove fork-local engine types superseded
   by the new SDK. Do not keep old `EmitSound_t` layout assumptions.
6. `src/utils/utils.cpp`, `src/utils/utils.h`, `src/commands.cpp`: retain the
   release's SDK-backed client list, Steam interface helpers and new commands.
7. `src/topdefender.cpp`, `src/topdefender.h`, `src/events.cpp`: the public release
   already contains the expanded TopDefender feature. Keep its current networked
   score assignments, MAX_PLAYERS usage and original-clan-tag restoration.
8. `src/entwatch.cpp`, `src/entwatch.h`, `src/playermanager.cpp`,
   `src/playermanager.h`: use the current event signatures, config-loaded guards,
   player fields and clan-tag preservation instead of obsolete implementations.
9. `src/map_votes.cpp`: use the release's explicit floating-point conversion.
10. `cfg/cs2fixes.cfg`: adopt the release layout and defaults, including the newer
    `Top Defender` clan tag. Existing runtime configuration was not edited.

Two additional semantic conflicts had no Git conflict marker:

- `ccsplayercontroller.h` acquired a second `SetClanTag` with the old name-swap
  trick. Removed it in favor of the single upstream method and `ResetClanTag`.
- The old name-trimming workaround survived in `playermanager.cpp`. Removed it
  because upstream deliberately removed the name-swap behavior.

## Retained customization and migration boundaries

The only production-source delta from `v2.1` is in `src/detours.cpp`:

- Preserve the existing `css_countdown_text <seconds> "<text>"` bridge, with its
  existing timer gate and message formatting.
- Add the explicit `engine2/ivengineserver2.h` include now required by the bridge.
  The first full compilation exposed the missing complete interface type after
  the upstream include refactor; the declaration alone was insufficient.

The public release already contains the old fork's expanded TopDefender and other
upstream-derived improvements. Legacy grenade purchase logic was not resurrected.
`cs2f_weapons_enable` and `cs2f_votemanager_enable` remain disabled in the shipped
default config. The planned SwiftlyS2 purchase replacement, native carry-limit
cvars and map-configuration ownership are separate from this source merge.

The bridge still targets the existing CounterStrikeSharp command. This does not
prove that an equivalent SwiftlyS2 consumer is present. The legacy bridge formats
map text into a server command without dedicated quoting/sanitization and has no
independent enable cvar. Those are follow-up hardening items, not new behavior
introduced during synchronization.

## Configuration-path verification

For the exact `v2.1` tree reviewed here:

- `CS2Fixes::Load` calls `ParseCfg("cs2fixes")`.
- `Hook_SwitchToLoop` invokes `PreLevelLoad` during `levelload`.
- `Hook_ApplyGameSettings` invokes the parser again before map-vote/migration work.
- `CCfgParser::ExecuteConfigs` reads the main plugin cfg, then a nonempty map name's
  `addons/cs2fixes/cfg/maps/<map>.cfg`.
- `cs2f_cvarwhitelist_enable` and `configs/cvar_whitelist.jsonc` control the game's
  allowed map commands. They are not a guard around the above parser calls.
- `cs2f_votemanager_enable` gates map-vote work, not those parser calls.

No file-execution opt-out was identified in these call sites or the cvar definitions
of `v2.1` / original `34aaed13`. This statement is limited to those revisions. The
user has supplied an additional configuration lead; the associated SourceTree
image identifies historical branch commits, not an identified opt-out patch. The
exact setting still needs identification before choosing any additional switch or
production migration procedure. Do not confuse
blocking map-issued commands with skipping plugin-owned cfg files, or add a new
setting while an applicable existing setting is still being investigated.

## Validation

- `python tests/test_ozlin_sync.py`: 12 source-contract tests. These check the
  release identity, retained history, absence of unresolved conflicts, exact
  preserved bridge delta, old clan-tag removal, KHook migration, default feature
  toggles and SDK pin. They do not exercise the engine or claim ABI correctness.
- `git diff --check v2.1`: checks only the integration delta. The broader merge
  against the old fork reports pre-existing whitespace in newly imported upstream
  build scripts/particle sources; those files are intentionally identical to the
  public release rather than reformatted as part of this task.
- Required `docker compose up`: attempted, unavailable because Docker is not
  installed/on PATH on this PC. No Linux compilation was performed.
- Windows x64 Release build: **passed**, including final DLL linking and package
  creation after adding the missing interface include. Build logs are in the
  local ignored `validation-output/` folder. The DLL is under
  `build/package/cs2/addons/cs2fixes/bin/win64/cs2fixes.dll`. Compiler warnings remain
  in the upstream source; a successful build is not a clean-warning claim.

Build environment: VS 2022 17.14.44, MSVC 19.44, Python 3.12,
AMBuild `5b0aa725beef33c9cf6a1b00659a010c3d9120d3`,
Metamod `ea513d8960c52d266a1c5620dc762def0dd8bce8`,
KHook `0ca2b01363aff9fa11ad73b1b08dfdc2a195fb3a`, and the release SDK pins.
Dependencies were materialized under `.build-deps/`; existing installations and
original source trees were not updated. The old local AMBuild lacked DetectProtoc,
so current official AMBuild was used without a global installation.

## Not validated / release blockers

1. `upstream/dev` is seven commits ahead of `v2.1`, including replacement of
   SayTextFilter signatures and a 2026-11-04 gamedata update. This checkpoint pins
   the requested public release; it must not be labeled current-CS2 compatible.
2. No CS2 server load/unload, map transition, live CountDown, hook ordering,
   multiplayer test or SwiftlyS2 1.5.0 coexistence test was run.
3. RampBugFix is not integrated in this checkpoint. Its current TryPlayerMove ABI,
   particularly the fifth argument, still requires evidence before implementation.
4. Production cfg ownership and the user's additional cfg-disable commit lead
   remain to be reconciled. Nothing was copied into a server installation.

The checkpoint can be reviewed or abandoned by using the isolated clone; no
rollback of the original branch or server is necessary because neither changed.
