#!/usr/bin/env python3
"""Build, package, check and publish CrossPlay apps. Standard library only.

    appsdk/tools/cpapp.py build appsdk/apps/memory -o out/       # compile + pack
    appsdk/tools/cpapp.py pack appsdk/apps/memory app.wasm -o memory.cpapp
    appsdk/tools/cpapp.py check memory.cpapp
    appsdk/tools/cpapp.py publish memory.cpapp --server https://apps.example --token $APPSTORE_TOKEN
    appsdk/tools/cpapp.py publish memory.cpapp --data server/appstore/data   # straight into a data folder

An app folder holds app.json: the manifest fields (id, name, version, summary,
author, category, requires, memoryKB, dataQuotaKB, optionally api) plus
build.sources, the shared .cpp files it compiles with. The API version
defaults to the one in appsdk/include/crossplay_app.h, which is the one
the app's own cp_app_api_version returns.

A .cpapp is a zip of manifest.json and the files it lists. `pack` and `check`
apply the device's manifest rules and check every import of the program
against docs/app-api/compat.json for the API it claims, so an app that would
not run is refused here rather than on somebody's device.
"""

import argparse
import io
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "server", "appstore"))

from crossplay_store import rules  # noqa: E402

HEADER = os.path.join(ROOT, "appsdk", "include", "crossplay_app.h")
COMPAT = os.path.join(ROOT, "docs", "app-api", "compat.json")
MANIFEST_KEYS = ("id", "name", "version", "api", "summary", "author", "category", "entry", "requires", "memoryKB",
                 "dataQuotaKB")


def sdk_api():
    text = open(HEADER, encoding="utf-8").read()
    major = re.search(r"#define CP_API_VERSION_MAJOR (\d+)", text).group(1)
    minor = re.search(r"#define CP_API_VERSION_MINOR (\d+)", text).group(1)
    return "%s.%s" % (major, minor)


def load_app(app_dir):
    with open(os.path.join(app_dir, "app.json"), encoding="utf-8") as f:
        return json.load(f)


def pack(app_dir, wasm_path, out_path):
    app = load_app(app_dir)
    with open(wasm_path, "rb") as f:
        wasm = f.read()
    manifest = {k: app[k] for k in MANIFEST_KEYS if k in app}
    manifest.setdefault("api", sdk_api())
    manifest.setdefault("entry", "app.wasm")
    manifest["files"] = [rules.file_entry(manifest["entry"], wasm)]
    manifest = rules.check_manifest(manifest)
    db = rules.ApiDatabase.load(COMPAT)
    info = rules.check_imports(wasm, rules.parse_api(manifest["api"]), db)
    if info["memory"] and info["memory"][0] * 64 > manifest["memoryKB"]:
        raise rules.Invalid("It starts with %d KB of memory but memoryKB is %d." % (info["memory"][0] * 64,
                                                                                    manifest["memoryKB"]))
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        z.writestr(manifest["entry"], wasm)
    if os.path.isdir(out_path):
        out_path = os.path.join(out_path, "%s-%s.cpapp" % (manifest["id"], manifest["version"]))
    with open(out_path, "wb") as f:
        f.write(buf.getvalue())
    print("%s: %s %s, API %s, %d bytes" % (out_path, manifest["id"], manifest["version"], manifest["api"], len(wasm)))
    return out_path


def check(package_path):
    """Everything the server checks on publish, without a server."""
    from crossplay_store.server import Store
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        store = Store(tmp, rules.ApiDatabase.load(COMPAT))
        with open(package_path, "rb") as f:
            manifest = store.publish(f.read())
    print("%s: ok (%s %s, API %s)" % (package_path, manifest["id"], manifest["version"], manifest["api"]))


def build(app_dir, out_dir):
    app = load_app(app_dir)
    os.makedirs(out_dir, exist_ok=True)
    wasm = os.path.join(out_dir, os.path.basename(os.path.normpath(app_dir)) + ".wasm")
    sources = [os.path.join(ROOT, s) for s in app.get("build", {}).get("sources", [])]
    subprocess.check_call([os.path.join(ROOT, "appsdk", "tools", "build-app.sh"), app_dir, wasm] + sources)
    return pack(app_dir, wasm, out_dir)


def publish(package_path, server, token, data_dir):
    with open(package_path, "rb") as f:
        body = f.read()
    if data_dir:
        from crossplay_store.server import Store

        manifest = Store(data_dir, rules.ApiDatabase.load(COMPAT)).publish(body)
        print("published %s %s into %s" % (manifest["id"], manifest["version"], data_dir))
        return
    request = urllib.request.Request(
        server.rstrip("/") + "/v1/apps",
        data=body,
        method="POST",
        headers={"Authorization": "Bearer " + token, "Content-Type": "application/zip"},
    )
    try:
        with urllib.request.urlopen(request) as response:
            print(response.read().decode("utf-8").strip())
    except urllib.error.HTTPError as e:
        raise SystemExit("publish refused (%d): %s" % (e.code, e.read().decode("utf-8", "replace").strip()))


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("app_dir")
    b.add_argument("-o", "--out", default="out")
    k = sub.add_parser("pack")
    k.add_argument("app_dir")
    k.add_argument("wasm")
    k.add_argument("-o", "--out", default=".")
    c = sub.add_parser("check")
    c.add_argument("package")
    u = sub.add_parser("publish")
    u.add_argument("package")
    u.add_argument("--server", default=os.environ.get("APPSTORE_URL"))
    u.add_argument("--token", default=os.environ.get("APPSTORE_TOKEN"))
    u.add_argument("--data", help="publish into a server data folder instead of over HTTP")
    args = p.parse_args(argv)
    try:
        if args.cmd == "build":
            build(args.app_dir, args.out)
        elif args.cmd == "pack":
            pack(args.app_dir, args.wasm, args.out)
        elif args.cmd == "check":
            check(args.package)
        elif args.cmd == "publish":
            if not args.data and not (args.server and args.token):
                raise SystemExit("publish needs --server and --token (or APPSTORE_URL/APPSTORE_TOKEN), or --data")
            publish(args.package, args.server, args.token, args.data)
    except rules.Invalid as e:
        raise SystemExit("refused: %s" % e)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
