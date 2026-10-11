#include "WasmApp.h"

#include <Arduino.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <m3_fork.h>
#include <wasm3.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "../../../appsdk/include/crossplay_app.h"
#include "../ui/ToyboxFonts.h"

namespace fui = freeink::ui;

// --- What wasm3 asks of the firmware ------------------------------------------
//
// lib/wasm3 is built with d_m3HostAllocator: its code pages, stack and linear
// memory come from here, so they land in PSRAM rather than in the internal RAM
// the reader and Wi-Fi live on.

extern "C" void* m3_HostCalloc(const size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr) p = malloc(size);
  if (p != nullptr) std::memset(p, 0, size);
  return p;
}

extern "C" void m3_HostFree(void* p) { heap_caps_free(p); }

extern "C" void* m3_HostRealloc(void* p, const size_t size) {
  void* q = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (q == nullptr) q = realloc(p, size);
  return q;
}

namespace {

// The watchdog. wasm3 calls m3_Yield on every call and, with the FORK CHANGE in
// m3_exec.h, on every loop iteration; a non-null result is a trap. One app runs
// at a time and its calls are serialised, so one deadline is enough.
constexpr char kTrapTimeout[] = "[trap] the app took too long to answer";
volatile bool gDeadlineSet = false;
volatile uint32_t gDeadline = 0;
uint32_t gYields = 0;

void armDeadline(const uint32_t budgetMs) {
  gYields = 0;
  gDeadline = millis() + budgetMs;
  gDeadlineSet = true;
}
void disarmDeadline() { gDeadlineSet = false; }

}  // namespace

extern "C" M3Result m3_Yield() {
  if (!gDeadlineSet) return m3Err_none;
  // millis() is cheap but not free; a tight loop yields millions of times a
  // second.
  if ((++gYields & 0x3FF) != 0) return m3Err_none;
  if (static_cast<int32_t>(millis() - gDeadline) >= 0) return kTrapTimeout;
  return m3Err_none;
}

namespace appstore {
namespace {

// Time budgets per call. Below the 5s task watchdog, so a stuck app is
// stopped by us rather than by a reset.
constexpr uint32_t kInitBudgetMs = 4000;
constexpr uint32_t kRenderBudgetMs = 3000;
constexpr uint32_t kEventBudgetMs = 3000;
constexpr uint32_t kSuspendBudgetMs = 1500;

// The wasm3 value stack. Holds the app's locals and operands, not its shadow
// stack (that is in linear memory).
constexpr uint32_t kValueStackBytes = 64 * 1024;

// The native stack app code runs on, on the device. d_m3MaxNativeStack is
// 28KB; the rest is headroom for the deepest import (drawing text).
constexpr uint32_t kWorkerStackBytes = 40 * 1024;

constexpr size_t kMaxTextBytes = 1024;
constexpr size_t kMaxLogBytes = 200;
constexpr int kMaxDataFiles = 64;
constexpr size_t kBounceBytes = 512;
constexpr int32_t kMinTouchSize = 44;

// WASI errno.
constexpr int32_t kWasiBadFd = 8;

int faceFont(const int32_t face) {
  switch (face) {
    case CP_FACE_DISPLAY:
      return toybox::kDisplayFontId;
    case CP_FACE_TILE:
      return toybox::kTileFontId;
    case CP_FACE_BUTTON:
      return toybox::kButtonFontId;
    case CP_FACE_SERIF_TILE:
      return toybox::kSerifTileFontId;
    case CP_FACE_SERIF_TITLE:
      return toybox::kSerifTitleFontId;
    case CP_FACE_SERIF_SMALL:
      return toybox::kSerifSmallFontId;
    case CP_FACE_READING:
      return toybox::kReadingFontId;
    case CP_FACE_READING_BOLD:
      return toybox::kReadingBoldFontId;
    case CP_FACE_READING_SMALL:
      return toybox::kReadingSmallFontId;
    case CP_FACE_READING_BOLD_SMALL:
      return toybox::kReadingBoldSmallFontId;
    case CP_FACE_HUGE:
      return toybox::kHugeFontId;
    case CP_FACE_LARGE:
      return toybox::kLargeFontId;
    default:
      return toybox::kUiFontId;  // CP_FACE_UI, and the documented fallback
  }
}

fui::Color colorOf(const int32_t c) {
  switch (c) {
    case CP_COLOR_TRANSPARENT:
      return fui::Color::Transparent;
    case CP_COLOR_WHITE:
      return fui::Color::White;
    case CP_COLOR_LIGHT_GRAY:
      return fui::Color::LightGray;
    case CP_COLOR_DARK_GRAY:
      return fui::Color::DarkGray;
    default:
      return fui::Color::Black;
  }
}

fui::Paint paintOf(const int32_t packed) {
  fui::Paint paint;
  switch ((packed >> 8) & 0xFF) {
    case CP_PAINT_SOLID:
      paint.kind = fui::PaintKind::Solid;
      break;
    case CP_PAINT_DITHER:
      paint.kind = fui::PaintKind::Dither;
      break;
    default:
      return fui::Paint::none();  // unknown kinds draw nothing
  }
  paint.color = colorOf(packed & 0xFF);
  return paint;
}

fui::TextStyle styleOf(const int32_t s) {
  fui::TextStyle style;
  style.font = fui::GfxRendererTarget::FONT_SMALL;  // bound to the face per call
  switch (s & 3) {
    case 1:
      style.align = fui::TextAlign::Center;
      break;
    case 2:
      style.align = fui::TextAlign::Right;
      break;
    default:
      style.align = fui::TextAlign::Left;
      break;
  }
  style.color = colorOf((s >> 2) & 7);
  const int lines = (s >> 5) & 0xFF;
  style.maxLines = static_cast<uint8_t>(lines == 0 ? 1 : lines);
  style.bold = ((s >> 13) & 1) != 0;
  style.inverted = ((s >> 14) & 1) != 0;
  switch ((s >> 15) & 3) {
    case 1:
      style.rotation = fui::Rotation::CW90;
      break;
    case 2:
      style.rotation = fui::Rotation::R180;
      break;
    case 3:
      style.rotation = fui::Rotation::CCW90;
      break;
    default:
      style.rotation = fui::Rotation::None;
      break;
  }
  return style;
}

int16_t clamp16(const int32_t v) {
  return static_cast<int16_t>(std::max<int32_t>(-32768, std::min<int32_t>(32767, v)));
}
uint8_t clamp8(const int32_t v) { return static_cast<uint8_t>(std::max<int32_t>(0, std::min<int32_t>(255, v))); }

fui::Rect rectOf(const int32_t x, const int32_t y, const int32_t w, const int32_t h) {
  return fui::Rect{clamp16(x), clamp16(y), clamp16(std::max<int32_t>(0, w)), clamp16(std::max<int32_t>(0, h))};
}

// Minutes east of UTC, from what the C library thinks local time is.
int32_t utcOffsetMinutes() {
  const time_t now = time(nullptr);
  tm local{};
  tm utc{};
  localtime_r(&now, &local);
  gmtime_r(&now, &utc);
  int days = local.tm_yday - utc.tm_yday;
  if (local.tm_year != utc.tm_year) days = local.tm_year > utc.tm_year ? 1 : -1;
  return days * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
}

}  // namespace

// --- The instance ---------------------------------------------------------------

struct WasmApp::Impl {
  enum class Job : uint8_t { Prepare, ApiVersion, Init, Render, Event, Suspend };

