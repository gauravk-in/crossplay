#include "BambuActivity.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <ctime>

#include "../../DevMode.h"
#include "../../WifiCredentialStore.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "BambuLink.h"
#include "BambuStore.h"

namespace fui = freeink::ui;

namespace {

constexpr unsigned long kJoinTimeoutMs = 20000;
// A change of stage, of spools or of the connection is drawn at once; the
// numbers that tick during a print (percent, layer, temperatures) wait this
// long, so a panel that takes a second per refresh is not refreshing all day.
constexpr unsigned long kTickPaintMs = 20000;
// The card gets the last state at most this often, and on the way out.
constexpr unsigned long kSaveEveryMs = 5UL * 60UL * 1000UL;
// After the LAN fails with the cloud to fall back on, the LAN is not tried
// again for this long, so a reader away from home does not spend ten seconds
// of every reconnect on an address that is not there.
constexpr unsigned long kLocalRetryMs = 10UL * 60UL * 1000UL;
// A clean refresh every this many paints, fast ones between.
constexpr int kCleanEvery = 10;

unsigned long retryDelayMs(const int failures) {
  switch (failures) {
    case 0:
    case 1:
      return 5000;
    case 2:
      return 15000;
    case 3:
      return 30000;
    default:
      return 60000;
  }
}

// The percentage is the one number read from across the room.
constexpr int kHeroFont = toybox::kLargeFontId;

}  // namespace

std::unique_ptr<Activity> BambuActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<BambuActivity>(renderer, mappedInput);
}

void BambuActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  {
    char id[32];
#ifdef SIMULATOR
    std::snprintf(id, sizeof(id), "crossplay-sim");
#else
    std::snprintf(id, sizeof(id), "crossplay-%06lx", static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFF));
#endif
    clientId_ = id;
  }
  if (!bambu::store::begin()) {
    showNotice("The card would not open, so nothing can be saved.");
    return;
  }
  {
    RenderLock lock(*this);
    loadConfig();
    // The last thing the printer said, so the screen is not empty while the
    // radio comes up. It is marked as old until the printer speaks.
    const std::string snapshot = bambu::store::read(bambu::store::kSnapshotPath, bambu::store::kSnapshotMax);
    if (!snapshot.empty()) bambu::mergeReport(working_, snapshot);
    state_ = working_;
  }
  if (configured_) {
    setView(View::Monitor);
    queueConnect(true);
  } else {
    setView(View::Setup);
  }
}

void BambuActivity::onExit() {
  saveSnapshot(true);
  link_.stop();
  stopPhone();
  releaseWifi();
  Activity::onExit();
}

// --- Data ----------------------------------------------------------------

void BambuActivity::loadConfig() {
  config_ = bambu::parseConfig(bambu::store::read(bambu::store::kConfigPath, bambu::kMaxConfigBytes));
#if BAMBU_CLOUD
  account_ = bambu::cloud::parseAccount(bambu::store::read(bambu::store::kCloudPath, bambu::cloud::kMaxFileBytes));
#endif
  configured_ =
      bambu::isComplete(config_) || (BAMBU_CLOUD && !config_.serial.empty() && bambu::cloud::signedIn(account_));
}

void BambuActivity::saveSnapshot(const bool force) {
  if (!unsaved_ || !state_.seen) return;
  if (!force && millis() - lastSavedAt_ < kSaveEveryMs) return;
  if (!bambu::store::write(bambu::store::kSnapshotPath, bambu::formatSnapshot(state_))) {
    LOG_ERR("BAMBU", "last.json was not written");
    return;
  }
  unsaved_ = false;
  lastSavedAt_ = millis();
}

// --- Navigation ----------------------------------------------------------

void BambuActivity::setView(const View view) {
  {
    RenderLock lock(*this);
    view_ = view;
    interactionsReady_ = false;
    dirty_ = false;
    urgent_ = false;
  }
  requestUpdate();
}

void BambuActivity::openHome() { setView(configured_ ? View::Monitor : View::Setup); }

void BambuActivity::showNotice(const std::string& text) {
  {
    RenderLock lock(*this);
    notice_ = text;
  }
  setView(View::Notice);
}

