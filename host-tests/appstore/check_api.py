"""The app API is written down in three places, and they must agree:

  appsdk/include/crossplay_app.h          what apps compile against
  src/apps_local/appstore/WasmApp.cpp     what the firmware links (kImports)
  docs/app-api/compat.json                the record the store and cpapp.py use

This fails when the version numbers differ, when a function is in one and not
the others, or when a signature differs. A change to the API that skipped one
of them is exactly the change that breaks somebody's installed app.
"""

import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "server", "appstore"))

from crossplay_store import rules  # noqa: E402

problems = []


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as f:
        return f.read()


header = read("appsdk", "include", "crossplay_app.h")
firmware = read("src", "apps_local", "appstore", "WasmApp.cpp")
compat = json.loads(read("docs", "app-api", "compat.json"))
db = rules.ApiDatabase(compat)


def define(name):
    return int(re.search(r"#define %s (\d+)" % name, header).group(1))


provides = (define("CP_API_VERSION_MAJOR"), define("CP_API_VERSION_MINOR"))
compatible_from = (define("CP_API_COMPATIBLE_FROM_MAJOR"), define("CP_API_COMPATIBLE_FROM_MINOR"))
if rules.parse_api(compat["current"]) != provides:
    problems.append("compat.json current is %s, the header says %s" % (compat["current"], rules.api_str(provides)))
if rules.parse_api(compat["compatibleFrom"]) != compatible_from:
    problems.append("compat.json compatibleFrom is %s, the header says %s"
                    % (compat["compatibleFrom"], rules.api_str(compatible_from)))
if not db.known(provides):
    problems.append("compat.json has no entry for version %s" % rules.api_str(provides))
latest = db.versions[-1]
if latest["_api"] != provides:
    problems.append("the newest compat.json version is %s, not %s" % (latest["version"], rules.api_str(provides)))
if latest["_from"] != compatible_from:
    problems.append("compat.json's %s entry says compatibleFrom %s" % (latest["version"], latest["compatibleFrom"]))
for older, newer in zip(db.versions, db.versions[1:]):
    if newer["_from"] != older["_from"] and not newer.get("breaking"):
        problems.append("%s raises compatibleFrom but is not marked breaking" % newer["version"])
    if newer["_from"] < older["_from"]:
        problems.append("%s lowers compatibleFrom" % newer["version"])

expected = db.functions_in(provides)
linked = dict(re.findall(r'\{\{kCrossplay, "([a-z_0-9]+)", "([^"]+)"\}', firmware))
declared = set(re.findall(r"^CP_IMPORT\(([a-z_0-9]+)\)", header, re.M))
for name, sig in sorted(expected.items()):
    if name not in linked:
        problems.append("compat.json has %s in %s, but the firmware does not link it" % (name, compat["current"]))
    elif linked[name] != sig:
        problems.append("%s is %s in compat.json and %s in the firmware" % (name, sig, linked[name]))
    if name not in declared:
        problems.append("compat.json has %s, but the SDK header does not declare it" % name)
for name in sorted(set(linked) - set(expected)):
    problems.append("the firmware links %s, which compat.json does not have in %s" % (name, compat["current"]))
for name in sorted(declared - set(expected)):
    problems.append("the SDK header declares %s, which compat.json does not have in %s" % (name, compat["current"]))
for name, fn in compat["functions"].items():
    since = rules.parse_api(fn["since"])
    if not db.known(since):
        problems.append("%s is since %s, which is not a version in compat.json" % (name, fn["since"]))
# A function an older, still-supported API had must still be linked, with the
# same signature: an app built against it is promised that it runs.
for v in db.versions:
    if v["_api"] < compatible_from:
        continue
    for name, sig in db.functions_in(v["_api"]).items():
        if linked.get(name) not in (None, sig) or (name not in linked):
            problems.append("API %s apps import %s %s, which this firmware does not link as that" % (
                v["version"], name, sig))

if problems:
    for p in problems:
        print("FAIL: " + p)
    sys.exit(1)
print("app API %s: header, firmware and compat.json agree (%d functions)" % (compat["current"], len(expected)))