  Impl(WasmApp& owner, GfxRenderer& renderer, const Device device)
      : owner(owner), renderer(renderer), device(device), target(renderer, device.hasTouch) {}

  WasmApp& owner;
  GfxRenderer& renderer;
  Device device;
  fui::GfxRendererTarget target;

  Manifest manifest;
  std::string dataDir;

  uint8_t* wasm = nullptr;
  IM3Environment env = nullptr;
  IM3Runtime runtime = nullptr;
  IM3Module module = nullptr;  // owned by `runtime` once loaded
  IM3Function fnInitialize = nullptr;
  IM3Function fnApiVersion = nullptr;
  IM3Function fnInit = nullptr;
  IM3Function fnRender = nullptr;
  IM3Function fnEvent = nullptr;
  IM3Function fnSuspend = nullptr;

  // Set by imports during a call, read by the activity after it.
  bool drawing = false;
  bool renderRequested = false;
  int32_t refresh = CP_REFRESH_FAST;
  bool timerSet = false;
  int32_t timerMs = 0;

  std::string text;           // NUL-terminated copy of the last string drawn or measured
  uint8_t* bounce = nullptr;  // SD transfers go through internal RAM, never straight into PSRAM

  // The call in flight.
  Job job = Job::Prepare;
  int32_t args[4] = {};
  int32_t ret = 0;
  M3Result result = m3Err_none;

  SemaphoreHandle_t lock = nullptr;
#if defined(ARDUINO_ARCH_ESP32)
  TaskHandle_t worker = nullptr;
  bool workerExternal = false;
  volatile bool ending = false;
  TaskHandle_t waiter = nullptr;
  static void trampoline(void* self) { static_cast<Impl*>(self)->workerLoop(); }
  void workerLoop();
#endif

  bool startWorker();
  void stopWorker();
  M3Result runJob();
  M3Result call(Job which, int32_t a = 0, int32_t b = 0, int32_t c = 0, int32_t d = 0);
  void requestExit() { owner.exitRequested_ = true; }
  bool fail(const std::string& why);
  bool failTrap(M3Result trap, const char* during);
  bool loadFile(const std::string& path, uint32_t size, const std::string& sha256, std::string& error);
  bool linkImports(std::string& error);
  std::string dataPath(const char* name, int32_t len) const;
  int64_t dataBytesExcept(const std::string& skip, int& files) const;
  const char* copyText(const void* p, int32_t len);

