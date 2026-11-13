# Legacy RampBugFix integration

## Status

This optional integration adapts the legacy RampBugFix algorithm to the current
CS2Fixes movement path. `cs2f_rampbugfix_enable` defaults to `false`. Enabling it
does not bypass the exact binary identity and signature checks.

The audited Windows profile targets CS2 server version 2000942, patch 1.41.9.6,
dated 2026-11-04. Its `server.dll` size is 34672792 bytes and its FNV-1a 64 hash
is `64e05e7cdc591254`. The adapter requires every target signature to match once.

The audited Linux binary is server version 2000940, patch 1.41.9.5, dated
2026-11-02. Its `libserver.so` size is 41807736 bytes and its FNV-1a 64 hash is
`de50ca3604b5443f`. Its SHA-256 is
`503c5b0e3c7dc0c1c66f32d51437da6896cf7eb3a7dd299db9c01f89199b59f5` and its
ELF Build ID is `b9eac3d151886d4fc820d2e1d1f1d8642daa54c9`. All seven required movement and
trace targets and the 64-byte movement-filter layout were statically verified
on instance 28056. `CategorizePosition` has one `.text` match at ELF VA
`0x15BAE40`; its direct call at `0x15E9A02` confirms the expected ABI.

A second exact Linux profile covers Valve build 26445667 from depot 2347773,
manifest 8513165868182791792. Its `libserver.so` is 41850680 bytes, has FNV-1a
64 hash `96c2563722232d09`, SHA-256
`ee0cc32a4b83c4a11bdd6bc32c5347000165ee236b5c6ff0b1901f49827405e3`, and ELF
Build ID `5ea0e482b46eb71c4f9615c48b34ddc36f55ca40`. All seven signatures match once.
`CategorizePosition` is at ELF VA `0x15C5400`, with a direct call at
`0x15F40C2`. Static comparison found equivalent ABI-relevant instructions for
the audited movement, trace, filter, hull and mask paths, including the first
0x803 bytes of the trace function, the filter constructor and 0x40-byte sized
deleting destructor, the first 0x9a0 bytes of `TryPlayerMove`, and the first
0x805 bytes of `CategorizePosition`. This is static-only evidence. The newer
binary also requires its paired build-specific gamedata override; this adapter
does not alter tracked gamedata.

These profiles establish static ABI compatibility only. They do not establish
gameplay correctness, compatibility with later game builds, or compatibility
with other movement detours.

## Design and lifecycle

The module reuses CS2Fixes' existing `ProcessMovement` KHook. It installs one
five-argument `TryPlayerMove` member hook and one `CategorizePosition` member
hook only when the cvar was enabled during plugin load and the complete audited
profile matches. The fifth nullable `bool*` argument is forwarded untouched.

Both optional callbacks must execute while corrections remain inert. A later
game-frame boundary with no active movement scope arms corrections. Runtime
disable stops corrections immediately and clears all player state. Re-enabling
requires fresh observations of both callbacks and another safe frame boundary.
Enabling at runtime cannot prepare a module that was disabled or unsupported at
load.

Spawn, death, disconnect, round prestart, level shutdown and map startup clear applicable state.
Pawn replacement, slot reuse, an out-of-band origin change, recursive movement,
skipped original calls and teleports during `TryPlayerMove` also reject stale
predictions. Movement state stores handles and values rather than long-lived raw
entity pointers.

Hook registration is fail closed. A partial registration is removed through
KHook's synchronous removal path before load continues. Normal unload first
stops corrections and synchronously removes both optional callbacks, then runs
the existing CS2Fixes manager cleanup. Metamod's plugin-owned KHook unloader
retains its own hook-ID barrier and delays library release until every tracked
ID retires, including forced unload. A normal unload requested from inside an
active movement scope is refused so synchronous removal cannot wait on its own
detour lock. Corrections and scope IDs are invalidated before refusal, and the
unload can be retried after the callback unwinds. The module never
independently queues an asynchronous removal that could prevent Metamod
attaching its completion callback.

## Native ABI inputs

Both audited platforms use velocity at `CMoveData+0x38` and origin at
`CMoveData+0xC8`. The adapter calls the native movement-filter constructor,
uncached five-argument bbox trace, dynamic hull-minimum and hull-maximum getters,
and player solid-mask getter. It does not call the cached six-argument trace
helper because that helper requires a valid engine cache at movement services
offset `0x61C`.

The trace storage is 192 bytes and 16-byte aligned. Used fields are start at
`0x78`, end at `0x84`, normal at `0x90`, fraction at `0xAC`, and start-in-solid
at `0xBB`. Windows uses a 72-byte movement filter with iterate at `0x40`.
Linux uses a 64-byte filter with iterate at `0x3A`. Compile-time assertions bind
the SDK declarations to those audited sizes.

## Legacy behavior and bounded changes

The extracted core keeps four bumps, the 0.75-unit pierce distance, plane and
velocity thresholds, the disabled backward-fraction rejection introduced after
the Call to Arms update, and the effective integer `-1 / 128` zero adjustment.

The adaptation rejects malformed or mismatched traces, exercises all ten
intended 0.1 through 1.0 pierce distances, validates the connector trace before
using it, rejects stale movement IDs, and refuses non-finite corrections. These
are safety and determinism changes around the legacy algorithm. They require
gameplay validation before release.

## Provenance and licence

The core is adapted from `Interesting-exe/CS2Fixes-RampbugFix` at commit
`dc9ad2ec896bf0818ffe01e839dba20ce4891fe5`, whose repository licence is GNU GPL
version 3. Its README credits the original ramp-fix approach to zer0. This
integration uses no code or binary from the legacy bundled funchook dependency.
The newer CS2KZ algorithm was consulted only for current declarations and was
not substituted for the requested legacy behavior. See `NOTICE` and `LICENSE`.

## Validation boundary

The engine-independent core tests cover valid and malformed traces, pierce
search, plane transitions, correction acceptance, stale predictions, ground
nudge behavior, teleports represented as origin mismatch, and numeric bounds.
The adapter lifecycle tests cover callback observation, safe-frame arming,
runtime toggles, registration failure and shutdown. Full Windows and Linux
compilation remains separate from runtime acceptance.

An authorized isolated runtime test must cover ramps, seams, walls, steps,
standing and ducked hulls, jump, ladder, landing, teleport, spawn, death,
disconnect, round restart, map change, speed modification, cvar off/on, normal
unload, forced unload, the nullable fifth argument, and SwiftlyS2 v1.5.0 in both
load orders. The legacy standalone RampBugFix plugin must not be loaded at the
same time. No production load, restart or deployment is part of this local
integration work.

CS2Fixes' existing speed modifier stores scaled frame time in one global value.
The RampBugFix integration preserves that behavior. Nested ProcessMovement or a
foreign movement hook that recalls it while speed scaling is active remains a
runtime compatibility risk and belongs in the isolated nested-hook test matrix.