// --- The connection --------------------------------------------------------

void BambuActivity::queueConnect(const bool fromTap) {
  if (!configured_) return;
  connectQueued_ = true;
  connectFromTap_ = fromTap;
  nextConnectAt_ = 0;
}

void BambuActivity::pumpInput() {
  mappedInput.update();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) cancel_ = true;
}

void BambuActivity::runConnect() {
  connectQueued_ = false;
  cancel_ = false;
  {
    RenderLock lock(*this);
    connecting_ = true;
    live_ = false;
  }
  // Drawn before the radio's wait, so the screen says what is happening.
  requestUpdate();
  lastPaintAt_ = millis();

  std::string why;
  if (!joinWifi(why)) {
    {
      RenderLock lock(*this);
      connecting_ = false;
    }
    if (cancel_) {
      shelf::leave(renderer, mappedInput);
      return;
    }
#ifndef SIMULATOR
    if (connectFromTap_) {
      WiFi.mode(WIFI_STA);
      startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                                 dropLink("No Wi-Fi, so the printer is out of reach.");
                                 return;
                               }
                               queueConnect(true);
                             });
      return;
    }
#endif
    dropLink(why);
    return;
  }

  const auto abort = [this] {
    pumpInput();
    return cancel_;
  };
  const bool canLocal = bambu::isComplete(config_);
  const bool canCloud = BAMBU_CLOUD && !config_.serial.empty() && bambu::cloud::signedIn(account_);
  const bool localRecentlyFailed = localFailedAt_ != 0 && millis() - localFailedAt_ < kLocalRetryMs;
  bool ok = false;
  bool cloud = false;
  if (canLocal && !(canCloud && localRecentlyFailed)) {
    ok = link_.connect(bambu::localTarget(config_), clientId_, abort, why);
    if (ok) {
      localFailedAt_ = 0;
    } else if (!cancel_) {
      localFailedAt_ = millis();
      if (localFailedAt_ == 0) localFailedAt_ = 1;
      LOG_ERR("BAMBU", "LAN connect failed: %s", why.c_str());
    }
  }
  if (!ok && !cancel_ && canCloud) {
    {
      RenderLock lock(*this);
      tryingCloud_ = true;
    }
    if (canLocal) requestUpdate();
    ok = link_.connect(bambu::cloudTarget(config_, account_), clientId_, abort, why);
    cloud = ok;
  }
  {
    RenderLock lock(*this);
    tryingCloud_ = false;
    viaCloud_ = cloud;
  }
  if (cancel_) {
    link_.stop();
    {
      RenderLock lock(*this);
      connecting_ = false;
    }
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (!ok) {
    LOG_ERR("BAMBU", "connect failed: %s", why.c_str());
    dropLink(why);
    return;
  }
  LOG_INF("BAMBU", "connected %s", viaCloud_ ? "through Bambu's cloud" : config_.host.c_str());
  {
    RenderLock lock(*this);
    connecting_ = false;
    live_ = true;
    lastWhy_.clear();
    failures_ = 0;
    liveSince_ = millis();
    dirty_ = true;
    urgent_ = true;
  }
}

void BambuActivity::dropLink(const std::string& why) {
  link_.stop();
  {
    RenderLock lock(*this);
    live_ = false;
    connecting_ = false;
    lastWhy_ = why;
    ++failures_;
    nextConnectAt_ = millis() + retryDelayMs(failures_);
    if (nextConnectAt_ == 0) nextConnectAt_ = 1;
    dirty_ = true;
    urgent_ = true;
  }
  if (server_) server_->setFacts("status=" + why + "\n");
}

void BambuActivity::pollLink() {
  if (!link_.connected()) return;
  std::string why;
  const bambu::Stage before = working_.stage();
  const int unitsBefore = working_.amsCount;
  const int trayBefore = working_.trayNow;
  const auto result = link_.poll(working_, why);
  if (result == bambu::Link::Poll::Dropped) {
    LOG_ERR("BAMBU", "dropped: %s", why.c_str());
    dropLink(why);
    return;
  }
  if (result != bambu::Link::Poll::Changed) return;
  {
    RenderLock lock(*this);
    state_ = working_;
    dirty_ = true;
    unsaved_ = true;
    if (state_.stage() != before || state_.amsCount != unitsBefore || state_.trayNow != trayBefore) urgent_ = true;
  }
}