  ~Impl();
};

WasmApp::Impl::~Impl() {
  stopWorker();
  if (runtime != nullptr) {
    m3_FreeRuntime(runtime);  // frees the loaded module too
  } else if (module != nullptr) {
    m3_FreeModule(module);
  }
  if (env != nullptr) m3_FreeEnvironment(env);
  if (wasm != nullptr) heap_caps_free(wasm);
  if (bounce != nullptr) heap_caps_free(bounce);
#if defined(ARDUINO_ARCH_ESP32)
  // The simulator's FreeRTOS shim has no delete; its mutex is a few bytes.
  if (lock != nullptr) vSemaphoreDelete(lock);
#endif
}

bool WasmApp::Impl::fail(const std::string& why) {
  owner.failed_ = true;
  owner.error_ = why;
  LOG_ERR("WASM", "%s: %s", manifest.id.c_str(), why.c_str());
  return false;
}

bool WasmApp::Impl::failTrap(const M3Result trap, const char* during) {
  std::string why;
  if (trap == kTrapTimeout) {
    why = "It stopped answering";
  } else if (trap == m3Err_trapStackOverflow) {
    why = "It ran out of stack";
  } else if (trap == m3Err_trapOutOfBoundsMemoryAccess) {
    why = "It reached outside its own memory";
  } else if (trap == m3Err_trapExit || trap == m3Err_trapAbort) {
    why = "It gave up";
  } else {
    why = "It crashed";
  }
  why += " while ";
  why += during;
  why += ".";
  // The trap itself is for whoever wrote the app, not whoever is holding it.
  LOG_ERR("WASM", "%s trapped while %s: %s", manifest.id.c_str(), during, trap);
  return fail(why);
}

// --- Running app code -------------------------------------------------------

M3Result WasmApp::Impl::runJob() {
  M3Result r = m3Err_none;
  switch (job) {
    case Job::Prepare:
      // Compiled up front so the first tap is not the one that waits for it,
      // and so a module that will not compile fails here, with a reason,
      // rather than in the middle of a game.
      r = m3_CompileModule(module);
      if (r == m3Err_none && fnInitialize != nullptr) {
        armDeadline(kInitBudgetMs);
        r = m3_CallV(fnInitialize);
      }
      break;
    case Job::ApiVersion:
      armDeadline(kInitBudgetMs);
      r = m3_CallV(fnApiVersion);
      if (r == m3Err_none) r = m3_GetResultsV(fnApiVersion, &ret);
      break;
    case Job::Init:
      armDeadline(kInitBudgetMs);
      r = m3_CallV(fnInit);
      if (r == m3Err_none) r = m3_GetResultsV(fnInit, &ret);
      break;
    case Job::Render:
      armDeadline(kRenderBudgetMs);
      r = m3_CallV(fnRender);
      break;
    case Job::Event:
      armDeadline(kEventBudgetMs);
      r = m3_CallV(fnEvent, args[0], args[1], args[2], args[3]);
      if (r == m3Err_none) r = m3_GetResultsV(fnEvent, &ret);
      break;
    case Job::Suspend:
      if (fnSuspend == nullptr) break;
      armDeadline(kSuspendBudgetMs);
      r = m3_CallV(fnSuspend);
      break;
  }
  disarmDeadline();
  return r;
}

M3Result WasmApp::Impl::call(const Job which, const int32_t a, const int32_t b, const int32_t c, const int32_t d) {
  xSemaphoreTake(lock, portMAX_DELAY);
  job = which;
  args[0] = a;
  args[1] = b;
  args[2] = c;
  args[3] = d;
  ret = 0;
#if defined(ARDUINO_ARCH_ESP32)
  waiter = xTaskGetCurrentTaskHandle();
  xTaskNotifyGive(worker);
  // No timeout: the watchdog above ends every call, and a caller that gave up
  // early would leave the worker writing into `ret` behind it.
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#else
  result = runJob();
#endif
  const M3Result r = result;
  xSemaphoreGive(lock);
  return r;
}

#if defined(ARDUINO_ARCH_ESP32)
void WasmApp::Impl::workerLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (ending) break;
    result = runJob();
    xTaskNotifyGive(waiter);
  }
  // Nothing below may touch this object: the waiter frees it once notified.
  TaskHandle_t last = waiter;
  xTaskNotifyGive(last);
  for (;;) vTaskSuspend(nullptr);
}
#endif

bool WasmApp::Impl::startWorker() {
  lock = xSemaphoreCreateMutex();
  if (lock == nullptr) return false;
#if defined(ARDUINO_ARCH_ESP32)
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t core = 1;
#else
  constexpr BaseType_t core = 0;
#endif
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(&Impl::trampoline, "wasm_app", kWorkerStackBytes, this, 1, &handle, core) == pdPASS) {
    worker = handle;
    workerExternal = false;
    return true;
  }
  // Nothing the app does needs the flash cache disabled, the one thing a
  // PSRAM stack cannot do; GoActivity makes the same trade.
  if (xTaskCreatePinnedToCoreWithCaps(&Impl::trampoline, "wasm_app", kWorkerStackBytes, this, 1, &handle, core,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
    worker = handle;
    workerExternal = true;
    return true;
  }
  // No fallback onto the loop or render task: their stacks are smaller than
  // the native budget wasm3 was built with, so an app could overflow them.
  return false;
#else
  return true;
#endif
}

void WasmApp::Impl::stopWorker() {
#if defined(ARDUINO_ARCH_ESP32)
  if (worker == nullptr) return;
  if (lock != nullptr) xSemaphoreTake(lock, portMAX_DELAY);
  ending = true;
  waiter = xTaskGetCurrentTaskHandle();
  xTaskNotifyGive(worker);
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
  for (int i = 0; i < 100 && eTaskGetState(worker) != eSuspended; ++i) vTaskDelay(1);
  if (eTaskGetState(worker) == eSuspended) {
    if (workerExternal) {
      vTaskDeleteWithCaps(worker);
    } else {
      vTaskDelete(worker);
    }
  } else {
    LOG_ERR("WASM", "App task did not park; leaking it rather than deleting it mid-run");
  }
  worker = nullptr;
  if (lock != nullptr) xSemaphoreGive(lock);
#endif
}

// --- Loading ----------------------------------------------------------------

bool WasmApp::Impl::loadFile(const std::string& path, const uint32_t size, const std::string& sha256,
                             std::string& error) {
  HalFile file;
  if (!Storage.openFileForRead("WASM", path.c_str(), file)) {
    error = "Its program file is missing. Install it again.";
    return false;
  }
  if (file.size() != size) {
    error = "Its program file is the wrong size. Install it again.";
    return false;
  }
  wasm = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (wasm == nullptr) wasm = static_cast<uint8_t*>(malloc(size));
  if (wasm == nullptr) {
    error = "There is not enough memory to load it.";
    return false;
  }
  // Checked again at every launch, not only at install: a card is a file
  // system anyone can write to with a laptop.
  Sha256 hash;
  uint32_t done = 0;
  while (done < size) {
    const int want = static_cast<int>(std::min<uint32_t>(kBounceBytes, size - done));
    const int got = file.read(bounce, want);
    if (got != want) {
      error = "Its program file could not be read.";
      return false;
    }
    std::memcpy(wasm + done, bounce, got);
    hash.update(bounce, got);
    done += got;
  }
  if (hash.hex() != sha256) {
    error = "Its program file does not match what was installed. Install it again.";
    return false;
  }
  return true;
}

std::string WasmApp::Impl::dataPath(const char* name, const int32_t len) const {
  std::string path = dataDir;
  path += '/';
  path.append(name, static_cast<size_t>(len));
  return path;
}

// Bytes in the data folder, leaving out `skip` (the file about to be
// replaced) and our own temporaries. -1 when the folder cannot be read.
int64_t WasmApp::Impl::dataBytesExcept(const std::string& skip, int& files) const {
  files = 0;
  if (!Storage.exists(dataDir.c_str())) return 0;
  HalFile dir = Storage.open(dataDir.c_str());
  if (!dir || !dir.isDirectory()) return -1;
  int64_t total = 0;
  char name[64];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    if (name[0] == '.' || entry.isDirectory()) continue;
    ++files;
    if (skip == name) continue;
    total += static_cast<int64_t>(entry.size());
  }
  return total;
}

