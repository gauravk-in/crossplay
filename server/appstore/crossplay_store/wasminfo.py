"""Reads what a WebAssembly module imports and exports. Standard library only.

    python3 -m crossplay_store.wasminfo app.wasm   (from server/appstore)

cpapp.py uses it to check an app against the API it claims, before the device
ever sees it.
"""

import sys

VALTYPES = {0x7F: "i", 0x7E: "I", 0x7D: "f", 0x7C: "F"}


class Reader:
    def __init__(self, data, pos=0, end=None):
        self.data = data
        self.pos = pos
        self.end = len(data) if end is None else end

    def byte(self):
        if self.pos >= self.end:
            raise ValueError("truncated module")
        b = self.data[self.pos]
        self.pos += 1
        return b

    def uleb(self):
        result = 0
        shift = 0
        while True:
            b = self.byte()
            result |= (b & 0x7F) << shift
            if not b & 0x80:
                return result
            shift += 7
            if shift > 35:
                raise ValueError("bad LEB128")

    def name(self):
        n = self.uleb()
        if self.pos + n > self.end:
            raise ValueError("truncated name")
        s = self.data[self.pos:self.pos + n].decode("utf-8")
        self.pos += n
        return s


def signature(params, results):
    """wasm3's signature spelling: "i(iI*)" style, with i32 written as i."""
    ret = "".join(VALTYPES.get(t, "?") for t in results) or "v"
    return "%s(%s)" % (ret, "".join(VALTYPES.get(t, "?") for t in params))


def read(data):
    """Returns {"imports": [(module, name, kind, sig)], "exports": [(name, kind)],
    "memory": (min_pages, max_pages or None)}."""
    if data[:4] != b"\0asm" or data[4:8] != b"\1\0\0\0":
        raise ValueError("not a WebAssembly 1.0 module")
    r = Reader(data, 8)
    types = []
    imports = []
    exports = []
    memory = None
    while r.pos < r.end:
        sid = r.byte()
        size = r.uleb()
        body = Reader(data, r.pos, r.pos + size)
        r.pos += size
        if sid == 1:
            for _ in range(body.uleb()):
                if body.byte() != 0x60:
                    raise ValueError("bad type")
                params = [body.byte() for _ in range(body.uleb())]
                results = [body.byte() for _ in range(body.uleb())]
                types.append((params, results))
        elif sid == 2:
            for _ in range(body.uleb()):
                module = body.name()
                field = body.name()
                kind = body.byte()
                if kind == 0:
                    t = types[body.uleb()]
                    imports.append((module, field, "func", signature(*t)))
                elif kind == 1:
                    body.byte()
                    flags = body.uleb()
                    body.uleb()
                    if flags & 1:
                        body.uleb()
                    imports.append((module, field, "table", ""))
                elif kind == 2:
                    flags = body.uleb()
                    lo = body.uleb()
                    hi = body.uleb() if flags & 1 else None
                    memory = (lo, hi)
                    imports.append((module, field, "memory", ""))
                elif kind == 3:
                    body.byte()
                    body.byte()
                    imports.append((module, field, "global", ""))
                else:
                    raise ValueError("bad import kind")
        elif sid == 5:
            for _ in range(body.uleb()):
                flags = body.uleb()
                lo = body.uleb()
                hi = body.uleb() if flags & 1 else None
                memory = (lo, hi)
        elif sid == 7:
            kinds = {0: "func", 1: "table", 2: "memory", 3: "global"}
            for _ in range(body.uleb()):
                name = body.name()
                kind = kinds.get(body.byte(), "?")
                body.uleb()
                exports.append((name, kind))
    return {"imports": imports, "exports": exports, "memory": memory}


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    with open(argv[1], "rb") as f:
        info = read(f.read())
    for module, field, kind, sig in info["imports"]:
        print("import %s.%s %s %s" % (module, field, kind, sig))
    for name, kind in info["exports"]:
        print("export %s %s" % (name, kind))
    if info["memory"]:
        lo, hi = info["memory"]
        print("memory %d..%s pages" % (lo, hi if hi is not None else "unbounded"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