void BambuActivity::maybeRepaint() {
  if (view_ != View::Monitor || !dirty_) return;
  const unsigned long since = millis() - lastPaintAt_;
  if (!urgent_ && since < kTickPaintMs) return;
  {
    RenderLock lock(*this);
    dirty_ = false;
    urgent_ = false;
  }
  lastPaintAt_ = millis();
  requestUpdate();
}

// --- The radio -------------------------------------------------------------

// Joins the network the reader last used and remembers whether it was this app
// that brought the radio up, so it puts down only what it picked up.
bool BambuActivity::joinWifi(std::string& why) {
#ifdef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) WiFi.begin();
  if (WiFi.status() != WL_CONNECTED) why = "No Wi-Fi in the simulator.";
  return WiFi.status() == WL_CONNECTED;
#else
  if (WiFi.status() == WL_CONNECTED) return true;
  if (devmode::holdsRadio()) {
    why = "Wi-Fi is busy with Developer Mode. The reader will try again.";
    return false;
  }
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    why = "This reader has no Wi-Fi network saved.";
    return false;
  }
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential.has_value()) {
    why = "The saved Wi-Fi password is gone. Connect again from Settings.";
    return false;
  }
  if (!yieldedDevMode_) {
    devmode::pause();
    yieldedDevMode_ = true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), credential->password.empty() ? nullptr : credential->password.c_str());
  broughtRadioUp_ = true;
  const unsigned long deadline = millis() + kJoinTimeoutMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) {
      // A dozing radio adds its sleep interval to every report's latency.
      WiFi.setSleep(false);
      return true;
    }
    pumpInput();
    if (cancel_) {
      why = "Stopped.";
      return false;
    }
    delay(100);
  }
  why = "Could not join " + ssid + ". The reader will try again.";
  return false;
#endif
}

void BambuActivity::releaseWifi() {
#ifndef SIMULATOR
  if (server_ && server_->isRunning()) return;
  if (broughtRadioUp_) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    broughtRadioUp_ = false;
  }
  if (yieldedDevMode_ && !devPaused_) {
    devmode::resume();
    yieldedDevMode_ = false;
  }
#endif
}

// --- The phone -----------------------------------------------------------

void BambuActivity::startPhone() {
  cancel_ = false;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) {
    std::string why;
    if (!joinWifi(why)) {
      WiFi.mode(WIFI_STA);
      startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                                 showNotice("Setting up on your phone needs Wi-Fi. Nothing changed.");
                                 return;
                               }
                               startPhone();
                             });
      return;
    }
  }
#endif
  // Developer Mode holds port 80 while its toggle is on; it yields while this
  // screen is up, and every way out of here goes through stopPhone().
  if (!devPaused_) {
    devmode::pause();
    devPaused_ = true;
  }
  server_ = makeUniqueNoThrow<BambuServer>();
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
  server_->setFacts(lastWhy_.empty() ? std::string() : "status=" + lastWhy_ + "\n");
  const bool started = server_->begin();
#ifndef SIMULATOR
  if (!started) {
    stopPhone();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#else
  (void)started;
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#endif
  {
    RenderLock lock(*this);
    // The code carries the address, which depends on no service; the name,
    // which does, is only what a person reads.
    phoneUrl_ = "http://" + dotted + "/bambu";
#ifdef SIMULATOR
    (void)mdnsUp;
    phoneReadable_ = phoneUrl_;
#else
    phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/bambu" : phoneUrl_;
#endif
    phoneSaved_ = false;
  }
  setView(View::Phone);
}

void BambuActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  if (devPaused_) {
    devPaused_ = false;
    if (!yieldedDevMode_) devmode::resume();
  }
}

// --- Input ---------------------------------------------------------------