const char* WasmApp::Impl::copyText(const void* p, const int32_t len) {
  text.assign(static_cast<const char*>(p), std::min<size_t>(static_cast<size_t>(len), kMaxTextBytes));
  return text.c_str();
}

// --- The imports ----------------------------------------------------------------
//
// Every pointer an app passes is an offset into its own linear memory. SPAN
// checks offset + length against that memory before anything is read or
// written, and traps the app when it is outside. Values are copied with memcpy:
// an app can pass any address, and the Xtensa core faults on unaligned words.

namespace {

WasmApp::Impl& self(IM3Runtime runtime) { return *static_cast<WasmApp::Impl*>(m3_GetUserData(runtime)); }

bool inMemory(IM3Runtime runtime, const uint32_t offset, const int64_t len) {
  if (len < 0) return false;
  return static_cast<uint64_t>(offset) + static_cast<uint64_t>(len) <= m3_GetMemorySize(runtime);
}

#define SPAN(ptr, offset, len)                                                           \
  if (!inMemory(runtime, (offset), (len))) m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess); \
  uint8_t* ptr = static_cast<uint8_t*>(_mem) + (offset);

// --- System

m3ApiRawFunction(cp_api_version_impl) {
  m3ApiReturnType(int32_t);
  m3ApiReturn((CP_API_VERSION_MAJOR << 16) | CP_API_VERSION_MINOR);
}

m3ApiRawFunction(cp_device_info_impl) {
  m3ApiGetArg(uint32_t, out);
  SPAN(head, out, 4);
  int32_t want = 0;
  std::memcpy(&want, head, 4);
  if (want <= 0) m3ApiSuccess();
  WasmApp::Impl& app = self(runtime);
  const fui::DeviceContext device = app.target.deviceContext();
  cp_device_info_t info{};
  info.size = sizeof(info);
  info.width = device.width;
  info.height = device.height;
  info.hasTouch = app.device.hasTouch ? 1 : 0;
  info.hasButtons = app.device.hasButtons ? 1 : 0;
  info.safeTop = device.safeArea.top;
  info.safeRight = device.safeArea.right;
  info.safeBottom = device.safeArea.bottom;
  info.safeLeft = device.safeArea.left;
  info.minTouchSize = kMinTouchSize;
  // An older app knows a shorter struct; a newer one gets what we have and
  // keeps its own zeroes past it.
  const size_t n = std::min<size_t>(static_cast<size_t>(want), sizeof(info));
  SPAN(dest, out, static_cast<int64_t>(n));
  std::memcpy(dest, &info, n);
  m3ApiSuccess();
}

m3ApiRawFunction(cp_log_impl) {
  m3ApiGetArg(int32_t, level);
  m3ApiGetArg(uint32_t, textOff);
  m3ApiGetArg(int32_t, len);
  SPAN(p, textOff, len);
  WasmApp::Impl& app = self(runtime);
  char line[kMaxLogBytes + 1];
  const size_t n = std::min<size_t>(static_cast<size_t>(len), kMaxLogBytes);
  std::memcpy(line, p, n);
  line[n] = '\0';
  if (level <= 0) {
    LOG_ERR("APP", "%s: %s", app.manifest.id.c_str(), line);
  } else if (level == 1) {
    LOG_INF("APP", "%s: %s", app.manifest.id.c_str(), line);
  } else {
    LOG_DBG("APP", "%s: %s", app.manifest.id.c_str(), line);
  }
  m3ApiSuccess();
}

m3ApiRawFunction(cp_now_ms_impl) {
  m3ApiReturnType(int64_t);
  m3ApiReturn(static_cast<int64_t>(millis()));
}

m3ApiRawFunction(cp_unix_time_impl) {
  m3ApiReturnType(int64_t);
  // 2020-01-01: anything earlier is a clock that was never set.
  const time_t now = time(nullptr);
  m3ApiReturn(now < 1577836800 ? 0 : static_cast<int64_t>(now));
}

m3ApiRawFunction(cp_utc_offset_minutes_impl) {
  m3ApiReturnType(int32_t);
  m3ApiReturn(utcOffsetMinutes());
}

m3ApiRawFunction(cp_random_impl) {
  m3ApiReturnType(uint32_t);
  m3ApiReturn(static_cast<uint32_t>(esp_random()));
}

m3ApiRawFunction(cp_request_render_impl) {
  m3ApiGetArg(int32_t, refresh);
  WasmApp::Impl& app = self(runtime);
  // Inside render the frame is already being drawn; asking again would loop.
  if (!app.drawing) {
    const int32_t r = refresh == CP_REFRESH_HALF || refresh == CP_REFRESH_FULL ? refresh : CP_REFRESH_FAST;
    app.refresh = app.renderRequested ? std::max(app.refresh, r) : r;
    app.renderRequested = true;
  }
  m3ApiSuccess();
}

m3ApiRawFunction(cp_set_timer_impl) {
  m3ApiGetArg(int32_t, ms);
  WasmApp::Impl& app = self(runtime);
  app.timerSet = true;
  app.timerMs = ms < 0 ? 0 : ms;
  m3ApiSuccess();
}

m3ApiRawFunction(cp_exit_impl) {
  self(runtime).requestExit();
  m3ApiSuccess();
}

// --- Storage

