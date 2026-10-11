# The app store (prototype)

Apps built into the firmware can only be mixed by building a firmware. The app
store lets a device download apps instead, so an app written for one CrossPlay
build runs on any other build that speaks the same **app API**
([app-api/README.md](app-api/README.md)).

Status: a working prototype. Math Quiz and Memory are packaged as downloadable
apps for trial; the built-in versions stay in the firmware.

## The pieces

| Piece | Where |
| --- | --- |
| App SDK: the C header, a C++ wrapper over FreeInkUI, the build script | `appsdk/` |
| The two trial apps | `appsdk/apps/mathquiz`, `appsdk/apps/memory` (they compile the built-in apps' own Core and Screens files) |
| Packaging tool: build, pack, check, publish | `appsdk/tools/cpapp.py` |
| Store server (Python standard library only) | `server/appstore/`, deploy notes in its README |
| The API record the server checks against | `docs/app-api/compat.json` |
| Runtime: wasm3 plus the `crossplay` imports | `lib/wasm3`, `src/apps_local/appstore/WasmApp.*` |
| The store app on the shelf | `src/apps_local/appstore/AppStore*`, `AppLibrary.*`, `WasmAppActivity.*` |

## How an app runs

An app is a `wasm32-wasip1` reactor module. It exports `cp_app_api_version`,
`cp_app_init`, `cp_app_render`, `cp_app_event` and optionally
`cp_app_suspend`, and imports only from `crossplay` (plus three WASI stubs so
`printf` links). The firmware runs it in wasm3:

- On the device, app code runs on its own task with a 40 KB stack, so a deep
  recursion never touches the UI task's stack. wasm3's native stack is capped at
  28 KB inside that, so recursion traps rather than overflowing.
- Linear memory is capped at the manifest's `memoryKB` (at most 4 MB) and is
  allocated in PSRAM first.
- Every call has a time budget (init 4 s, render and events 3 s, suspend
  1.5 s). wasm3 is patched to yield on every loop iteration, and the yield traps
  once the budget is spent, so an endless loop stops the app instead of the
  device.
- Every pointer an app passes is checked against its memory. A bad one stops
  the app with "It reached outside its own memory."
- A stopped app shows APP STOPPED with a sentence about why, and CLOSE goes
  back to the store. The firmware never restarts.
- Files: an app can only read and write plain names inside its own data folder,
  up to its `dataQuotaKB` and 64 files. Writes are atomic.
- Input arrives as events: Back, tap, swipe, the buttons, a one-shot timer,
  resume, and "shown" once a frame is on the panel. Back that the app does not
  keep closes it.

## Install layout on the card

```
/.crosspoint/apps/<id>/current          the installed version, one line
/.crosspoint/apps/<id>/v-<ver>/         manifest.json and the files
/.crosspoint/apps/<id>/data/            the app's own files, kept across updates
/.crosspoint/apps/.staging/             a download in progress
/.crosspoint/appstore.json              {"server": "https://..."} (optional)
```

Installing fetches the manifest, checks it matches what the catalog offered,
checks free space (the package plus 1 MB), downloads each file to staging while
hashing it, compares SHA-256 and size, moves the folder into place, then writes
`current` atomically and removes older version folders. A failure at any point
leaves the previous version running. The firmware checks every file's SHA-256
again at each launch, so a card edited on a computer cannot start a modified
app. Remove deletes the app and its data after asking.

## The protocol

All GETs, all JSON, no cookies. Paths are relative to the configured server
address, so the store can sit behind a reverse proxy under any prefix.

| Request | Answer |
| --- | --- |
| `GET v1/apps?api=1.0&from=1.0&touch=1&buttons=1&firmware=<version>` | Catalog. Each app has `compatible`; when true, the newest version this device runs with its `manifest` and `files` URLs, and `newer` when a later version needs newer firmware. When false, `reason` is a sentence for the user. `compatible=1` drops the ones that do not run. |
| `GET v1/apps/<id>` | Every published version with its API. |
| `GET v1/apps/<id>/<ver>/manifest.json` | The manifest the device installs. |
| `GET v1/apps/<id>/<ver>/files/<name>` | A file; immutable, cached forever. |
| `GET v1/api` | `compat.json`. |
| `POST v1/apps` (Bearer token) | Publish a `.cpapp`. |
| `DELETE v1/apps/<id>/<ver>` (Bearer token) | Withdraw a version. Installed copies keep working. |

The device works out compatibility itself as well, so a store that is wrong or
out of date can only hide apps, never install one that cannot run.

## Corner cases handled

- **Firmware too old for an app**: the store offers the newest version that
  runs, and says a newer version exists for newer firmware. If none runs, the
  app shows NOT HERE with "Needs app API 2.0; this firmware has 1.0. Update the
  firmware."
- **Firmware too new for an app** (after a breaking API change): NOT HERE with
  the first sentence of that version's release note from `compat.json`.
- **Firmware update breaks an installed app**: the installed list marks it and
  will not launch it, rather than letting it crash.
- **An app labelled with the wrong API**: refused at publish (imports checked
  against the declared API with signatures), refused at launch (every import
  must link, and the exported `cp_app_api_version` must match the manifest).
- **Hardware**: `requires: ["touch"]` hides an app from a buttons-only device,
  and the reverse.
- **Half-finished download, power loss, full card**: staging plus an atomic
  `current` pointer; space is checked first.
- **Tampering or a corrupt card**: SHA-256 checked on install and on each
  launch.
- **Misbehaving apps**: time budgets, memory cap, stack cap, pointer checks,
  file sandbox and quota. The app stops; the device does not.
- **Republishing a version**: refused. Versions are immutable so a device never
  sees a file change under a manifest it already checked.
- **Path tricks** in ids, versions or file names: refused by the same name
  rules on the server and the device; the server only serves files a manifest
  lists.
- **No Wi-Fi or no store address**: the store opens the Wi-Fi picker; the
  address comes from `/.crosspoint/appstore.json`, the build flag
  `CROSSPLAY_APPSTORE_URL`, or SET ADDRESS on the device.

## Not in the prototype

- **Signing.** Publishing needs a token, and files are hash-checked, but a
  device trusts whatever store it is pointed at. Next step: an ed25519 key per
  store, the public half baked into firmware, a signature over each manifest.
- **Network, radio, Bluetooth or sleep-screen APIs.** Memory's PLAY NEARBY is
  missing in the downloadable build for that reason.
- **Resume after deep sleep.** An app opens fresh after the device wakes;
  anything it wants kept must be saved in `cp_app_suspend`.
- **A data migration hook** between app versions. Apps read their own old
  files.
- **Updates in the background.** The store shows UPDATE on an installed app
  when the catalog has a newer one; nothing updates on its own.
- **The three-variant render rule** for new screens was skipped; the store
  screens follow the shelf's existing list and detail patterns.
- **Building apps needs wasi-sdk 25** (`appsdk/tools/build-app.sh`). The
  firmware does not.
