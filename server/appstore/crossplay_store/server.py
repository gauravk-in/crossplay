"""The CrossPlay app store server. Python standard library only.

    python3 -m crossplay_store.server --data ./data --api ../../docs/app-api/compat.json

Serves a catalog that a device asks for with its own app API and inputs, and
the files of each app version. See docs/app-store.md for the protocol and
server/appstore/README.md for deploying it.

Layout of --data:

    apps/<id>/<version>/manifest.json   the manifest the device installs
    apps/<id>/<version>/<file>          every file the manifest lists

A published version is immutable: the device checks every file against the
manifest's SHA-256, and a version that changed under it would fail that check
on the next install. Publish a new version instead.
"""

import argparse
import hmac
import html
import io
import json
import logging
import os
import re
import shutil
import sys
import tempfile
import threading
import zipfile
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

from . import rules

log = logging.getLogger("appstore")

MAX_UPLOAD_BYTES = rules.MAX_PACKAGE_BYTES + 256 * 1024
_VERSION_PATH_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


class Store:
    """Every published app version, read from disk, and the catalog logic."""

    def __init__(self, data_dir, api_db):
        self.data_dir = data_dir
        self.apps_dir = os.path.join(data_dir, "apps")
        self.db = api_db
        self.lock = threading.Lock()
        self.apps = {}  # id -> [manifest, ...], newest version first
        os.makedirs(self.apps_dir, exist_ok=True)
        self.reload()

    def reload(self):
        apps = {}
        for app_id in sorted(os.listdir(self.apps_dir)):
            if not rules.valid_app_id(app_id):
                continue
            versions = []
            app_dir = os.path.join(self.apps_dir, app_id)
            if not os.path.isdir(app_dir):
                continue
            for version in os.listdir(app_dir):
                path = os.path.join(app_dir, version, "manifest.json")
                if not _VERSION_PATH_RE.match(version) or not os.path.isfile(path):
                    continue
                try:
                    with open(path, encoding="utf-8") as f:
                        manifest = rules.check_manifest(json.load(f))
                except (OSError, ValueError) as e:
                    log.error("skipping %s %s: %s", app_id, version, e)
                    continue
                if manifest["id"] != app_id or manifest["version"] != version:
                    log.error("skipping %s %s: manifest names %s %s", app_id, version, manifest["id"],
                              manifest["version"])
                    continue
                versions.append(manifest)
            if versions:
                versions.sort(key=lambda m: rules.parse_app_version(m["version"]), reverse=True)
                apps[app_id] = versions
        with self.lock:
            self.apps = apps
        log.info("%d apps, %d versions", len(apps), sum(len(v) for v in apps.values()))

    def snapshot(self):
        with self.lock:
            return dict(self.apps)

    # --- The catalog -------------------------------------------------------

    @staticmethod
    def _size(manifest):
        return sum(f["size"] for f in manifest["files"])

    @staticmethod
    def _urls(manifest):
        # Relative to the server address the device was given, so the store
        # works behind a reverse proxy at any path.
        base = "v1/apps/%s/%s" % (manifest["id"], manifest["version"])
        return base + "/manifest.json", [dict(f, url="%s/files/%s" % (base, f["name"])) for f in manifest["files"]]

    def _missing(self, manifest, touch, buttons):
        if "touch" in manifest["requires"] and not touch:
            return "Needs a touch screen."
        if "buttons" in manifest["requires"] and not buttons:
            return "Needs buttons."
        return None

    def entry(self, versions, provides, compatible_from, touch, buttons):
        latest = versions[0]
        chosen = None
        for m in versions:
            api = rules.parse_api(m["api"])
            if rules.api_compatible(api, provides, compatible_from) and not self._missing(m, touch, buttons):
                chosen = m
                break
        shown = chosen or latest
        out = {
            "id": latest["id"],
            "name": shown["name"],
            "summary": shown["summary"],
            "author": shown["author"],
            "category": shown["category"],
            "latest": latest["version"],
            "api": shown["api"],
            "compatible": chosen is not None,
        }
        if chosen is not None:
            manifest_url, files = self._urls(chosen)
            out.update(version=chosen["version"], size=self._size(chosen), manifest=manifest_url, files=files)
            if chosen is not latest:
                out["newer"] = {"version": latest["version"], "api": latest["api"]}
        else:
            reason = self._missing(latest, touch, buttons) or self.db.why_not(
                rules.parse_api(latest["api"]), provides, compatible_from
            )
            out.update(version="", size=self._size(latest), reason=reason)
        return out

    def catalog(self, provides, compatible_from, touch, buttons, only_compatible):
        apps = []
        for _, versions in sorted(self.snapshot().items(), key=lambda kv: kv[1][0]["name"].lower()):
            e = self.entry(versions, provides, compatible_from, touch, buttons)
            if e["compatible"] or not only_compatible:
                apps.append(e)
        return {
            "api": {"provides": rules.api_str(provides), "compatibleFrom": rules.api_str(compatible_from)},
            "serverApi": self.db.doc["current"],
            "apps": apps,
        }

    def versions_of(self, app_id):
        versions = self.snapshot().get(app_id)
        if not versions:
            return None
        return {
            "id": app_id,
            "versions": [
                {"version": m["version"], "api": m["api"], "size": self._size(m), "requires": m["requires"]}
                for m in versions
            ],
        }

    def path_of(self, app_id, version, name):
        """The file on disk, or None for anything that is not a published file."""
        if not rules.valid_app_id(app_id) or not _VERSION_PATH_RE.match(version):
            return None
        versions = self.snapshot().get(app_id) or []
        manifest = next((m for m in versions if m["version"] == version), None)
        if manifest is None:
            return None
        if name != "manifest.json" and name not in {f["name"] for f in manifest["files"]}:
            return None
        return os.path.join(self.apps_dir, app_id, version, name)

    # --- Publishing --------------------------------------------------------

    def publish(self, package_bytes):
        """Adds one version from a .cpapp (a zip of manifest.json and its files).
        Returns the manifest; raises rules.Invalid with a reason."""
        try:
            z = zipfile.ZipFile(io.BytesIO(package_bytes))
        except zipfile.BadZipFile:
            raise rules.Invalid("The package is not a .cpapp (zip) file.")
        names = z.namelist()
        if "manifest.json" not in names:
            raise rules.Invalid("The package has no manifest.json.")
        info = {i.filename: i for i in z.infolist()}
        if sum(i.file_size for i in info.values()) > rules.MAX_PACKAGE_BYTES + 64 * 1024:
            raise rules.Invalid("The package is larger than 4 MB.")
        try:
            manifest = rules.check_manifest(json.loads(z.read("manifest.json").decode("utf-8")))
        except (ValueError, UnicodeDecodeError) as e:
            if isinstance(e, rules.Invalid):
                raise
            raise rules.Invalid("manifest.json is not valid JSON.")
        listed = {f["name"]: f for f in manifest["files"]}
        extra = set(names) - set(listed) - {"manifest.json"}
        if extra:
            raise rules.Invalid("The package holds files its manifest does not list: %s." % ", ".join(sorted(extra)))
        contents = {}
        for name, f in listed.items():
            if name not in info:
                raise rules.Invalid("The package is missing %s." % name)
            data = z.read(name)
            if rules.file_entry(name, data) != {"name": name, "size": f["size"], "sha256": f["sha256"]}:
                raise rules.Invalid("%s does not match the size and SHA-256 in the manifest." % name)
            contents[name] = data
        api = rules.parse_api(manifest["api"])
        if not self.db.known(api):
            raise rules.Invalid(
                "App API %s is not one this store knows (it knows up to %s)." % (manifest["api"], self.db.doc["current"])
            )
        rules.check_imports(contents[manifest["entry"]], api, self.db)

        target = os.path.join(self.apps_dir, manifest["id"], manifest["version"])
        if os.path.exists(target):
            raise rules.Invalid(
                "%s %s is already published. Versions never change; publish %s with a higher version."
                % (manifest["id"], manifest["version"], manifest["name"])
            )
        os.makedirs(os.path.dirname(target), exist_ok=True)
        staging = tempfile.mkdtemp(prefix=".incoming-", dir=self.apps_dir)
        try:
            for name, data in contents.items():
                with open(os.path.join(staging, name), "wb") as f:
                    f.write(data)
            with open(os.path.join(staging, "manifest.json"), "w", encoding="utf-8") as f:
                json.dump(manifest, f, indent=2)
                f.write("\n")
            os.rename(staging, target)
        except BaseException:
            shutil.rmtree(staging, ignore_errors=True)
            raise
        self.reload()
        log.info("published %s %s (API %s)", manifest["id"], manifest["version"], manifest["api"])
        return manifest

    def withdraw(self, app_id, version):
        path = self.path_of(app_id, version, "manifest.json")
        if path is None:
            return False
        shutil.rmtree(os.path.dirname(path))
        self.reload()
        log.info("withdrew %s %s", app_id, version)
        return True


