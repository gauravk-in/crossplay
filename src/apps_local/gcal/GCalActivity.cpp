#include "GCalActivity.h"

#include <ESPmDNS.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "../../CrossPointSettings.h"
#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../WifiCredentialStore.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../gtasks/GTasksScreens.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;

constexpr const char* kTag = "GCAL";

// Below this the clock has never been set: "today" would be in 1970.
constexpr int64_t kClockFloor = 1700000000;

// How long a poll waits for the saved network before trying again next time.
constexpr uint32_t kJoinTimeoutMs = 15000;

// How long the sign-in page stays up after it worked, so the phone's next
// status poll hears "done" instead of a dead address.
constexpr uint32_t kDoneLingerMs = 3000;

constexpr const char* kTitle = "CALENDAR";
constexpr const char* kIntro =
    "Your Google Calendar, on this reader. Start here, then sign in with Google on your phone, on the same Wi-Fi. "
    "It is the same sign-in as Tasks, so you do it once for both.";

std::string randomHex(const size_t bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t b = static_cast<uint8_t>(esp_random());
    out += kHex[b >> 4];
    out += kHex[b & 0x0f];
  }
  return out;
}

}  // namespace

std::unique_ptr<Activity> GCalActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<GCalActivity>(renderer, mappedInput);
}

// --- Lifecycle -------------------------------------------------------------

void GCalActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  settings_ = library_.loadSettings();
  meta_ = library_.loadMeta();
  events_ = library_.loadEvents();
  wasCharging_ = gpio.isUsbConnected();
  client_ = account_.loadClient();
  reloadCredentials();
  rebuild(-1);
  LOG_INF(kTag, "opened: %d events on the card, %s, poll %u min", static_cast<int>(events_.size()),
          creds_.complete() ? "signed in" : "not signed in", static_cast<unsigned>(settings_.pollMinutes));
  requestUpdate();
}

void GCalActivity::onExit() {
  stopPhone();
  Activity::onExit();
  // A radio this app brought up comes down with it, and never one Developer
  // Mode holds.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    releaseWifi();
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  } else {
    releaseWifi();
  }
}

bool GCalActivity::polling() const { return creds_.complete() && settings_.pollMinutes > 0; }

bool GCalActivity::preventAutoSleep() {
  if (server_ && server_->isRunning()) return true;
  return polling() && gpio.isUsbConnected();
}

void GCalActivity::reloadCredentials() {
  creds_ = account_.loadCredentials();
  token_ = gtasks::AccessToken{};
  if (creds_.complete()) {
    signInReason_.clear();
    phase_ = Phase::Schedule;
  } else {
    phase_ = Phase::SignIn;
  }
}

// --- The schedule ------------------------------------------------------------

int64_t GCalActivity::currentDay() const {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  if (now > kClockFloor) return gcal::toLocal(now).day;
  return -1;
}

void GCalActivity::rebuild(const int64_t keepDay) {
  int64_t today = currentDay();
  int64_t firstDay = 0;
  int64_t lastDay = 0;
  if (today >= 0) {
    firstDay = today - gcal::kDaysBack;
    lastDay = today + gcal::kDaysAhead;
  } else if (!events_.empty()) {
    // No clock: draw whatever the card holds, from its first event.
    firstDay = events_.front().allDay ? events_.front().start : gcal::toLocal(events_.front().start).day;
    lastDay = firstDay + gcal::kDaysBack + gcal::kDaysAhead;
  }
  std::vector<gcal::Item> items = gcal::buildSchedule(events_, firstDay, lastDay, today);
  RenderLock lock(*this);
  items_ = std::move(items);
  builtFor_ = today;
  todayIndex_ = today >= 0 ? gcal::indexOfDay(items_, today) : 0;
  if (todayIndex_ >= static_cast<int>(items_.size()))
    todayIndex_ = items_.empty() ? 0 : static_cast<int>(items_.size()) - 1;
  if (keepDay >= 0) {
    first_ = gcal::indexOfDay(items_, keepDay);
    if (first_ >= static_cast<int>(items_.size())) first_ = todayIndex_;
  } else {
    first_ = todayIndex_;
  }
  measured_ = false;
}