m3ApiRawFunction(cp_file_read_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, nameOff);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(uint32_t, bufOff);
  m3ApiGetArg(int32_t, cap);
  SPAN(name, nameOff, nameLen);
  SPAN(buf, bufOff, cap);
  WasmApp::Impl& app = self(runtime);
  if (!validFileName(reinterpret_cast<const char*>(name), static_cast<size_t>(nameLen))) m3ApiReturn(-1);
  const std::string path = app.dataPath(reinterpret_cast<const char*>(name), nameLen);
  if (!Storage.exists(path.c_str())) m3ApiReturn(-1);
  HalFile file;
  if (!Storage.openFileForRead("APP", path.c_str(), file)) m3ApiReturn(-1);
  int32_t total = 0;
  while (total < cap) {
    const int want = static_cast<int>(std::min<int32_t>(kBounceBytes, cap - total));
    const int got = file.read(app.bounce, want);
    if (got < 0) m3ApiReturn(-1);
    if (got == 0) break;
    std::memcpy(buf + total, app.bounce, got);
    total += got;
  }
  m3ApiReturn(total);
}

m3ApiRawFunction(cp_file_size_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, nameOff);
  m3ApiGetArg(int32_t, nameLen);
  SPAN(name, nameOff, nameLen);
  WasmApp::Impl& app = self(runtime);
  if (!validFileName(reinterpret_cast<const char*>(name), static_cast<size_t>(nameLen))) m3ApiReturn(-1);
  const std::string path = app.dataPath(reinterpret_cast<const char*>(name), nameLen);
  if (!Storage.exists(path.c_str())) m3ApiReturn(-1);
  HalFile file;
  if (!Storage.openFileForRead("APP", path.c_str(), file)) m3ApiReturn(-1);
  m3ApiReturn(static_cast<int32_t>(file.size()));
}

m3ApiRawFunction(cp_file_write_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, nameOff);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(uint32_t, dataOff);
  m3ApiGetArg(int32_t, len);
  SPAN(name, nameOff, nameLen);
  SPAN(data, dataOff, len);
  WasmApp::Impl& app = self(runtime);
  if (!validFileName(reinterpret_cast<const char*>(name), static_cast<size_t>(nameLen))) m3ApiReturn(-1);
  const std::string bare(reinterpret_cast<const char*>(name), static_cast<size_t>(nameLen));
  const std::string path = app.dataPath(bare.c_str(), nameLen);

  int files = 0;
  const int64_t others = app.dataBytesExcept(bare, files);
  const bool exists = Storage.exists(path.c_str());
  if (others < 0 || (!exists && files >= kMaxDataFiles) ||
      others + len > static_cast<int64_t>(app.manifest.dataQuotaKB) * 1024) {
    LOG_ERR("APP", "%s: write of %s refused: over quota", app.manifest.id.c_str(), bare.c_str());
    m3ApiReturn(-1);
  }
  if (!Storage.ensureDirectoryExists(app.dataDir.c_str())) m3ApiReturn(-1);

  // Beside the file and renamed over it: a power cut leaves the old contents
  // or the new, never half. The leading dot keeps it out of the app's names.
  const std::string part = app.dataDir + "/." + bare + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("APP", part.c_str(), file)) m3ApiReturn(-1);
    int32_t done = 0;
    while (done < len) {
      const size_t n = std::min<size_t>(kBounceBytes, static_cast<size_t>(len - done));
      std::memcpy(app.bounce, data + done, n);
      if (file.write(app.bounce, n) != n) {
        file.close();
        Storage.remove(part.c_str());
        m3ApiReturn(-1);
      }
      done += static_cast<int32_t>(n);
    }
  }
  if (!Storage.replaceFile(part.c_str(), path.c_str())) {
    Storage.remove(part.c_str());
    m3ApiReturn(-1);
  }
  m3ApiReturn(0);
}

m3ApiRawFunction(cp_file_remove_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, nameOff);
  m3ApiGetArg(int32_t, nameLen);
  SPAN(name, nameOff, nameLen);
  WasmApp::Impl& app = self(runtime);
  if (!validFileName(reinterpret_cast<const char*>(name), static_cast<size_t>(nameLen))) m3ApiReturn(-1);
  const std::string path = app.dataPath(reinterpret_cast<const char*>(name), nameLen);
  if (!Storage.exists(path.c_str())) m3ApiReturn(-1);
  m3ApiReturn(Storage.remove(path.c_str()) ? 0 : -1);
}

// --- Drawing

m3ApiRawFunction(cp_measure_text_impl) {
  m3ApiGetArg(int32_t, face);
  m3ApiGetArg(uint32_t, textOff);
  m3ApiGetArg(int32_t, len);
  m3ApiGetArg(int32_t, style);
  m3ApiGetArg(uint32_t, out);
  SPAN(p, textOff, len);
  SPAN(dest, out, sizeof(cp_size_t));
  WasmApp::Impl& app = self(runtime);
  app.target.setFont(fui::GfxRendererTarget::FONT_SMALL, faceFont(face));
  const fui::TextStyle s = styleOf(style);
  const fui::Size size = app.target.measureText(s.font, app.copyText(p, len), s);
  const cp_size_t result{size.width, size.height};
  std::memcpy(dest, &result, sizeof(result));
  m3ApiSuccess();
}

m3ApiRawFunction(cp_line_height_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, face);
  WasmApp::Impl& app = self(runtime);
  app.target.setFont(fui::GfxRendererTarget::FONT_SMALL, faceFont(face));
  m3ApiReturn(static_cast<int32_t>(app.target.lineHeight(fui::GfxRendererTarget::FONT_SMALL)));
}

m3ApiRawFunction(cp_ink_bounds_impl) {
  m3ApiGetArg(int32_t, face);
  m3ApiGetArg(uint32_t, textOff);
  m3ApiGetArg(int32_t, len);
  m3ApiGetArg(int32_t, style);
  m3ApiGetArg(uint32_t, out);
  SPAN(p, textOff, len);
  SPAN(dest, out, sizeof(cp_rect_t));
  WasmApp::Impl& app = self(runtime);
  app.target.setFont(fui::GfxRendererTarget::FONT_SMALL, faceFont(face));
  const fui::TextStyle s = styleOf(style);
  const fui::Rect r = app.target.inkBounds(s.font, app.copyText(p, len), s);
  const cp_rect_t result{r.x, r.y, r.width, r.height};
  std::memcpy(dest, &result, sizeof(result));
  m3ApiSuccess();
}

