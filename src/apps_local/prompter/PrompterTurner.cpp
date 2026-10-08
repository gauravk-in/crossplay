#include "PrompterTurner.h"

#if defined(PROMPTER_BLE) && !defined(SIMULATOR)

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

// Keeps the Bluetooth controller's memory at boot. Without it initArduino()
// releases the BLE controller's reservation before any app could ask for it,
// and BLEDevice::init() then fails for good.
extern "C" bool bleInUse(void) { return true; }

namespace prompter {
namespace {

constexpr uint16_t kHidService = 0x1812;
constexpr uint16_t kReportMap = 0x2A4B;
constexpr uint16_t kReport = 0x2A4D;
constexpr uint16_t kReportReference = 0x2908;
constexpr uint32_t kRetryMs = 4000;
constexpr int kMaxFound = 12;
constexpr int kMaxReports = 8;
constexpr uint32_t kWorkerStack = 6144;
// Long enough for a first pairing, which the remote may hold for a keypress.
constexpr uint32_t kPairTimeoutMs = 30000;

// The last line ESP-IDF or the BLE library logged while the radio came up: the
// library reports why init failed only there, and a reader has no serial port.
char initLog[96] = "";
vprintf_like_t passLog = nullptr;

int captureLog(const char* format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  char line[160];
  vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);
  // Drop the colour codes and the "E (1234) " prefix, keep the message.
  const char* text = line;
  if (const char* stamp = strstr(text, ") ")) text = stamp + 2;
  size_t n = 0;
  for (const char* c = text; *c != '\0' && n + 1 < sizeof(initLog); ++c) {
    if (*c == '\033') {
      while (*c != '\0' && *c != 'm') ++c;
      if (*c == '\0') break;
      continue;
    }
    if (*c == '\n' || *c == '\r') break;
    initLog[n++] = *c;
  }
  if (n > 0) initLog[n] = '\0';
  return passLog != nullptr ? passLog(format, args) : vprintf(format, args);
}

// One link at a time, and the BLE library's callbacks are plain functions, so
// the link's state lives here rather than in the object.
struct Shared {
  SemaphoreHandle_t lock = nullptr;
  QueueHandle_t turns = nullptr;
  TaskHandle_t worker = nullptr;
  std::atomic<bool> stopping{false};
  std::atomic<bool> workerDone{true};
  std::atomic<uint8_t> state{0};
  std::atomic<uint32_t> generation{0};
  std::atomic<bool> swap{false};

  // Guarded by `lock`.
  std::vector<TurnerLink::Found> found;
  std::string wantAddress;
  int wantType = 0;
  std::string detail;
  std::string failure;

  // Written by the worker while connecting, read by the host task's notify
  // callback after; a remote's reports cannot arrive before it subscribes.
  struct Report {
    uint16_t handle;
    ReportKind kind;
  };
  Report reports[kMaxReports] = {};
  int reportCount = 0;
  TurnDecoder decoder;

  BLEClient* client = nullptr;
};

Shared g;

void setState(const TurnerLink::State s) { g.state.store(static_cast<uint8_t>(s)); }

void setDetail(const std::string& text) {
  xSemaphoreTake(g.lock, portMAX_DELAY);
  g.detail = text;
  xSemaphoreGive(g.lock);
}

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
 public:
  void onResult(BLEAdvertisedDevice device) override {
    const bool hid = (device.haveServiceUUID() && device.isAdvertisingService(BLEUUID(kHidService))) ||
                     (device.haveAppearance() && device.getAppearance() >= 0x03C0 && device.getAppearance() <= 0x03CF);
    if (!hid) return;
    TurnerLink::Found f;
    f.address = device.getAddress().toString().c_str();
    f.type = device.getAddressType();
    f.name = device.haveName() ? device.getName().c_str() : "";
    f.rssi = device.getRSSI();
    xSemaphoreTake(g.lock, portMAX_DELAY);
    bool known = false;
    for (TurnerLink::Found& have : g.found) {
      if (have.address == f.address) {
        if (have.name.empty()) have.name = f.name;
        have.rssi = f.rssi;
        known = true;
      }
    }
    if (!known && static_cast<int>(g.found.size()) < kMaxFound) g.found.push_back(f);
    xSemaphoreGive(g.lock);
  }
};

