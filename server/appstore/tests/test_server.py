"""Tests for the store: catalog selection, publishing rules and the HTTP layer.

    cd server/appstore && python3 -m unittest discover -s tests -t .
"""

import io
import json
import os
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zipfile

from crossplay_store import rules, server

HERE = os.path.dirname(os.path.abspath(__file__))
COMPAT = os.path.join(HERE, "..", "..", "..", "docs", "app-api", "compat.json")


def _leb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _name(s):
    return _leb(len(s)) + s.encode()


def _section(sid, body):
    return bytes([sid]) + _leb(len(body)) + body


def tiny_wasm(imports=(("crossplay", "api_version"),), exports=("cp_app_api_version", "cp_app_init", "cp_app_render",
                                                                "cp_app_event")):
    """A module whose imports and exports are all () -> i32."""
    types = _section(1, _leb(1) + b"\x60\x00\x01\x7f")
    imp = _leb(len(imports)) + b"".join(_name(m) + _name(f) + b"\x00\x00" for m, f in imports)
    funcs = _section(3, _leb(len(exports)) + b"\x00" * len(exports))
    exp = _leb(len(exports)) + b"".join(_name(e) + b"\x00" + _leb(len(imports) + i) for i, e in enumerate(exports))
    body = b"\x00\x41\x00\x0b"
    code = _section(10, _leb(len(exports)) + (_leb(len(body)) + body) * len(exports))
    return b"\x00asm\x01\x00\x00\x00" + types + _section(2, imp) + funcs + _section(7, exp) + code


def package(app_id="org.test.app", version="1.0.0", api="1.0", requires=(), wasm=None, extra=None, manifest=None):
    wasm = tiny_wasm() if wasm is None else wasm
    m = {"id": app_id, "name": "Test App", "version": version, "api": api, "requires": list(requires),
         "files": [rules.file_entry("app.wasm", wasm)]}
    m.update(manifest or {})
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("manifest.json", json.dumps(m))
        z.writestr("app.wasm", wasm)
        for name, data in (extra or {}).items():
            z.writestr(name, data)
    return buf.getvalue()


def future_db():
    """1.1 adds a function; 2.0 removes one and still runs 1.1 apps."""
    with open(COMPAT, encoding="utf-8") as f:
        doc = json.load(f)
    doc["current"] = "2.0"
    doc["compatibleFrom"] = "1.1"
    doc["versions"] += [
        {"version": "1.1", "compatibleFrom": "1.0", "notes": "Adds vibrate.", "added": ["vibrate"]},
        {"version": "2.0", "compatibleFrom": "1.1", "breaking": True, "notes": "fill takes a radius. More.",
         "removed": ["now_ms"]},
    ]
    doc["functions"]["vibrate"] = {"signature": "i()", "since": "1.1"}
    doc["functions"]["now_ms"]["until"] = "2.0"
    return rules.ApiDatabase(doc)


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = server.Store(self.tmp.name, rules.ApiDatabase.load(COMPAT))

    def tearDown(self):
        self.tmp.cleanup()

    def test_publish_then_catalog(self):
        self.store.publish(package())
        cat = self.store.catalog((1, 0), (1, 0), True, True, False)
        self.assertEqual(len(cat["apps"]), 1)
        app = cat["apps"][0]
        self.assertTrue(app["compatible"])
        self.assertEqual(app["version"], "1.0.0")
        self.assertEqual(app["manifest"], "v1/apps/org.test.app/1.0.0/manifest.json")
        self.assertEqual(app["files"][0]["url"], "v1/apps/org.test.app/1.0.0/files/app.wasm")

    def test_versions_are_immutable(self):
        self.store.publish(package())
        with self.assertRaisesRegex(rules.Invalid, "already published"):
            self.store.publish(package())

    def test_touch_requirement_filters(self):
        self.store.publish(package(requires=["touch"]))
        app = self.store.catalog((1, 0), (1, 0), False, True, False)["apps"][0]
        self.assertFalse(app["compatible"])
        self.assertEqual(app["reason"], "Needs a touch screen.")
        self.assertEqual(self.store.catalog((1, 0), (1, 0), False, True, True)["apps"], [])

    def test_unknown_api_refused(self):
        with self.assertRaisesRegex(rules.Invalid, "not one this store knows"):
            self.store.publish(package(api="1.7"))

    def test_unknown_import_refused(self):
        wasm = tiny_wasm(imports=(("crossplay", "launch_missiles"),))
        with self.assertRaisesRegex(rules.Invalid, "no app API has"):
            self.store.publish(package(wasm=wasm))

    def test_foreign_module_refused(self):
        wasm = tiny_wasm(imports=(("env", "api_version"),))
        with self.assertRaisesRegex(rules.Invalid, "devices do not provide"):
            self.store.publish(package(wasm=wasm))

    def test_wrong_signature_refused(self):
        wasm = tiny_wasm(imports=(("crossplay", "now_ms"),))  # I() in the API, i() here
        with self.assertRaisesRegex(rules.Invalid, "as i\\(\\)"):
            self.store.publish(package(wasm=wasm))

    def test_missing_export_refused(self):
        wasm = tiny_wasm(exports=("cp_app_api_version", "cp_app_init"))
        with self.assertRaisesRegex(rules.Invalid, "does not export cp_app_render"):
            self.store.publish(package(wasm=wasm))

    def test_hash_mismatch_refused(self):
        bad = package(manifest={"files": [{"name": "app.wasm", "size": 4, "sha256": "0" * 64}]})
        with self.assertRaisesRegex(rules.Invalid, "does not match"):
            self.store.publish(bad)

    def test_unlisted_file_refused(self):
        with self.assertRaisesRegex(rules.Invalid, "does not list"):
            self.store.publish(package(extra={"../../evil": b"x"}))

    def test_bad_ids_and_names(self):
        for bad in ("..", "a/b", "Org.app", "ab", "../etc"):
            self.assertFalse(rules.valid_app_id(bad), bad)
        for bad in ("../x", "a/b", ".hidden", "", "x" * 40):
            self.assertFalse(rules.valid_file_name(bad), bad)

    def test_path_of_only_serves_listed_files(self):
        self.store.publish(package())
        self.assertIsNotNone(self.store.path_of("org.test.app", "1.0.0", "app.wasm"))
        self.assertIsNone(self.store.path_of("org.test.app", "1.0.0", "../../etc/passwd"))
        self.assertIsNone(self.store.path_of("org.test.app", "..", "app.wasm"))
        self.assertIsNone(self.store.path_of("..", "1.0.0", "app.wasm"))

    def test_withdraw(self):
        self.store.publish(package())
        self.assertTrue(self.store.withdraw("org.test.app", "1.0.0"))
        self.assertFalse(self.store.withdraw("org.test.app", "1.0.0"))
        self.assertEqual(self.store.catalog((1, 0), (1, 0), True, True, False)["apps"], [])


class CompatibilityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = future_db()
        self.store = server.Store(self.tmp.name, self.db)
        self.store.publish(package(version="1.0.0", api="1.0"))
        vib = tiny_wasm(imports=(("crossplay", "vibrate"),))
        self.store.publish(package(version="1.1.0", api="1.1", wasm=vib))

    def tearDown(self):
        self.tmp.cleanup()

    def test_old_firmware_gets_the_newest_version_it_runs(self):
        app = self.store.catalog((1, 0), (1, 0), True, True, False)["apps"][0]
        self.assertEqual(app["version"], "1.0.0")
        self.assertEqual(app["newer"], {"version": "1.1.0", "api": "1.1"})

    def test_new_firmware_gets_the_latest(self):
        app = self.store.catalog((2, 0), (1, 1), True, True, False)["apps"][0]
        self.assertEqual(app["version"], "1.1.0")
        self.assertNotIn("newer", app)

    def test_import_from_a_later_api_refused(self):
        vib = tiny_wasm(imports=(("crossplay", "vibrate"),))
        with self.assertRaisesRegex(rules.Invalid, "in app API 1.1, but says it was built for 1.0"):
            self.store.publish(package(version="1.2.0", api="1.0", wasm=vib))

    def test_why_not_explains_both_directions(self):
        self.assertIn("Update the firmware", self.db.why_not((2, 0), (1, 0), (1, 0)))
        reason = self.db.why_not((1, 0), (2, 0), (1, 1))
        self.assertIn("no longer runs", reason)
        self.assertIn("API 2.0 changed: fill takes a radius", reason)
        self.assertIsNone(self.db.why_not((1, 1), (2, 0), (1, 1)))

    def test_removed_function_not_in_later_api(self):
        self.assertIn("now_ms", self.db.functions_in((1, 1)))
        self.assertNotIn("now_ms", self.db.functions_in((2, 0)))


class HttpTest(unittest.TestCase):
    TOKEN = "s3cret"

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.store = server.Store(cls.tmp.name, rules.ApiDatabase.load(COMPAT))
        cls.httpd = server.make_server("127.0.0.1", 0, cls.store, cls.TOKEN)
        cls.base = "http://127.0.0.1:%d" % cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.tmp.cleanup()

    def call(self, method, path, body=None, token=None):
        req = urllib.request.Request(self.base + path, data=body, method=method)
        if token:
            req.add_header("Authorization", "Bearer " + token)
        try:
            with urllib.request.urlopen(req) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def test_publish_needs_the_token(self):
        pkg = package(app_id="org.test.http")
        self.assertEqual(self.call("POST", "/v1/apps", pkg)[0], 401)
        self.assertEqual(self.call("POST", "/v1/apps", pkg, "wrong")[0], 401)
        self.assertEqual(self.call("POST", "/v1/apps", pkg, self.TOKEN)[0], 201)
        status, body = self.call("GET", "/v1/apps?api=1.0&from=1.0&touch=1&buttons=1")
        self.assertEqual(status, 200)
        ids = [a["id"] for a in json.loads(body)["apps"]]
        self.assertIn("org.test.http", ids)
        status, body = self.call("GET", "/v1/apps/org.test.http/1.0.0/files/app.wasm")
        self.assertEqual((status, body), (200, tiny_wasm()))
        self.assertEqual(self.call("DELETE", "/v1/apps/org.test.http/1.0.0")[0], 401)
        self.assertEqual(self.call("DELETE", "/v1/apps/org.test.http/1.0.0", token=self.TOKEN)[0], 200)

    def test_traversal_is_not_found(self):
        for path in ("/v1/apps/org.test.http/1.0.0/files/..%2f..%2fmanifest.json",
                     "/v1/apps/../../etc/passwd", "/v1/apps/org.test.http/1.0.0/files/../../../x"):
            self.assertEqual(self.call("GET", path)[0], 404, path)

    def test_health_and_index(self):
        self.assertEqual(self.call("GET", "/healthz")[0], 200)
        status, body = self.call("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"App API", body)


if __name__ == "__main__":
    unittest.main()