void GCalActivity::stepPage(const int delta) {
  if (!measured_ || items_.empty()) return;
  int next = first_;
  if (delta > 0) {
    const int shown = gcal::fitFrom(heights_, first_, pageHeight_);
    if (first_ + shown >= static_cast<int>(items_.size())) return;
    next = first_ + shown;
  } else {
    if (first_ <= 0) return;
    next = gcal::pageBefore(heights_, first_, pageHeight_);
  }
  {
    RenderLock lock(*this);
    first_ = next;
  }
  requestUpdate();
}

void GCalActivity::goToday() {
  if (builtFor_ != currentDay()) rebuild(-1);
  {
    RenderLock lock(*this);
    first_ = todayIndex_;
  }
  requestUpdate();
}

// --- Screens ---------------------------------------------------------------

void GCalActivity::show(const Phase phase) {
  {
    RenderLock lock(*this);
    phase_ = phase;
  }
  requestUpdate();
}

void GCalActivity::showNotice(const char* headline, std::string message) {
  {
    RenderLock lock(*this);
    noticeHeadline_ = headline;
    noticeMessage_ = std::move(message);
    phase_ = Phase::Notice;
  }
  requestUpdate();
}

void GCalActivity::paintBusyNow(const char* headline) {
  {
    RenderLock lock(*this);
    busyHeadline_ = headline;
    phase_ = Phase::Busy;
    step_ = Step::None;
  }
  requestUpdate(true);
}

// --- The radio -------------------------------------------------------------

#if defined(FREEINK_NET_WOLFSSL)
// Tasks' join: Developer Mode keeps the radio, a connection somebody else
// brought up is used and left alone, and only the saved network is tried.
bool GCalActivity::joinWifi(std::string& message) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (devmode::holdsRadio()) {
    message = "Wi-Fi is busy with Developer Mode.";
    return false;
  }
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    message = "This reader has no Wi-Fi network saved.";
    return false;
  }
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential.has_value()) {
    message = "The saved Wi-Fi password is gone. Connect again from Settings.";
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
      LOG_INF(kTag, "joined '%s'", ssid.c_str());
      return true;
    }
    delay(100);
  }
  LOG_ERR(kTag, "could not join '%s'", ssid.c_str());
  message = "Could not reach Wi-Fi.";
  return false;
}

void GCalActivity::releaseWifi() {
  if (broughtRadioUp_) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    broughtRadioUp_ = false;
  }
  if (yieldedDevMode_) {
    devmode::resume();
    yieldedDevMode_ = false;
  }
}
#else
bool GCalActivity::joinWifi(std::string&) { return true; }
void GCalActivity::releaseWifi() {
  if (yieldedDevMode_) {
    devmode::resume();
    yieldedDevMode_ = false;
  }
}
#endif

// --- Syncing ---------------------------------------------------------------

bool GCalActivity::ensureToken(std::string& message) {
  if (token_.usable(millis())) return true;
  if (auth_.refresh(client_, creds_, millis(), token_, message)) return true;
  if (auth_.signedOut) {
    RenderLock lock(*this);
    signInReason_ = message;
    phase_ = Phase::SignIn;
  }
  return false;
}

