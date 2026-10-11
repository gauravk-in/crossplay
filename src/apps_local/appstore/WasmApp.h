#pragma once

// One running downloadable app: a WebAssembly module, the wasm3 interpreter
// that runs it, and the "crossplay" imports that are the whole of what it can
// reach (appsdk/include/crossplay_app.h).
//
// What stands between an app and the device:
//   - Every import checks the pointers and lengths it is handed against the
//     app's own linear memory; a bad one traps the app, never the firmware.
//   - Linear memory is capped at the manifest's memoryKB.
//   - Each call has a time budget, enforced from m3_Yield; an app that spins
//     is stopped with a trap.
//   - Files are bare names inside the app's own data folder, under the
//     manifest's dataQuotaKB.
//   - On the device, app code runs on its own task with a bounded native stack
//     (d_m3MaxNativeStack in lib/wasm3/library.json), so deep recursion traps
//     instead of overflowing the loop or render task.
//
// A trap ends the app: failed() goes true, error() says why, and every later
// call is a no-op. The activity shows the error and leaves.

#include <cstdint>
#include <memory>
#include <string>

#include "AppStoreCore.h"

class GfxRenderer;

namespace appstore {

class WasmApp {
 public:
  struct Device {
    bool hasTouch = true;
    bool hasButtons = true;
  };

  WasmApp(GfxRenderer& renderer, Device device);
  ~WasmApp();
  WasmApp(const WasmApp&) = delete;
  WasmApp& operator=(const WasmApp&) = delete;

  // Loads `<packageDir>/<manifest.entry>`, checks it against the manifest,
  // links the API, and runs the app's own initialisation. False with error()
  // set when the app cannot run; the reasons are written for the person
  // holding the device.
  bool start(const std::string& packageDir, const Manifest& manifest, const std::string& dataDir);

  // Draws the whole screen into the renderer's buffer. The caller clears the
  // buffer first and pushes it to the panel after.
  bool render();
  // 1 when the app handled it, 0 when it did not, -1 when it trapped.
  int32_t event(int32_t type, int32_t a = 0, int32_t b = 0, int32_t c = 0);
  // About to close or sleep. Safe to call after a trap.
  void suspend();

  bool failed() const { return failed_; }
  const std::string& error() const { return error_; }

  // What the app asked for since the last take. A render request carries the
  // strongest refresh asked for (CP_REFRESH_*).
  bool takeRenderRequest(int32_t& refresh);
  // True when cp_set_timer was called; `ms` 0 means cancel.
  bool takeTimer(int32_t& ms);
  bool exitRequested() const { return exitRequested_; }

  // The import table, for host-tests/appstore/check_api.py and the docs. The
  // signature strings are wasm3's ("i(ii)", i = i32, I = i64, v = none).
  struct ImportSpec {
    const char* module;
    const char* name;
    const char* signature;
  };
  static const ImportSpec* imports(int& count);

  // Raw-function plumbing; not for callers.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
  bool failed_ = false;
  std::string error_;
  bool exitRequested_ = false;
  friend struct Impl;
};

}  // namespace appstore
