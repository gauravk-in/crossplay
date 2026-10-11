# The app API

Downloadable apps (see [../app-store.md](../app-store.md)) are WebAssembly
modules that call the firmware through one import module, `crossplay`. That set
of functions is the **app API**. It has a version, `major.minor`, and three
places that must agree on it:

| Where | What it holds |
| --- | --- |
| `appsdk/include/crossplay_app.h` | `CP_API_VERSION_MAJOR/MINOR`, `CP_API_COMPATIBLE_FROM_MAJOR/MINOR`, and one `CP_IMPORT(name)` per function. Apps compile against it, and the firmware takes the version it runs from it (`firmwareApi()` in `AppStoreCore.cpp`). |
| `src/apps_local/appstore/WasmApp.cpp` | `kImports`: the functions the firmware actually links, with their wasm3 signatures. |
| `docs/app-api/compat.json` | The history: every version, what it added or removed, and where compatibility breaks. |

`host-tests/appstore/check_api.py` fails when they disagree. It runs as
part of `host-tests/appstore/run.sh`.

## What runs where

A firmware that provides API `P` and still runs everything from `F` up runs an
app built against `A` when `F <= A <= P`. The store server applies the same
rule to pick which version of an app to offer, and the device applies it again
before installing and before every launch, so a card moved between devices
never starts an app the firmware cannot run.

On top of the version check, the firmware refuses to start a module that
imports a function it does not link (`m3fork_UnlinkedImports`), and the server
refuses to publish one that imports a function its declared API does not have,
or has with another signature. Either catches a mislabelled app.

## Changing the API

**Adding a function** (the normal case) is a minor bump and breaks nothing:

1. Declare it in `crossplay_app.h` with `CP_IMPORT(name)` and bump
   `CP_API_VERSION_MINOR`.
2. Add it to `kImports` in `WasmApp.cpp`. Leave the `CP_API_COMPATIBLE_FROM`
   defines alone.
3. In `compat.json`: set `current`, add a `versions` entry with `added` and a
   one-line `notes`, and add the function under `functions` with its
   `signature` and `since`.
4. `sh host-tests/appstore/run.sh`.

Apps built against the old minor keep running. Apps that use the new function
declare the new minor, and older firmware is told to update (the store says
"Needs app API 1.1; this firmware has 1.0. Update the firmware.").

**Changing or removing a function** is a break. Avoid it: add `fill2` beside
`fill` instead of changing `fill`, and keep the old one linked for as long as
the flash allows. When a break is unavoidable:

1. Bump the major (or a minor with an explicit `compatibleFrom`, if only the
   apps of some minors are affected), and raise the `CP_API_COMPATIBLE_FROM`
   defines to the oldest version whose apps still run.
2. In `compat.json`: the new version entry gets `"breaking": true`, its
   `compatibleFrom`, `removed`/`changed` lists, and `notes` whose first sentence
   says what changed in words a person holding the device understands. The
   store shows that sentence next to every app the break stops.
3. Mark removed functions with `"until": "<version>"` rather than deleting them,
   so the server can still check apps built for older versions.
4. Rebuild and republish the affected apps against the new version. Until
   their authors do, the store keeps offering the old version to old firmware
   and tells new firmware why the app is missing.

Signatures use wasm3's spelling: the return type, then the parameters, with
`i` = i32, `I` = i64, `f` = f32, `F` = f64, `v` = void. `v(iiii)` takes four
i32 and returns nothing.

## Rules the API itself keeps

- Every pointer an app passes is a byte offset into its own memory with a
  length; the firmware checks the whole span and stops the app if it reaches
  outside. Structures cross the boundary as little-endian bytes with fixed
  layouts, never as C++ types.
- No function blocks for long. Anything slow (network, big file I/O) gets a
  start call and an event when it finishes, so the watchdog budgets stay
  meaningful.
- Fonts, colours and refresh modes are small enums the firmware maps to its
  own; an app never learns a firmware-internal id.