m3ApiRawFunction(cp_fill_impl) {
  m3ApiGetArg(int32_t, x);
  m3ApiGetArg(int32_t, y);
  m3ApiGetArg(int32_t, w);
  m3ApiGetArg(int32_t, h);
  m3ApiGetArg(int32_t, paint);
  m3ApiGetArg(int32_t, radius);
  m3ApiGetArg(int32_t, corners);
  WasmApp::Impl& app = self(runtime);
  if (app.drawing) app.target.fill(rectOf(x, y, w, h), paintOf(paint), clamp8(radius), clamp8(corners & 0xF));
  m3ApiSuccess();
}

m3ApiRawFunction(cp_stroke_impl) {
  m3ApiGetArg(int32_t, x);
  m3ApiGetArg(int32_t, y);
  m3ApiGetArg(int32_t, w);
  m3ApiGetArg(int32_t, h);
  m3ApiGetArg(int32_t, paint);
  m3ApiGetArg(int32_t, width);
  m3ApiGetArg(int32_t, radius);
  m3ApiGetArg(int32_t, corners);
  WasmApp::Impl& app = self(runtime);
  if (app.drawing) {
    app.target.stroke(rectOf(x, y, w, h), paintOf(paint), clamp8(width), clamp8(radius), clamp8(corners & 0xF));
  }
  m3ApiSuccess();
}

m3ApiRawFunction(cp_line_impl) {
  m3ApiGetArg(int32_t, x0);
  m3ApiGetArg(int32_t, y0);
  m3ApiGetArg(int32_t, x1);
  m3ApiGetArg(int32_t, y1);
  m3ApiGetArg(int32_t, width);
  m3ApiGetArg(int32_t, paint);
  WasmApp::Impl& app = self(runtime);
  if (app.drawing) {
    app.target.line(fui::Point{clamp16(x0), clamp16(y0)}, fui::Point{clamp16(x1), clamp16(y1)}, clamp8(width),
                    paintOf(paint));
  }
  m3ApiSuccess();
}

m3ApiRawFunction(cp_triangle_impl) {
  m3ApiGetArg(int32_t, ax);
  m3ApiGetArg(int32_t, ay);
  m3ApiGetArg(int32_t, bx);
  m3ApiGetArg(int32_t, by);
  m3ApiGetArg(int32_t, cx);
  m3ApiGetArg(int32_t, cy);
  m3ApiGetArg(int32_t, paint);
  WasmApp::Impl& app = self(runtime);
  if (app.drawing) {
    app.target.triangle(fui::Point{clamp16(ax), clamp16(ay)}, fui::Point{clamp16(bx), clamp16(by)},
                        fui::Point{clamp16(cx), clamp16(cy)}, paintOf(paint));
  }
  m3ApiSuccess();
}

m3ApiRawFunction(cp_text_impl) {
  m3ApiGetArg(int32_t, x);
  m3ApiGetArg(int32_t, y);
  m3ApiGetArg(int32_t, w);
  m3ApiGetArg(int32_t, h);
  m3ApiGetArg(uint32_t, textOff);
  m3ApiGetArg(int32_t, len);
  m3ApiGetArg(int32_t, face);
  m3ApiGetArg(int32_t, style);
  SPAN(p, textOff, len);
  WasmApp::Impl& app = self(runtime);
  if (app.drawing) {
    app.target.setFont(fui::GfxRendererTarget::FONT_SMALL, faceFont(face));
    app.target.text(rectOf(x, y, w, h), app.copyText(p, len), styleOf(style));
  }
  m3ApiSuccess();
}

m3ApiRawFunction(cp_bitmap_impl) {
  m3ApiGetArg(int32_t, x);
  m3ApiGetArg(int32_t, y);
  m3ApiGetArg(int32_t, w);
  m3ApiGetArg(int32_t, h);
  m3ApiGetArg(uint32_t, dataOff);
  m3ApiGetArg(int32_t, bw);
  m3ApiGetArg(int32_t, bh);
  m3ApiGetArg(int32_t, format);
  m3ApiGetArg(int32_t, fit);
  m3ApiGetArg(int32_t, paint);
  if (bw <= 0 || bh <= 0 || bw > 2048 || bh > 2048) m3ApiSuccess();
  SPAN(data, dataOff, static_cast<int64_t>((bw + 7) / 8) * bh);
  WasmApp::Impl& app = self(runtime);
  if (!app.drawing) m3ApiSuccess();
  fui::BitmapRef ref;
  ref.data = data;
  ref.width = static_cast<uint16_t>(bw);
  ref.height = static_cast<uint16_t>(bh);
  ref.progmem = false;
  switch (format) {
    case CP_BITMAP_MASK1:
      ref.format = fui::BitmapFormat::Mask1;
      break;
    case CP_BITMAP_BW1:
      ref.format = fui::BitmapFormat::BW1;
      break;
    default:
      m3ApiSuccess();  // unknown formats draw nothing
  }
  fui::BitmapMode mode;
  switch (fit) {
    case CP_FIT_STRETCH:
      mode = fui::BitmapMode::Stretch;
      break;
    case CP_FIT_CONTAIN:
      mode = fui::BitmapMode::Contain;
      break;
    case CP_FIT_COVER:
      mode = fui::BitmapMode::Cover;
      break;
    default:
      mode = fui::BitmapMode::Center;
      break;
  }
  app.target.bitmap(rectOf(x, y, w, h), ref, mode, paintOf(paint), fui::Rotation::None);
  m3ApiSuccess();
}

m3ApiRawFunction(cp_set_clip_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, x);
  m3ApiGetArg(int32_t, y);
  m3ApiGetArg(int32_t, w);
  m3ApiGetArg(int32_t, h);
  WasmApp::Impl& app = self(runtime);
  if (!app.drawing) m3ApiReturn(0);
  if (w <= 0) {
    app.renderer.setClipRect(0, 0, 32767, 32767);
  } else {
    app.target.setClipRect(rectOf(x, y, w, h));
  }
  m3ApiReturn(1);
}

