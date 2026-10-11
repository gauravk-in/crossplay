# CrossPlay app store server

Serves the catalog and files that the APP STORE app on the device downloads.
Python 3.9+ standard library only; no database, the data folder is the store.
Protocol and design: [docs/app-store.md](../../docs/app-store.md).

## Run it

Directly:

```sh
cd server/appstore
APPSTORE_TOKEN=$(openssl rand -hex 24) python3 -m crossplay_store.server --data ./data --port 8080
```

With Docker, from the repository root:

```sh
export APPSTORE_TOKEN=$(openssl rand -hex 24)   # keep it: publishing needs it
docker compose -f server/appstore/compose.yaml up -d --build
```

Settings (flag or environment):

| Flag | Env | Default |
| --- | --- | --- |
| `--data` | `APPSTORE_DATA` | `./data` |
| `--api` | `APPSTORE_API` | `docs/app-api/compat.json` in this checkout |
| `--host` | `APPSTORE_HOST` | `0.0.0.0` |
| `--port` | `APPSTORE_PORT` | `8080` |
| | `APPSTORE_TOKEN` | unset: publishing over HTTP is off |

Open `http://<host>:8080/` for a page listing what is published and the API
history; `/healthz` answers `ok`.

**Put it behind HTTPS** (Caddy, nginx, a tunnel) for anything outside your own
network: the token travels in a header. The device accepts either. The store
uses relative URLs, so serving it under a prefix (`https://example.com/store/`)
works; give the device that full address.

When the firmware's app API changes, deploy the new `compat.json` with it (it is
copied into the image at build time), or the server refuses apps built for the
new version.

## Publish the two trial apps

Building needs [wasi-sdk 25](https://github.com/WebAssembly/wasi-sdk/releases)
(`WASI_SDK=/path/to/wasi-sdk-25.0-...`).

```sh
appsdk/tools/cpapp.py build appsdk/apps/mathquiz -o out/
appsdk/tools/cpapp.py build appsdk/apps/memory -o out/
appsdk/tools/cpapp.py check out/org.crossplay.mathquiz-1.0.0.cpapp
appsdk/tools/cpapp.py publish out/org.crossplay.mathquiz-1.0.0.cpapp --server https://store.example.com --token "$APPSTORE_TOKEN"
appsdk/tools/cpapp.py publish out/org.crossplay.memory-1.0.0.cpapp   --server https://store.example.com --token "$APPSTORE_TOKEN"
```

On the server machine, `--data ./data` instead of `--server/--token` writes
straight into the data folder; restart the server afterwards so it rereads it.

To pull a version: `curl -X DELETE -H "Authorization: Bearer $APPSTORE_TOKEN"
https://store.example.com/v1/apps/<id>/<version>`. Devices that installed it
keep it. Published versions never change; fix something by publishing a higher
version.

## Point a device at it

Any of these, first one wins:

1. `/.crosspoint/appstore.json` on the card: `{"server": "https://store.example.com"}`
2. Build flag `-DCROSSPLAY_APPSTORE_URL=\"https://store.example.com\"` in
   `platformio.local.ini`
3. On the device: APP STORE, then SET ADDRESS when it says it has no store.

## Tests

```sh
sh host-tests/appstore/run.sh     # device-side rules, API agreement, these server tests
```