void BambuActivity::loop() {
  if (connectQueued_) {
    runConnect();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Setup:
      case View::Monitor:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Phone:
        stopPhone();
        openHome();
        if (configured_ && !link_.connected()) queueConnect(true);
        return;
      case View::Notice:
        openHome();
        return;
    }
  }

  // Either side key asks the printer for everything again, or reconnects.
  if ((mappedInput.wasReleased(MappedInputManager::Button::Up) ||
       mappedInput.wasReleased(MappedInputManager::Button::Down)) &&
      view_ == View::Monitor) {
    if (link_.connected()) {
      link_.requestPushAll();
    } else {
      queueConnect(true);
    }
    return;
  }

  if (server_ && server_->isRunning()) {
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    if (server_->takeChanged()) {
      {
        RenderLock lock(*this);
        const bambu::Config before = config_;
        loadConfig();
        localFailedAt_ = 0;
        // A different printer is a different state.
        if (before.host != config_.host || before.serial != config_.serial) {
          working_ = bambu::PrinterState{};
          state_ = working_;
          Storage.remove(bambu::store::kSnapshotPath);
        }
        failures_ = 0;
        lastWhy_.clear();
        phoneSaved_ = true;
        interactionsReady_ = false;
      }
      link_.stop();
      live_ = false;
      requestUpdate();
    }
  }

  pollLink();
  if (view_ == View::Monitor && configured_ && !link_.connected() && nextConnectAt_ != 0 &&
      static_cast<long>(millis() - nextConnectAt_) >= 0) {
    nextConnectAt_ = 0;
    queueConnect(false);
    return;
  }
  maybeRepaint();
  if (millis() - lastSavedAt_ >= kSaveEveryMs) saveSnapshot(false);

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  fui::ActionEvent action{};
  {
    RenderLock lock(*this);
    if (!interactionsReady_) return;
    fui::InputSnapshot input{};
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    action = interactions_.route(input);
  }

  switch (action.action) {
    case bambuui::ActionUsePhone:
      startPhone();
      return;
    case bambuui::ActionRefresh:
      if (link_.connected()) {
        link_.requestPushAll();
      } else {
        queueConnect(true);
      }
      return;
    case bambuui::ActionDismiss: {
      const bool wasPhone = view_ == View::Phone;
      stopPhone();
      openHome();
      if (wasPhone && configured_ && !link_.connected()) queueConnect(true);
      return;
    }
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------

std::string BambuActivity::linkText() const {
  if (connecting_) {
    if (tryingCloud_ || !bambu::isComplete(config_)) return "CONNECTING THROUGH BAMBU CLOUD";
    return "CONNECTING TO " + config_.host;
  }
  if (live_) return viaCloud_ ? "LIVE  /  BAMBU CLOUD" : "LIVE  /  " + config_.host;
  return nextConnectAt_ != 0 ? "NOT LIVE  /  TRYING AGAIN SOON" : "NOT LIVE  /  TAP TO TRY AGAIN";
}

std::string BambuActivity::endsAt() const {
  if (state_.remainingMin < 0) return "";
  const time_t now = time(nullptr);
  // Before the clock is set the reader thinks it is 1970.
  if (now < 1700000000) return "";
  const time_t end = now + static_cast<time_t>(state_.remainingMin) * 60;
  struct tm local{};
  localtime_r(&end, &local);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "ENDS %02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

void BambuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Setup:
      bambuui::buildSetup(screen);
      break;
    case View::Monitor: {
      const std::string link = linkText();
      const std::string ends = endsAt();
      bambuui::MonitorModel model;
      model.state = &state_;
      model.link = link.c_str();
      model.live = live_;
      model.endsAt = ends.c_str();
      model.reason = lastWhy_.c_str();
      const bambuui::Hero hero = bambuui::buildMonitor(screen, model);
      // The one big number gets a cut of its own; three slots are a working set.
      target.setFont(fui::GfxRendererTarget::FONT_TITLE, kHeroFont);
      bambuui::drawHero(screen, hero);
      break;
    }
    case View::Phone: {
      bambuui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.saved = phoneSaved_;
      const fui::Rect qr = bambuui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice:
      bambuui::buildNotice(screen, notice_.c_str());
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Bambu");
  ++paints_;
  renderer.displayBuffer(paints_ % kCleanEvery == 1 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
