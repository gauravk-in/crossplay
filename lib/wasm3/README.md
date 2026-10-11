# wasm3 (vendored)

[Wasm3](https://github.com/wasm3/wasm3) v0.9.0 (commit 0cd3832), MIT licensed,
copied from `source/` with the WASI, uvwasi, libc and tracer bindings left out:
downloadable apps get only the firmware's own `crossplay` imports and the
handful of WASI shims `src/apps_local/appstore/WasmHost.cpp` defines.

One local change, marked `FORK CHANGE` in `m3_core.c`: with
`d_m3HostAllocator` the firmware supplies the allocator, so module code and
linear memory go to PSRAM.

To update, copy the same files from a newer tag and re-apply that block.