// --- WASI, as much of it as a C++ runtime links ------------------------------
//
// wasi-libc's stdio references these even in an app that never prints. Output
// to stdout and stderr goes to the log; nothing else is a file.

m3ApiRawFunction(wasi_fd_write_impl) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, fd);
  m3ApiGetArg(uint32_t, iovsOff);
  m3ApiGetArg(int32_t, iovsLen);
  m3ApiGetArg(uint32_t, writtenOff);
  if (iovsLen < 0 || iovsLen > 64) m3ApiReturn(kWasiBadFd);
  SPAN(iovs, iovsOff, static_cast<int64_t>(iovsLen) * 8);
  SPAN(written, writtenOff, 4);
  if (fd != 1 && fd != 2) m3ApiReturn(kWasiBadFd);
  WasmApp::Impl& app = self(runtime);
  char line[kMaxLogBytes + 1];
  size_t used = 0;
  uint32_t total = 0;
  for (int32_t i = 0; i < iovsLen; ++i) {
    uint32_t bufOff = 0;
    uint32_t bufLen = 0;
    std::memcpy(&bufOff, iovs + i * 8, 4);
    std::memcpy(&bufLen, iovs + i * 8 + 4, 4);
    SPAN(buf, bufOff, bufLen);
    const size_t n = std::min<size_t>(bufLen, kMaxLogBytes - used);
    std::memcpy(line + used, buf, n);
    used += n;
    total += bufLen;
  }
  while (used > 0 && (line[used - 1] == '\n' || line[used - 1] == '\r')) --used;
  line[used] = '\0';
  if (used > 0) LOG_INF("APP", "%s: %s", app.manifest.id.c_str(), line);
  std::memcpy(written, &total, 4);
  m3ApiReturn(0);
}

m3ApiRawFunction(wasi_fd_seek_impl) {
  m3ApiReturnType(int32_t);
  m3ApiReturn(kWasiBadFd);
}

m3ApiRawFunction(wasi_fd_close_impl) {
  m3ApiReturnType(int32_t);
  m3ApiReturn(kWasiBadFd);
}

#undef SPAN

struct ImportEntry {
  WasmApp::ImportSpec spec;
  M3RawCall fn;
};

// The firmware's half of the API. host-tests/appstore/check_api.py reads this
// table: every crossplay row has to be in docs/app-api/compat.json with the
// same signature, and every compat.json function has to be here.
constexpr char kCrossplay[] = "crossplay";
constexpr char kWasi[] = "wasi_snapshot_preview1";
const ImportEntry kImports[] = {
    // api 1.0
    {{kCrossplay, "api_version", "i()"}, &cp_api_version_impl},
    {{kCrossplay, "device_info", "v(i)"}, &cp_device_info_impl},
    {{kCrossplay, "log", "v(iii)"}, &cp_log_impl},
    {{kCrossplay, "now_ms", "I()"}, &cp_now_ms_impl},
    {{kCrossplay, "unix_time", "I()"}, &cp_unix_time_impl},
    {{kCrossplay, "utc_offset_minutes", "i()"}, &cp_utc_offset_minutes_impl},
    {{kCrossplay, "random", "i()"}, &cp_random_impl},
    {{kCrossplay, "request_render", "v(i)"}, &cp_request_render_impl},
    {{kCrossplay, "set_timer", "v(i)"}, &cp_set_timer_impl},
    {{kCrossplay, "exit", "v()"}, &cp_exit_impl},
    {{kCrossplay, "file_read", "i(iiii)"}, &cp_file_read_impl},
    {{kCrossplay, "file_size", "i(ii)"}, &cp_file_size_impl},
    {{kCrossplay, "file_write", "i(iiii)"}, &cp_file_write_impl},
    {{kCrossplay, "file_remove", "i(ii)"}, &cp_file_remove_impl},
    {{kCrossplay, "measure_text", "v(iiiii)"}, &cp_measure_text_impl},
    {{kCrossplay, "line_height", "i(i)"}, &cp_line_height_impl},
    {{kCrossplay, "ink_bounds", "v(iiiii)"}, &cp_ink_bounds_impl},
    {{kCrossplay, "fill", "v(iiiiiii)"}, &cp_fill_impl},
    {{kCrossplay, "stroke", "v(iiiiiiii)"}, &cp_stroke_impl},
    {{kCrossplay, "line", "v(iiiiii)"}, &cp_line_impl},
    {{kCrossplay, "triangle", "v(iiiiiii)"}, &cp_triangle_impl},
    {{kCrossplay, "text", "v(iiiiiiii)"}, &cp_text_impl},
    {{kCrossplay, "bitmap", "v(iiiiiiiiii)"}, &cp_bitmap_impl},
    {{kCrossplay, "set_clip", "i(iiii)"}, &cp_set_clip_impl},
    // WASI shims
    {{kWasi, "fd_write", "i(iiii)"}, &wasi_fd_write_impl},
    {{kWasi, "fd_seek", "i(iIii)"}, &wasi_fd_seek_impl},
    {{kWasi, "fd_close", "i(i)"}, &wasi_fd_close_impl},
};
constexpr int kImportCount = static_cast<int>(sizeof(kImports) / sizeof(kImports[0]));

}  // namespace

bool WasmApp::Impl::linkImports(std::string& error) {
  for (const ImportEntry& entry : kImports) {
    const M3Result r = m3_LinkRawFunction(module, entry.spec.module, entry.spec.name, entry.spec.signature, entry.fn);
    // Not imported by this app: fine, most apps use a subset.
    if (r == m3Err_none || r == m3Err_functionLookupFailed) continue;
    error = std::string("It expects ") + entry.spec.module + "." + entry.spec.name +
            " to work differently. It was built for another version of the app API.";
    LOG_ERR("WASM", "link %s.%s: %s", entry.spec.module, entry.spec.name, r);
    return false;
  }
  const char* missingModule = nullptr;
  const char* missingField = nullptr;
  if (m3fork_UnlinkedImports(module, &missingModule, &missingField) > 0) {
    error = std::string("It needs ") + (missingModule ? missingModule : "?") + "." +
            (missingField ? missingField : "?") + ", which this firmware does not have. Update the firmware.";
    return false;
  }
  return true;
}