ScanCallbacks scanCallbacks;

void scanDone(BLEScanResults) {
  if (g.state.load() == static_cast<uint8_t>(TurnerLink::State::Scanning)) setState(TurnerLink::State::Idle);
}

class ClientCallbacks : public BLEClientCallbacks {
 public:
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override {
    if (g.state.load() == static_cast<uint8_t>(TurnerLink::State::Connected)) {
      setState(TurnerLink::State::Idle);
      g.generation.fetch_add(1);
      LOG_INF("PROMPT", "page turner disconnected");
    }
  }
};

ClientCallbacks clientCallbacks;

void onReport(BLERemoteCharacteristic* characteristic, uint8_t* data, const size_t length, bool) {
  if (characteristic == nullptr || g.turns == nullptr) return;
  ReportKind kind = ReportKind::Unknown;
  const uint16_t handle = characteristic->getHandle();
  for (int i = 0; i < g.reportCount; ++i) {
    if (g.reports[i].handle == handle) kind = g.reports[i].kind;
  }
  const Turn turn = applySwap(g.decoder.feed(kind, data, length), g.swap.load());
  if (turn != Turn::None) xQueueSend(g.turns, &turn, 0);
}

// Waits for the link to be encrypted. The BLE library starts security itself the
// moment the link comes up, so by the time connect() returns the pairing may
// already be over; its secureConnection() then waits forever for an event that
// has been and gone, which is why this polls the link state instead.
bool waitEncrypted(BLEClient* client) {
  const uint16_t conn = client->getConnId();
  ble_gap_conn_desc desc;
  if (ble_gap_conn_find(conn, &desc) != 0) return false;
  if (desc.sec_state.encrypted) return true;
  const int rc = ble_gap_security_initiate(conn);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    LOG_ERR("PROMPT", "security initiate failed: %d", rc);
    return false;
  }
  const uint32_t start = millis();
  while (millis() - start < kPairTimeoutMs) {
    if (g.stopping.load() || !client->isConnected()) return false;
    if (ble_gap_conn_find(conn, &desc) != 0) return false;
    if (desc.sec_state.encrypted) return true;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return false;
}

// One whole connection: link, encryption, the HID service, every input report.
bool connectOnce(const std::string& address, const int type, std::string& why) {
  if (g.client == nullptr) {
    g.client = BLEDevice::createClient();
    if (g.client == nullptr) {
      why = "Bluetooth has no room for another connection.";
      return false;
    }
    g.client->setClientCallbacks(&clientCallbacks);
  }
  BLEClient* client = g.client;
  if (!client->connect(BLEAddress(String(address.c_str()), static_cast<uint8_t>(type)), static_cast<uint8_t>(type))) {
    why = "Press a button on the remote to wake it.";
    return false;
  }
  if (g.stopping.load()) return false;
  // HID devices refuse to be read until the link is encrypted; the first time,
  // this is the pairing.
  if (!waitEncrypted(client)) {
    why = "The remote would not pair. Put it in pairing mode and try again.";
    client->disconnect();
    return false;
  }
  BLERemoteService* hid = client->getService(BLEUUID(kHidService));
  if (hid == nullptr) {
    why = "That device is not a keyboard or page turner.";
    client->disconnect();
    return false;
  }

  ReportKinds kinds;
  if (BLERemoteCharacteristic* map = hid->getCharacteristic(BLEUUID(kReportMap))) {
    const String raw = map->readValue();
    kinds = parseReportMap(reinterpret_cast<const uint8_t*>(raw.c_str()), raw.length());
  }

  g.reportCount = 0;
  g.decoder.reset();
  int subscribed = 0;
  std::map<uint16_t, BLERemoteCharacteristic*>* all = hid->getCharacteristicsByHandle();
  if (all != nullptr) {
    for (auto& entry : *all) {
      BLERemoteCharacteristic* c = entry.second;
      if (c == nullptr || !(c->getUUID() == BLEUUID(kReport)) || !c->canNotify()) continue;
      ReportKind kind = ReportKind::Unknown;
      if (BLERemoteDescriptor* ref = c->getDescriptor(BLEUUID(kReportReference))) {
        const String value = ref->readValue();
        // [report id, report type]; type 1 is an input report.
        if (value.length() >= 2 && static_cast<uint8_t>(value[1]) != 1) continue;
        if (value.length() >= 1) kind = kinds.kindOf(static_cast<uint8_t>(value[0]));
      }
      if (kind == ReportKind::Other) continue;
      if (g.reportCount < kMaxReports) g.reports[g.reportCount++] = {c->getHandle(), kind};
      if (c->subscribe(true, onReport)) ++subscribed;
    }
  }
  if (subscribed == 0) {
    why = "The remote has no keys the reader can listen to.";
    client->disconnect();
    return false;
  }
  return true;
}

