"""The rules of downloadable apps, in Python.

The device applies the same rules in src/apps_local/appstore/AppStoreCore.cpp;
the server and appsdk/tools/cpapp.py apply them here, before anything reaches a
device. host-tests/appstore/test_core.cpp and server/appstore/tests cover the
same cases on each side: what a valid manifest is, and what runs where.
"""

import hashlib
import json
import re

from . import wasminfo

MAX_MEMORY_KB = 4096
MAX_DATA_QUOTA_KB = 4096
MAX_PACKAGE_BYTES = 4 * 1024 * 1024
MAX_FILES = 8
MAX_NAME_BYTES = 24
REQUIREMENTS = ("touch", "buttons")
CATEGORIES = ("games", "apps")

_API_RE = re.compile(r"^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$")
_APP_VERSION_RE = re.compile(r"^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$")
_APP_ID_RE = re.compile(r"^[a-z][a-z0-9._-]{2,47}$")
_FILE_RE = re.compile(r"^[A-Za-z0-9_-][A-Za-z0-9._-]{0,31}$")
_HEX64_RE = re.compile(r"^[0-9a-f]{64}$")


class Invalid(ValueError):
    """A manifest, package or catalog request that breaks a rule. The message
    is meant for the person who made it."""


# --- Versions -----------------------------------------------------------------


def parse_api(text):
    """"1.0" -> (1, 0); None for anything else."""
    m = _API_RE.match(text or "") if isinstance(text, str) else None
    return (int(m.group(1)), int(m.group(2))) if m else None


def parse_app_version(text):
    m = _APP_VERSION_RE.match(text or "") if isinstance(text, str) else None
    return tuple(int(g) for g in m.groups()) if m else None


def api_str(api):
    return "%d.%d" % api


def api_compatible(app, provides, compatible_from):
    """An app built against `app` runs on a firmware that provides `provides`
    and still runs everything from `compatible_from` up."""
    return compatible_from <= app <= provides


# --- Names --------------------------------------------------------------------


def valid_app_id(app_id):
    return isinstance(app_id, str) and bool(_APP_ID_RE.match(app_id)) and ".." not in app_id


def valid_file_name(name):
    return isinstance(name, str) and bool(_FILE_RE.match(name))


# --- Manifests ----------------------------------------------------------------


def check_manifest(manifest):
    """Raises Invalid when `manifest` (a dict) would be refused by a device.
    Returns it with defaults filled in."""
    if not isinstance(manifest, dict):
        raise Invalid("The manifest is not a JSON object.")
    m = dict(manifest)
    m.setdefault("category", "apps")
    m.setdefault("entry", "app.wasm")
    m.setdefault("requires", [])
    m.setdefault("memoryKB", 1024)
    m.setdefault("dataQuotaKB", 256)
    m.setdefault("summary", "")
    m.setdefault("author", "")
    if not valid_app_id(m.get("id")):
        raise Invalid("The app id is missing or not allowed: 3 to 48 of a-z 0-9 . _ -, starting with a letter.")
    name = m.get("name")
    if not isinstance(name, str) or not name or len(name.encode("utf-8")) > MAX_NAME_BYTES:
        raise Invalid("The app name is missing or longer than %d bytes." % MAX_NAME_BYTES)
    if not parse_app_version(m.get("version")):
        raise Invalid("The app version is not major.minor.patch.")
    if not parse_api(m.get("api")):
        raise Invalid("The API version is not major.minor.")
    if m["category"] not in CATEGORIES:
        raise Invalid("The category is neither games nor apps.")
    if not isinstance(m["requires"], list):
        raise Invalid("requires is not a list.")
    for r in m["requires"]:
        if r not in REQUIREMENTS:
            raise Invalid("It needs something no firmware knows: %r." % (r,))
    for key, lo, hi in (("memoryKB", 64, MAX_MEMORY_KB), ("dataQuotaKB", 0, MAX_DATA_QUOTA_KB)):
        v = m[key]
        if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
            raise Invalid("%s must be a whole number from %d to %d." % (key, lo, hi))
    files = m.get("files")
    if not isinstance(files, list) or not files:
        raise Invalid("The package lists no files.")
    if len(files) > MAX_FILES:
        raise Invalid("The package lists more than %d files." % MAX_FILES)
    seen = set()
    total = 0
    for f in files:
        if not isinstance(f, dict) or not valid_file_name(f.get("name")):
            raise Invalid("The package has a file with a bad name.")
        if f["name"] == "manifest.json":
            raise Invalid("The package lists its own manifest as a file.")
        if f["name"] in seen:
            raise Invalid("The package lists %s twice." % f["name"])
        seen.add(f["name"])
        if not isinstance(f.get("sha256"), str) or not _HEX64_RE.match(f["sha256"]):
            raise Invalid("%s has no SHA-256." % f["name"])
        size = f.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or not 0 <= size <= MAX_PACKAGE_BYTES:
            raise Invalid("%s has a bad size." % f["name"])
        total += size
    if total > MAX_PACKAGE_BYTES:
        raise Invalid("The package is larger than 4 MB.")
    if not valid_file_name(m["entry"]) or m["entry"] not in seen:
        raise Invalid("The package does not contain its entry file.")
    return m