def _flag(query, key, default):
    v = query.get(key, [None])[0]
    return default if v is None else v not in ("0", "false", "no")


class Handler(BaseHTTPRequestHandler):
    store = None  # set by make_server
    token = None
    server_version = "CrossPlayStore/1"

    def log_message(self, fmt, *args):
        log.info("%s %s", self.address_string(), fmt % args)

    # --- Responses

    def _send(self, status, body, content_type, extra=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, status, doc, extra=None):
        self._send(status, (json.dumps(doc, indent=1) + "\n").encode("utf-8"), "application/json", extra)

    def _error(self, status, message):
        self._json(status, {"error": message})

    def _authorised(self):
        if not self.token:
            self._error(HTTPStatus.FORBIDDEN, "Publishing is off: the server has no APPSTORE_TOKEN.")
            return False
        given = self.headers.get("Authorization", "")
        if not hmac.compare_digest(given.encode(), ("Bearer " + self.token).encode()):
            self._error(HTTPStatus.UNAUTHORIZED, "Wrong or missing token.")
            return False
        return True

    # --- Routes

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urlsplit(self.path)
        parts = [p for p in url.path.split("/") if p]
        query = parse_qs(url.query)
        if parts == []:
            return self._send(HTTPStatus.OK, render_index(self.store).encode("utf-8"), "text/html; charset=utf-8")
        if parts == ["healthz"]:
            return self._send(HTTPStatus.OK, b"ok\n", "text/plain")
        if parts == ["v1", "api"]:
            return self._json(HTTPStatus.OK, self.store.db.doc)
        if parts == ["v1", "apps"]:
            provides = rules.parse_api(query.get("api", [self.store.db.doc["current"]])[0])
            if provides is None:
                return self._error(HTTPStatus.BAD_REQUEST, "api must be major.minor")
            compatible_from = rules.parse_api(query.get("from", [rules.api_str(provides)])[0])
            if compatible_from is None or provides < compatible_from:
                return self._error(HTTPStatus.BAD_REQUEST, "from must be major.minor, at most api")
            doc = self.store.catalog(
                provides,
                compatible_from,
                _flag(query, "touch", True),
                _flag(query, "buttons", True),
                _flag(query, "compatible", False),
            )
            return self._json(HTTPStatus.OK, doc, {"Cache-Control": "no-cache"})
        if len(parts) == 3 and parts[:2] == ["v1", "apps"]:
            doc = self.store.versions_of(parts[2])
            return self._json(HTTPStatus.OK, doc) if doc else self._error(HTTPStatus.NOT_FOUND, "No such app.")
        if len(parts) in (5, 6) and parts[:2] == ["v1", "apps"]:
            app_id, version = parts[2], parts[3]
            if len(parts) == 5 and parts[4] == "manifest.json":
                path = self.store.path_of(app_id, version, "manifest.json")
            elif len(parts) == 6 and parts[4] == "files":
                path = self.store.path_of(app_id, version, parts[5])
            else:
                path = None
            if path is None or not os.path.isfile(path):
                return self._error(HTTPStatus.NOT_FOUND, "No such file.")
            with open(path, "rb") as f:
                body = f.read()
            kind = "application/json" if path.endswith(".json") else (
                "application/wasm" if path.endswith(".wasm") else "application/octet-stream")
            # Published versions never change.
            return self._send(HTTPStatus.OK, body, kind, {"Cache-Control": "public, max-age=31536000, immutable"})
        self._error(HTTPStatus.NOT_FOUND, "Not found.")

    def do_POST(self):
        if urlsplit(self.path).path.rstrip("/") != "/v1/apps":
            return self._error(HTTPStatus.NOT_FOUND, "Not found.")
        if not self._authorised():
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_UPLOAD_BYTES:
            return self._error(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "Send one .cpapp of at most 4 MB.")
        body = self.rfile.read(length)
        try:
            manifest = self.store.publish(body)
        except rules.Invalid as e:
            return self._error(HTTPStatus.UNPROCESSABLE_ENTITY, str(e))
        self._json(HTTPStatus.CREATED, {"published": manifest["id"], "version": manifest["version"],
                                        "api": manifest["api"]})

    def do_DELETE(self):
        parts = [p for p in urlsplit(self.path).path.split("/") if p]
        if len(parts) != 4 or parts[:2] != ["v1", "apps"]:
            return self._error(HTTPStatus.NOT_FOUND, "Not found.")
        if not self._authorised():
            return
        if not self.store.withdraw(parts[2], parts[3]):
            return self._error(HTTPStatus.NOT_FOUND, "No such version.")
        self._json(HTTPStatus.OK, {"withdrawn": parts[2], "version": parts[3]})