void workerLoop(void*) {
  uint32_t nextTry = 0;
  while (!g.stopping.load()) {
    xSemaphoreTake(g.lock, portMAX_DELAY);
    const std::string address = g.wantAddress;
    const int type = g.wantType;
    xSemaphoreGive(g.lock);

    const auto state = static_cast<TurnerLink::State>(g.state.load());
    const bool connected = g.client != nullptr && g.client->isConnected();
    if (!address.empty() && !connected && state != TurnerLink::State::Scanning &&
        static_cast<int32_t>(millis() - nextTry) >= 0) {
      setState(TurnerLink::State::Connecting);
      std::string why;
      const bool ok = connectOnce(address, type, why);
      if (g.stopping.load()) break;
      if (ok) {
        std::string name = address;
        setDetail(name);
        setState(TurnerLink::State::Connected);
        LOG_INF("PROMPT", "page turner connected: %s", address.c_str());
      } else {
        setDetail(why);
        setState(TurnerLink::State::Idle);
        nextTry = millis() + kRetryMs;
      }
      g.generation.fetch_add(1);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  g.workerDone.store(true);
  vTaskDelete(nullptr);
}

}  // namespace

bool TurnerLink::available() { return true; }

bool TurnerLink::begin() {
  if (running_) return true;
  if (g.lock == nullptr) g.lock = xSemaphoreCreateMutex();
  if (g.turns == nullptr) g.turns = xQueueCreate(8, sizeof(Turn));
  if (g.lock == nullptr || g.turns == nullptr) {
    LOG_ERR("PROMPT", "OOM: page turner queue");
    return false;
  }
  const unsigned freeKb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  const unsigned blockKb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
  const bool wifiOn = WiFi.getMode() != WIFI_MODE_NULL;
  initLog[0] = '\0';
  passLog = esp_log_set_vprintf(&captureLog);
  const bool up = BLEDevice::init("CrossPlay");
  esp_log_set_vprintf(passLog);
  passLog = nullptr;
  if (!up) {
    char why[192];
    snprintf(why, sizeof(why), "%s (RAM %uK free, %uK block%s)", initLog[0] != '\0' ? initLog : "no error logged",
             freeKb, blockKb, wifiOn ? ", Wi-Fi on" : "");
    LOG_ERR("PROMPT", "BLE init failed: %s", why);
    xSemaphoreTake(g.lock, portMAX_DELAY);
    g.failure = why;
    xSemaphoreGive(g.lock);
    return false;
  }
  xSemaphoreTake(g.lock, portMAX_DELAY);
  g.failure.clear();
  xSemaphoreGive(g.lock);
  // Just Works bonding: a page turner has no screen to show a code on.
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  xQueueReset(g.turns);
  g.stopping.store(false);
  g.workerDone.store(false);
  g.client = nullptr;
  setState(State::Idle);
  if (xTaskCreate(&workerLoop, "PromptBLE", kWorkerStack, nullptr, 1, &g.worker) != pdPASS) {
    LOG_ERR("PROMPT", "page turner task did not start");
    xSemaphoreTake(g.lock, portMAX_DELAY);
    g.failure = "no memory for the page turner task";
    xSemaphoreGive(g.lock);
    g.workerDone.store(true);
    BLEDevice::deinit(false);
    setState(State::Off);
    return false;
  }
  running_ = true;
  return true;
}

void TurnerLink::end() {
  if (!running_) return;
  running_ = false;
  g.stopping.store(true);
  if (BLEScan* scan = BLEDevice::getScan()) scan->stop();
  // A connect in flight waits on the controller; cancelling ends the wait now.
  ble_gap_conn_cancel();
  for (int i = 0; i < 100 && !g.workerDone.load(); ++i) delay(50);
  if (g.client != nullptr && g.client->isConnected()) g.client->disconnect();
  // deinit() deletes the client it handed out.
  g.client = nullptr;
  g.reportCount = 0;
  BLEDevice::deinit(false);
  setState(State::Off);
  xSemaphoreTake(g.lock, portMAX_DELAY);
  g.found.clear();
  xSemaphoreGive(g.lock);
}

void TurnerLink::startScan(const int seconds) {
  if (!running_) return;
  xSemaphoreTake(g.lock, portMAX_DELAY);
  g.found.clear();
  xSemaphoreGive(g.lock);
  BLEScan* scan = BLEDevice::getScan();
  if (scan == nullptr) return;
  scan->setAdvertisedDeviceCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(90);
  setState(State::Scanning);
  if (!scan->start(static_cast<uint32_t>(seconds), &scanDone, false)) setState(State::Idle);
}

bool TurnerLink::scanning() const { return state() == State::Scanning; }

std::vector<TurnerLink::Found> TurnerLink::found() const {
  std::vector<Found> out;
  if (g.lock == nullptr) return out;
  xSemaphoreTake(g.lock, portMAX_DELAY);
  out = g.found;
  xSemaphoreGive(g.lock);
  return out;
}

void TurnerLink::follow(const std::string& address, const int type, const bool swap) {
  swap_ = swap;
  g.swap.store(swap);
  if (!running_) return;
  if (scanning()) {
    if (BLEScan* scan = BLEDevice::getScan()) scan->stop();
    setState(State::Idle);
  }
  xSemaphoreTake(g.lock, portMAX_DELAY);
  const bool changed = g.wantAddress != address;
  g.wantAddress = address;
  g.wantType = type;
  g.detail.clear();
  xSemaphoreGive(g.lock);
  if (changed && g.client != nullptr && g.client->isConnected()) g.client->disconnect();
}

void TurnerLink::forget() { follow(std::string(), 0, swap_); }

TurnerLink::State TurnerLink::state() const { return static_cast<State>(g.state.load()); }

std::string TurnerLink::detail() const {
  if (g.lock == nullptr) return std::string();
  xSemaphoreTake(g.lock, portMAX_DELAY);
  const std::string out = g.detail;
  xSemaphoreGive(g.lock);
  return out;
}

std::string TurnerLink::failure() const {
  if (g.lock == nullptr) return std::string();
  xSemaphoreTake(g.lock, portMAX_DELAY);
  const std::string out = g.failure;
  xSemaphoreGive(g.lock);
  return out;
}

Turn TurnerLink::takeTurn() {
  Turn turn = Turn::None;
  if (g.turns != nullptr && xQueueReceive(g.turns, &turn, 0) == pdTRUE) return turn;
  return Turn::None;
}

uint32_t TurnerLink::generation() const { return g.generation.load(); }

}  // namespace prompter

#else

namespace prompter {

bool TurnerLink::available() { return false; }
bool TurnerLink::begin() { return false; }
void TurnerLink::end() { running_ = false; }
void TurnerLink::startScan(int) {}
bool TurnerLink::scanning() const { return false; }
std::vector<TurnerLink::Found> TurnerLink::found() const { return {}; }
void TurnerLink::follow(const std::string&, int, const bool swap) { swap_ = swap; }
void TurnerLink::forget() {}
TurnerLink::State TurnerLink::state() const { return State::Off; }
std::string TurnerLink::detail() const { return std::string(); }
std::string TurnerLink::failure() const { return std::string(); }
Turn TurnerLink::takeTurn() { return Turn::None; }
uint32_t TurnerLink::generation() const { return 0; }

}  // namespace prompter

#endif