bool GCalActivity::sync(std::string& message, bool& changed) {
  changed = false;
  if (!ensureToken(message)) return false;

  bool retried = false;
  const auto retry = [this, &retried, &message]() {
    if (!api_.tokenRefused || retried) return false;
    retried = true;
    token_ = gtasks::AccessToken{};
    return ensureToken(message);
  };
  const auto consent = [this, &message]() {
    if (!api_.needsConsent) return;
    RenderLock lock(*this);
    signInReason_ = message;
    phase_ = Phase::SignIn;
  };

  std::vector<gcal::Calendar> calendars;
  bool ok = api_.calendars(token_, calendars, message);
  if (!ok && retry()) ok = api_.calendars(token_, calendars, message);
  if (!ok) {
    consent();
    return false;
  }

  // The window is counted from the reader's own today when it knows the date,
  // else from now as Google sees it.
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  const int64_t base = now > kClockFloor ? now : 0;
  const int64_t timeMin = base > 0 ? base - static_cast<int64_t>(gcal::kDaysBack + 1) * 86400 : 0;
  const int64_t timeMax = base > 0 ? base + static_cast<int64_t>(gcal::kDaysAhead + 1) * 86400 : 4102444800LL;

  std::vector<gcal::Event> fresh;
  fresh.reserve(events_.size() + 16);
  for (const gcal::Calendar& cal : calendars) {
    const size_t before = fresh.size();
    ok = api_.events(token_, cal.id, timeMin, timeMax, fresh, message);
    if (!ok && retry()) {
      fresh.resize(before);
      ok = api_.events(token_, cal.id, timeMin, timeMax, fresh, message);
    }
    if (!ok) {
      consent();
      return false;
    }
  }
  gcal::sortEvents(fresh);

  const std::string before = gcal::serializeEvents(events_);
  const std::string after = gcal::serializeEvents(fresh);
  changed = before != after;
  if (changed) library_.saveEvents(fresh);

  // Keep the page where it was: on the day at its top.
  const int64_t keepDay = first_ < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(first_)].day : -1;
  {
    RenderLock lock(*this);
    events_ = std::move(fresh);
  }
  if (changed || builtFor_ != currentDay()) rebuild(keepDay);
  meta_.lastSyncAt = now > kClockFloor ? now : 0;
  meta_.calendars = static_cast<int>(calendars.size());
  library_.saveMeta(meta_);
  LOG_INF(kTag, "synced %d calendars, %d events%s", meta_.calendars, static_cast<int>(events_.size()),
          changed ? "" : " (unchanged)");
  return true;
}

void GCalActivity::requestStep(const Step step, const char* headline) {
  {
    RenderLock lock(*this);
    busyHeadline_ = headline;
    phase_ = Phase::Busy;
    step_ = step;
  }
  requestUpdate();
}

void GCalActivity::onWifiChosen(const bool connected) {
  const Step step = afterWifi_;
  afterWifi_ = Step::None;
  if (!connected) {
    show(creds_.complete() ? Phase::Schedule : Phase::SignIn);
    return;
  }
  requestStep(step, step == Step::SignIn ? "STARTING" : "SYNCING");
}

void GCalActivity::runStep(const Step step) {
  std::string message;
  if (!joinWifi(message)) {
    afterWifi_ = step;
    WiFi.mode(WIFI_STA);
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
    return;
  }
  if (step == Step::SignIn) {
    startPhone();
    return;
  }
  lastAttemptMs_ = millis();
  everAttempted_ = true;
  bool changed = false;
  if (sync(message, changed)) {
    lastPollFailed_ = false;
    show(Phase::Schedule);
  } else if (phase_ == Phase::SignIn) {
    requestUpdate();
  } else {
    lastPollFailed_ = true;
    showNotice("NOT SYNCED", message);
  }
}

void GCalActivity::backgroundPoll() {
  lastAttemptMs_ = millis();
  everAttempted_ = true;
  std::string message;
  bool changed = false;
  const bool ok = joinWifi(message) && sync(message, changed);
  if (!ok) LOG_ERR(kTag, "poll: %s", message.c_str());
  const bool flipped = lastPollFailed_ != !ok;
  lastPollFailed_ = !ok;
  if (changed || flipped || phase_ == Phase::SignIn) requestUpdate();
}

// --- Signing in ------------------------------------------------------------

void GCalActivity::startSignIn() {
  client_ = account_.loadClient();
  if (!client_.complete()) {
    showNotice("NO GOOGLE CLIENT",
               "This reader has no Google sign-in client. Put client_id= and client_secret= lines in "
               "/.crosspoint/gtasks/client.cfg on the card. docs/apps/gtasks.md says how to make one.");
    return;
  }
  requestStep(Step::SignIn, "STARTING");
}

void GCalActivity::startPhone() {
  if (!yieldedDevMode_) {
    devmode::pause();
    yieldedDevMode_ = true;
  }
#ifdef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin("simulator");
  }
#endif
  // Tasks' page: it is the same Google sign-in, for both apps.
  server_ = makeUniqueNoThrow<CrossPointWebServer>(CrossPointWebServer::Surface::TasksOnly);
  if (!server_) {
    showNotice("NOT STARTED", "There was not enough memory to start the sign-in page.");
    return;
  }
  verifier_ = randomHex(32);
  state_ = randomHex(12);
  server_->setTasksLink(gtasks::authUrl(client_.id, gtasks::pkceChallenge(verifier_), state_));
  server_->begin();