// --- WasmApp ------------------------------------------------------------------

const WasmApp::ImportSpec* WasmApp::imports(int& count) {
  static ImportSpec specs[kImportCount];
  for (int i = 0; i < kImportCount; ++i) specs[i] = kImports[i].spec;
  count = kImportCount;
  return specs;
}

WasmApp::WasmApp(GfxRenderer& renderer, const Device device)
    : impl_(new (std::nothrow) Impl(*this, renderer, device)) {}

WasmApp::~WasmApp() = default;

bool WasmApp::start(const std::string& packageDir, const Manifest& manifest, const std::string& dataDir) {
  if (!impl_) {
    failed_ = true;
    error_ = "There is not enough memory to start it.";
    return false;
  }
  Impl& m = *impl_;
  m.manifest = manifest;
  m.dataDir = dataDir;

  m.bounce = static_cast<uint8_t*>(heap_caps_malloc(kBounceBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (m.bounce == nullptr) return m.fail("There is not enough memory to start it.");

  const PackageFile* entry = nullptr;
  for (const PackageFile& f : manifest.files) {
    if (f.name == manifest.entry) entry = &f;
  }
  if (entry == nullptr) return m.fail("Its manifest names no program file.");

  std::string error;
  if (!m.loadFile(packageDir + "/" + entry->name, entry->size, entry->sha256, error)) return m.fail(error);

  m.env = m3_NewEnvironment();
  if (m.env == nullptr) return m.fail("There is not enough memory to start it.");
  m.runtime = m3_NewRuntime(m.env, kValueStackBytes, &m);
  if (m.runtime == nullptr) return m.fail("There is not enough memory to start it.");

  M3Result r = m3_ParseModule(m.env, &m.module, m.wasm, entry->size);
  if (r != m3Err_none) {
    m.module = nullptr;
    return m.fail(std::string("Its program file is not one this device can read (") + r + ").");
  }
  r = m3_LoadModule(m.runtime, m.module);
  if (r != m3Err_none) {
    m3_FreeModule(m.module);
    m.module = nullptr;
    return m.fail(std::string("It could not be loaded (") + r + ").");
  }
  if (!m3fork_LimitMemory(m.runtime, manifest.memoryKB * 1024)) {
    return m.fail("It starts with more memory than its manifest asks for.");
  }
  if (!m.linkImports(error)) return m.fail(error);

  m3_FindFunction(&m.fnInitialize, m.runtime, "_initialize");  // optional: only reactor builds have it
  m3_FindFunction(&m.fnSuspend, m.runtime, "cp_app_suspend");  // optional
  if (m3_FindFunction(&m.fnApiVersion, m.runtime, "cp_app_api_version") != m3Err_none ||
      m3_FindFunction(&m.fnInit, m.runtime, "cp_app_init") != m3Err_none ||
      m3_FindFunction(&m.fnRender, m.runtime, "cp_app_render") != m3Err_none ||
      m3_FindFunction(&m.fnEvent, m.runtime, "cp_app_event") != m3Err_none) {
    return m.fail("It is missing one of the cp_app_ entry points. Was it built with the app SDK?");
  }
  if (m3_GetArgCount(m.fnEvent) != 4) return m.fail("Its cp_app_event takes the wrong arguments.");

  if (!m.startWorker()) return m.fail("There is not enough memory to start it.");

  r = m.call(Impl::Job::Prepare);
  if (r != m3Err_none) return m.failTrap(r, "starting");

  // The manifest is what the store checked; the module is what runs. They
  // have to be the same API, or the check was about something else.
  r = m.call(Impl::Job::ApiVersion);
  if (r != m3Err_none) return m.failTrap(r, "starting");
  if (m.ret != manifest.api.packed()) {
    return m.fail("It says it was built for app API " + ApiVersion::fromPacked(m.ret).str() +
                  ", but its manifest says " + manifest.api.str() + ".");
  }

  r = m.call(Impl::Job::Init);
  if (r != m3Err_none) return m.failTrap(r, "starting");
  if (m.ret != 0) return m.fail("It could not start (it returned " + std::to_string(m.ret) + ").");
  LOG_INF("WASM", "%s %s running", manifest.id.c_str(), manifest.version.c_str());
  return true;
}

bool WasmApp::render() {
  if (failed_ || !impl_) return false;
  Impl& m = *impl_;
  const auto saved = m.renderer.getClipRect();
  m.drawing = true;
  const M3Result r = m.call(Impl::Job::Render);
  m.drawing = false;
  m.renderer.setClipRect(saved[0], saved[1], saved[2], saved[3]);
  if (r != m3Err_none) return m.failTrap(r, "drawing");
  return true;
}

int32_t WasmApp::event(const int32_t type, const int32_t a, const int32_t b, const int32_t c) {
  if (failed_ || !impl_) return -1;
  const M3Result r = impl_->call(Impl::Job::Event, type, a, b, c);
  if (r != m3Err_none) {
    impl_->failTrap(r, "handling input");
    return -1;
  }
  return impl_->ret != 0 ? 1 : 0;
}

void WasmApp::suspend() {
  if (failed_ || !impl_ || impl_->fnInit == nullptr) return;
  const M3Result r = impl_->call(Impl::Job::Suspend);
  if (r != m3Err_none) impl_->failTrap(r, "saving");
}

bool WasmApp::takeRenderRequest(int32_t& refresh) {
  if (!impl_ || !impl_->renderRequested) return false;
  refresh = impl_->refresh;
  impl_->renderRequested = false;
  impl_->refresh = CP_REFRESH_FAST;
  return true;
}

bool WasmApp::takeTimer(int32_t& ms) {
  if (!impl_ || !impl_->timerSet) return false;
  ms = impl_->timerMs;
  impl_->timerSet = false;
  return true;
}

}  // namespace appstore