def file_entry(name, data):
    return {"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


# --- The API database (docs/app-api/compat.json) -------------------------------


class ApiDatabase:
    def __init__(self, doc):
        self.doc = doc
        self.current = parse_api(doc["current"])
        self.compatible_from = parse_api(doc["compatibleFrom"])
        self.versions = sorted(
            (dict(v, _api=parse_api(v["version"]), _from=parse_api(v["compatibleFrom"])) for v in doc["versions"]),
            key=lambda v: v["_api"],
        )
        self.functions = doc["functions"]

    @classmethod
    def load(cls, path):
        with open(path, encoding="utf-8") as f:
            return cls(json.load(f))

    def known(self, api):
        return any(v["_api"] == api for v in self.versions)

    def functions_in(self, api):
        """{name: signature} of every function an app built against `api` may import."""
        out = {}
        for name, fn in self.functions.items():
            since = parse_api(fn["since"])
            until = parse_api(fn["until"]) if fn.get("until") else None
            if since <= api and (until is None or api < until):
                out[name] = fn["signature"]
        return out

    def breaks_between(self, older, newer):
        """The breaking versions after `older` up to `newer`, oldest first."""
        return [v for v in self.versions if v.get("breaking") and older < v["_api"] <= newer]

    def why_not(self, app_api, provides, compatible_from):
        """A sentence for the person holding the device, or None when it runs."""
        if api_compatible(app_api, provides, compatible_from):
            return None
        if provides < app_api:
            return "Needs app API %s; this firmware has %s. Update the firmware." % (api_str(app_api), api_str(provides))
        breaks = self.breaks_between(app_api, provides)
        detail = ""
        if breaks:
            b = breaks[0]
            detail = " (API %s changed: %s)" % (b["version"], b.get("notes", "").split(". ")[0].rstrip("."))
        return "Built for app API %s, which this firmware no longer runs%s. Wait for an update to the app." % (
            api_str(app_api),
            detail,
        )


def check_imports(wasm_bytes, api, db):
    """Raises Invalid unless every import of the module is one `api` has, with
    the same signature, and the module exports the app entry points."""
    try:
        info = wasminfo.read(wasm_bytes)
    except ValueError as e:
        raise Invalid("The program file is not a WebAssembly module: %s." % e)
    allowed = db.functions_in(api)
    wasi = {"fd_write": "i(iiii)", "fd_seek": "i(iIii)", "fd_close": "i(i)"}
    for module, field, kind, sig in info["imports"]:
        if kind != "func":
            raise Invalid("It imports a %s (%s.%s); apps may only import functions." % (kind, module, field))
        if module == "crossplay":
            if field not in allowed:
                since = db.functions.get(field, {}).get("since")
                if since:
                    raise Invalid(
                        "It calls crossplay.%s, which is in app API %s, but says it was built for %s."
                        % (field, since, api_str(api))
                    )
                raise Invalid("It calls crossplay.%s, which no app API has." % field)
            if allowed[field] != sig:
                raise Invalid(
                    "It calls crossplay.%s as %s; app API %s has %s." % (field, sig, api_str(api), allowed[field])
                )
        elif module == "wasi_snapshot_preview1":
            if wasi.get(field) != sig:
                raise Invalid("It needs the WASI function %s, which devices do not provide." % field)
        else:
            raise Invalid("It imports from %r, which devices do not provide." % module)
    exports = {name for name, kind in info["exports"] if kind == "func"}
    for name in ("cp_app_api_version", "cp_app_init", "cp_app_render", "cp_app_event"):
        if name not in exports:
            raise Invalid("It does not export %s. Build it with the app SDK." % name)
    return info