#ifndef SIMULATOR
  if (!server_->isRunning()) {
    stopPhone();
    showNotice("NOT STARTED", "The reader could not open its sign-in page. Try again in a moment.");
    return;
  }
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  phoneUrl_ = std::string("http://") + WiFi.localIP().toString().c_str() + "/t";
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/t" : phoneUrl_;
#else
  phoneUrl_ = "http://127.0.0.1/t";
  phoneReadable_ = phoneUrl_;
#endif
  LOG_INF(kTag, "sign-in page up at %s", phoneUrl_.c_str());
  show(Phase::Phone);
}

void GCalActivity::stopPhone() {
  if (!server_) return;
  server_->stop();
  server_.reset();
#ifndef SIMULATOR
  MDNS.end();
#endif
  verifier_.clear();
  state_.clear();
}

void GCalActivity::takePaste(const std::string& pasted) {
  const gtasks::Pasted parsed = gtasks::parsePasted(pasted, state_);
  if (!parsed.ok()) {
    server_->setTasksStatus("error", parsed.message);
    return;
  }
  paintBusyNow("SIGNING IN");
  gtasks::Credentials creds;
  std::string message;
  if (!auth_.exchange(client_, parsed.code, verifier_, creds, message)) {
    server_->setTasksStatus("error", message);
    show(Phase::Phone);
    return;
  }
  if (!account_.saveCredentials(creds)) {
    server_->setTasksStatus("error", "The reader's card would not take the sign-in. Check it is not full or locked.");
    show(Phase::Phone);
    return;
  }
  server_->setTasksStatus("done", "Signed in as " +
                                      (creds.account.empty() ? std::string("your account") : creds.account) +
                                      ". The reader is fetching your calendar. You can close this page.");
  const uint32_t until = millis() + kDoneLingerMs;
  while (static_cast<int32_t>(until - millis()) > 0 && server_->isRunning()) {
    server_->handleClient();
    delay(10);
  }
  stopPhone();
  reloadCredentials();
  everAttempted_ = false;
  LOG_INF(kTag, "signed in");
  requestStep(Step::Sync, "SYNCING");
}

// --- The sleep screen ------------------------------------------------------

namespace {
uint8_t modeToRestore(const int previousMode) {
  if (previousMode >= 0 && previousMode < CrossPointSettings::SLEEP_SCREEN_MODE_COUNT &&
      previousMode != CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR) {
    return static_cast<uint8_t>(previousMode);
  }
  return static_cast<uint8_t>(CrossPointSettings::SLEEP_SCREEN_MODE::DARK);
}
}  // namespace

bool GCalActivity::asleepOn() const { return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR; }

void GCalActivity::restoreSleepSettings(const gcal::Asleep& asleep) {
  if (!asleepOn()) return;
  SETTINGS.sleepScreen = modeToRestore(asleep.previousMode);
  if (asleep.previousQuick == 1) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  } else if (asleep.previousQuick == 0) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
  }
  SETTINGS.saveToFile();
}

void GCalActivity::toggleAsleep() {
  if (asleepOn()) {
    gcal::Asleep previous;
    if (!library_.loadAsleep(previous)) previous = gcal::Asleep{};
    restoreSleepSettings(previous);
    library_.clearAsleep();
    LOG_INF(kTag, "calendar off the sleep screen; mode back to %d", SETTINGS.sleepScreen);
    requestUpdate();
    return;
  }
  gcal::Asleep choice;
  choice.previousMode = static_cast<int>(SETTINGS.sleepScreen);
  choice.previousQuick = static_cast<int>(SETTINGS.quickResumeSleepScreen);
  if (!library_.saveAsleep(choice)) {
    showNotice("NOT SAVED", "The card would not take the change. Nothing was changed.");
    return;
  }
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR;
  // Quick resume on an idle sleep skips the sleep screen entirely, so the
  // schedule would never appear on the ordinary sleep. Tasks makes the same
  // trade.
  SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
  SETTINGS.saveToFile();
  LOG_INF(kTag, "calendar on the sleep screen (replaced mode %d)", choice.previousMode);
  requestUpdate();
}