def render_index(store):
    rows = []
    for app_id, versions in sorted(store.snapshot().items(), key=lambda kv: kv[1][0]["name"].lower()):
        latest = versions[0]
        rows.append(
            "<tr><td><b>%s</b><br><small>%s</small></td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
            % (
                html.escape(latest["name"]),
                html.escape(app_id),
                html.escape(latest["summary"]),
                html.escape(latest["version"]),
                html.escape(", ".join("%s (API %s)" % (m["version"], m["api"]) for m in versions)),
                html.escape(", ".join(latest["requires"]) or "-"),
            )
        )
    history = []
    for v in reversed(store.db.versions):
        history.append(
            "<li><b>API %s</b> (%s)%s: %s</li>"
            % (
                html.escape(v["version"]),
                html.escape(v.get("date", "")),
                " <b>breaking</b>, runs apps from %s" % html.escape(v["compatibleFrom"]) if v.get("breaking") else "",
                html.escape(v.get("notes", "")),
            )
        )
    return """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>CrossPlay app store</title>
<style>body{font:15px/1.4 system-ui,sans-serif;max-width:60rem;margin:2rem auto;padding:0 1rem}
table{border-collapse:collapse;width:100%%}td,th{border-bottom:1px solid #ccc;padding:.4rem;text-align:left;
vertical-align:top}small{color:#666}code{background:#eee;padding:0 .2rem}</style></head><body>
<h1>CrossPlay app store</h1>
<p>Current app API <b>%s</b>; firmware with this API runs apps built for %s and up.
On the device: Apps, APP STORE, GET APPS. The catalog for one device is
<code>/v1/apps?api=1.0&amp;from=1.0&amp;touch=1&amp;buttons=1</code>.</p>
<table><tr><th>App</th><th>About</th><th>Latest</th><th>Versions</th><th>Needs</th></tr>%s</table>
<h2>App API history</h2><ul>%s</ul></body></html>
""" % (
        html.escape(store.db.doc["current"]),
        html.escape(store.db.doc["compatibleFrom"]),
        "".join(rows) or "<tr><td colspan=5>Nothing published yet.</td></tr>",
        "".join(history),
    )


def make_server(host, port, store, token):
    handler = type("BoundHandler", (Handler,), {"store": store, "token": token})
    return ThreadingHTTPServer((host, port), handler)


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    default_api = os.path.normpath(os.path.join(here, "..", "..", "..", "docs", "app-api", "compat.json"))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--data", default=os.environ.get("APPSTORE_DATA", "./data"))
    parser.add_argument("--api", default=os.environ.get("APPSTORE_API", default_api),
                        help="docs/app-api/compat.json")
    parser.add_argument("--host", default=os.environ.get("APPSTORE_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("APPSTORE_PORT", "8080")))
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    token = os.environ.get("APPSTORE_TOKEN") or None
    if not token:
        log.warning("APPSTORE_TOKEN is not set: publishing over HTTP is off (cpapp.py publish --data still works)")
    store = Store(args.data, rules.ApiDatabase.load(args.api))
    server = make_server(args.host, args.port, store, token)
    log.info("listening on %s:%d", args.host, args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