void GCalActivity::signOut() {
  if (!creds_.refreshToken.empty() && WiFi.status() == WL_CONNECTED) {
    paintBusyNow("SIGNING OUT");
    auth_.revoke(creds_.refreshToken);
  }
  gcal::Asleep asleep;
  if (library_.loadAsleep(asleep)) restoreSleepSettings(asleep);
  library_.clearAsleep();
  library_.forget();
  account_.forgetToken();
  {
    RenderLock lock(*this);
    events_.clear();
    meta_ = gcal::Meta{};
  }
  rebuild(-1);
  token_ = gtasks::AccessToken{};
  creds_ = gtasks::Credentials{};
  signInReason_.clear();
  LOG_INF(kTag, "signed out; token and calendar removed from the card");
  show(Phase::SignIn);
}

// --- The loop --------------------------------------------------------------

void GCalActivity::loop() {
  if (step_ != Step::None && phase_ == Phase::Busy) {
    const Step step = step_;
    step_ = Step::None;
    runStep(step);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (phase_) {
      case Phase::Schedule:
      case Phase::SignIn:
        shelf::leave(renderer, mappedInput);
        break;
      case Phase::SignOutConfirm:
        show(Phase::Settings);
        break;
      case Phase::Phone:
        stopPhone();
        show(Phase::SignIn);
        break;
      case Phase::Settings:
      case Phase::Notice:
      case Phase::Busy:
        show(creds_.complete() ? Phase::Schedule : Phase::SignIn);
        break;
    }
    return;
  }

  const bool charging = gpio.isUsbConnected();
  if (charging != wasCharging_) {
    wasCharging_ = charging;
    if (!charging) releaseWifi();
    requestUpdate();
  }

  if (server_ && server_->isRunning()) {
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    std::string pasted;
    if (server_->takeTasksPaste(pasted)) {
      takePaste(pasted);
      return;
    }
  }

  // Midnight passed with the app open: today moved, so the disc moves too.
  if (phase_ == Phase::Schedule && builtFor_ >= 0 && currentDay() != builtFor_) {
    const int64_t keepDay = first_ < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(first_)].day : -1;
    rebuild(keepDay);
    requestUpdate();
  }

  if (phase_ == Phase::Schedule && polling() &&
      gtasks::pollDue(charging, settings_.pollMinutes, everAttempted_, millis(), lastAttemptMs_)) {
    backgroundPoll();
    return;
  }

  // The two side keys page the schedule, as they page lists everywhere else.
  if (phase_ == Phase::Schedule) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      stepPage(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      stepPage(-1);
      return;
    }
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;
  if (everShown_ && millis() - phaseShownAtMs_ < kSettleMs) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);

  switch (event.action) {
    case gcalui::ActionRefresh:
      requestStep(Step::Sync, "SYNCING");
      break;
    case gcalui::ActionToday:
      goToday();
      break;
    case gcalui::ActionPagePrev:
      stepPage(-1);
      break;
    case gcalui::ActionPageNext:
      stepPage(1);
      break;
    case gcalui::ActionSettings:
      show(Phase::Settings);
      break;
    case gcalui::ActionSettingRow:
      if (event.value == static_cast<int>(gcalui::SettingRow::Poll)) {
        settings_.pollMinutes = gtasks::nextPollMinutes(settings_.pollMinutes);
        library_.saveSettings(settings_);
        lastAttemptMs_ = millis();
        everAttempted_ = true;
        requestUpdate();
      } else if (event.value == static_cast<int>(gcalui::SettingRow::Sleep)) {
        toggleAsleep();
      } else if (event.value == static_cast<int>(gcalui::SettingRow::SignOut)) {
        show(Phase::SignOutConfirm);
      }
      break;
    case gcalui::ActionCloseSettings:
    case gtasksui::ActionNotice:
      show(creds_.complete() ? Phase::Schedule : Phase::SignIn);
      break;
    case gcalui::ActionKeepSignedIn:
      show(Phase::Settings);
      break;
    case gcalui::ActionSignOut:
      signOut();
      break;
    case gtasksui::ActionStartSignIn:
      startSignIn();
      break;
    case gtasksui::ActionCancelSignIn:
      stopPhone();
      show(Phase::SignIn);
      break;
    default:
      break;
  }
}

// --- Rendering -------------------------------------------------------------

void GCalActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);
  const char* what = "Calendar";

  switch (phase_) {
    case Phase::Busy: {
      gtasksui::NoticeModel model;
      model.title = kTitle;
      model.headline = busyHeadline_.c_str();
      if (busyHeadline_ == "SYNCING") model.message = "Reading your calendars.";
      gtasksui::buildNotice(screen, model);
      what = "Calendar busy";
      break;
    }
    case Phase::Notice: {
      gtasksui::NoticeModel model;
      model.title = kTitle;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.actionLabel = creds_.complete() ? "BACK TO THE CALENDAR" : "BACK";
      gtasksui::buildNotice(screen, model);
      what = "Calendar notice";
      break;
    }
    case Phase::SignIn:
      gtasksui::buildSignIn(screen, signInReason_.c_str(), kTitle, kIntro);
      what = "Calendar sign in";
      break;
    case Phase::Phone: {
      const fui::Rect qr = gtasksui::buildPhone(screen, phoneReadable_.c_str());
      if (qr.width > 0) QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      what = "Calendar phone";
      break;
    }
    case Phase::Settings: {
      const std::string poll = gtasks::pollLabel(settings_.pollMinutes);
      gcalui::SettingsModel model;
      model.pollLabel = poll.c_str();
      model.sleepLabel = asleepOn() ? "ON" : "OFF";
      model.account = creds_.account.c_str();
      model.signedIn = creds_.complete();
      gcalui::buildSettings(screen, model);
      what = "Calendar settings";
      break;
    }
    case Phase::SignOutConfirm:
      gcalui::buildSignOutConfirm(screen);
      what = "Calendar sign out";
      break;
    case Phase::Schedule: {
      if (!measured_) {
        heights_ = gcalui::itemHeights(target, items_);
        pageHeight_ = gcalui::pageHeight(device, false);
        measured_ = true;
      }
      const int count = static_cast<int>(items_.size());
      if (first_ >= count) first_ = count > 0 ? count - 1 : 0;
      shown_ = gcal::fitFrom(heights_, first_, pageHeight_);

      if (lastPollFailed_) {
        std::snprintf(status_, sizeof(status_), "OFFLINE");
      } else if (polling() && gpio.isUsbConnected()) {
        std::snprintf(status_, sizeof(status_), "AUTO SYNC");
      } else if (meta_.lastSyncAt > 0) {
        const time_t when = static_cast<time_t>(meta_.lastSyncAt);
        struct tm parts{};
        localtime_r(&when, &parts);
        std::snprintf(status_, sizeof(status_), "%02d:%02d", parts.tm_hour, parts.tm_min);
      } else {
        status_[0] = '\0';
      }

      std::string title = kTitle;
      if (first_ < count) {
        // The month of the first DAY on the page, past a banner at its top.
        int index = first_;
        if (items_[static_cast<size_t>(index)].kind == gcal::Item::Kind::Month && index + 1 < count) ++index;
        title = gcal::monthTitle(items_[static_cast<size_t>(index)].day);
      }

      gcalui::ScheduleModel model;
      model.title = title.c_str();
      model.status = status_[0] != '\0' ? status_ : nullptr;
      model.items = shown_ > 0 ? items_.data() + first_ : nullptr;
      model.heights = shown_ > 0 ? heights_.data() + first_ : nullptr;
      model.count = shown_;
      model.events = events_.empty() ? nullptr : events_.data();
      model.eventCount = static_cast<int>(events_.size());
      model.today = builtFor_;
      model.canPagePrev = first_ > 0;
      model.canPageNext = first_ + shown_ < count;
      model.canGoToday = !(todayIndex_ >= first_ && todayIndex_ < first_ + shown_);
      model.settingsIcon = &icon_go_settings_32;
      if (meta_.lastSyncAt == 0 && !everAttempted_) {
        model.emptyHeadline = "NOT SYNCED YET";
        model.emptyMessage = "Tap REFRESH to fetch your Google Calendar.";
      }
      gcalui::buildSchedule(screen, model);
      what = "Calendar schedule";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);
  const bool phaseChanged = !everShown_ || phase_ != lastShownPhase_;
  const bool paging = phase_ == Phase::Schedule;
  const auto labels = mappedInput.mapLabels("Back", "", paging ? "Up" : "", paging ? "Down" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  if (phaseChanged) {
    lastShownPhase_ = phase_;
    phaseShownAtMs_ = millis();
    everShown_ = true;
  }
}
