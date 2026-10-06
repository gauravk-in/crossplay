#include "GTasksActivity.h"

#include <ESPmDNS.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../WifiCredentialStore.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;

constexpr const char* kTag = "GTASKS";

// Below this the clock has never been set, and a time on the band would be a
// time in 1970.
constexpr int64_t kClockFloor = 1700000000;

// How long a poll waits for the saved network. A background job that cannot
// join in this long tries again at the next interval rather than holding the
// loop: nobody asked for this one.
constexpr uint32_t kJoinTimeoutMs = 15000;

// How long the sign-in page stays up after it worked, so the phone's next
// status poll hears "done" instead of a dead address.
constexpr uint32_t kDoneLingerMs = 3000;

// `bytes` of the hardware RNG as lowercase hex: PKCE's verifier (RFC 7636
// allows [A-Za-z0-9-._~], 43 to 128 of them) and the sign-in's state.
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

std::unique_ptr<Activity> GTasksActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<GTasksActivity>(renderer, mappedInput);
}

// --- Lifecycle -------------------------------------------------------------

void GTasksActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  settings_ = library_.loadSettings();
  meta_ = library_.loadMeta();
  tasks_ = library_.loadTasks();
  wasCharging_ = gpio.isUsbConnected();
  client_ = library_.loadClient();
  reloadCredentials();
  LOG_INF(kTag, "opened: %d tasks, %d to send, %s, poll %u min", static_cast<int>(tasks_.size()),
          gtasks::pendingCount(tasks_), creds_.complete() ? "signed in" : "not signed in",
          static_cast<unsigned>(settings_.pollMinutes));
  requestUpdate();
}

void GTasksActivity::onExit() {
  stopPhone();
  Activity::onExit();
  // The Instapaper rule: a radio this app brought up comes down with it, and
  // never one Developer Mode holds.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    releaseWifi();
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  } else {
    releaseWifi();
  }
}

bool GTasksActivity::polling() const { return creds_.complete() && settings_.pollMinutes > 0; }

bool GTasksActivity::preventAutoSleep() {
  // A QR on the glass is someone signing in on a phone; sleeping would take
  // the page away mid-way.
  if (server_ && server_->isRunning()) return true;
  return polling() && gpio.isUsbConnected();
}

void GTasksActivity::reloadCredentials() {
  creds_ = library_.loadCredentials();
  token_ = gtasks::AccessToken{};
  if (creds_.complete()) {
    signInReason_.clear();
    phase_ = Phase::List;
  } else {
    phase_ = Phase::SignIn;
  }
}

// --- Screens ---------------------------------------------------------------

void GTasksActivity::show(const Phase phase) {
  {
    RenderLock lock(*this);
    phase_ = phase;
  }
  requestUpdate();
}

void GTasksActivity::showNotice(const char* headline, std::string message) {
  {
    RenderLock lock(*this);
    noticeHeadline_ = headline;
    noticeMessage_ = std::move(message);
    phase_ = Phase::Notice;
  }
  requestUpdate();
}

void GTasksActivity::paintBusyNow(const char* headline) {
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
// The Live engine's join, for the same reason: a background job loses every
// argument about the radio. Developer Mode keeps it, a connection somebody
// else brought up is used and left alone, and only the saved network is tried.
bool GTasksActivity::joinWifi(std::string& message) {
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

void GTasksActivity::releaseWifi() {
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
// The simulator has the laptop's network and no radio to own. The sign-in
// page still yields Developer Mode, so that is still given back.
bool GTasksActivity::joinWifi(std::string&) { return true; }
void GTasksActivity::releaseWifi() {
  if (yieldedDevMode_) {
    devmode::resume();
    yieldedDevMode_ = false;
  }
}
#endif

// --- Syncing ---------------------------------------------------------------

bool GTasksActivity::ensureToken(std::string& message) {
  if (token_.usable(millis())) return true;
  if (api_.refresh(client_, creds_, millis(), token_, message)) return true;
  if (api_.signedOut) {
    RenderLock lock(*this);
    signInReason_ = message;
    phase_ = Phase::SignIn;
  }
  return false;
}

bool GTasksActivity::sync(std::string& message, bool& changed) {
  changed = false;
  if (!ensureToken(message)) return false;

  const std::string before = gtasks::serializeTasks(tasks_);
  const std::string titleBefore = meta_.listTitle;
  // Worked on as a copy and swapped in under the render lock, because the
  // render task reads tasks_ and a charger poll can overlap a repaint.
  std::vector<gtasks::Task> work = tasks_;
  const auto commit = [this, &work]() {
    RenderLock lock(*this);
    tasks_ = work;
  };

  // Ticks first, so the list read afterwards already leaves them out. One
  // refresh-and-retry per sync if Google refuses the key mid-way; a second
  // refusal is a real one.
  bool retried = false;
  int sent = 0;
  for (gtasks::Task& t : work) {
    if (!t.pending) continue;
    std::string why;
    bool ok = api_.complete(token_, t.id, why);
    if (!ok && api_.tokenRefused && !retried) {
      retried = true;
      token_ = gtasks::AccessToken{};
      if (!ensureToken(message)) return false;
      ok = api_.complete(token_, t.id, why);
    }
    if (!ok) {
      // Stays pending; the list below still merges, so new tasks arrive even
      // when one tick will not go up.
      LOG_ERR(kTag, "tick did not go up: %s", why.c_str());
      continue;
    }
    t.pending = false;
    t.id.clear();  // completed: gone from the list Google is about to send
    ++sent;
  }
  if (sent > 0) {
    work.erase(std::remove_if(work.begin(), work.end(), [](const gtasks::Task& t) { return t.id.empty(); }),
               work.end());
    library_.saveTasks(work);
    commit();
    LOG_INF(kTag, "sent %d tick%s", sent, sent == 1 ? "" : "s");
  }

  std::vector<gtasks::Task> fresh;
  bool ok = api_.openTasks(token_, fresh, message);
  if (!ok && api_.tokenRefused && !retried) {
    token_ = gtasks::AccessToken{};
    if (!ensureToken(message)) return false;
    ok = api_.openTasks(token_, fresh, message);
  }
  if (!ok) return false;

  std::string title;
  std::string ignored;
  if (api_.listTitle(token_, title, ignored) && !title.empty()) {
    RenderLock lock(*this);
    meta_.listTitle = title;
  }

  work = gtasks::merge(work, fresh);
  library_.saveTasks(work);
  commit();
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  meta_.lastSyncAt = now > kClockFloor ? now : 0;
  library_.saveMeta(meta_);

  changed = gtasks::serializeTasks(tasks_) != before || meta_.listTitle != titleBefore;
  return true;
}

void GTasksActivity::requestStep(const Step step, const char* headline) {
  // The busy screen goes up first; the work runs on the next pass, once the
  // panel is already saying what is happening.
  {
    RenderLock lock(*this);
    busyHeadline_ = headline;
    phase_ = Phase::Busy;
    step_ = step;
  }
  requestUpdate();
}

void GTasksActivity::requestRefresh() { requestStep(Step::Sync, "SYNCING"); }

void GTasksActivity::onWifiChosen(const bool connected) {
  const Step step = afterWifi_;
  afterWifi_ = Step::None;
  if (!connected) {
    // They backed out of the picker. The list still works.
    show(creds_.complete() ? Phase::List : Phase::SignIn);
    return;
  }
  requestStep(step, step == Step::SignIn ? "STARTING" : "SYNCING");
}

void GTasksActivity::runStep(const Step step) {
  std::string message;
  if (!joinWifi(message)) {
    // No saved network, or it would not answer: hand over to the picker
    // rather than failing, which is what REFRESH promises to do.
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
    page_ = 0;
    show(Phase::List);
  } else if (phase_ == Phase::SignIn) {
    requestUpdate();
  } else {
    lastPollFailed_ = true;
    showNotice("NOT SYNCED", message);
  }
}

void GTasksActivity::backgroundPoll() {
  lastAttemptMs_ = millis();
  everAttempted_ = true;
  std::string message;
  bool changed = false;
  bool ok = joinWifi(message) && sync(message, changed);
  if (!ok) LOG_ERR(kTag, "poll: %s", message.c_str());
  // The band's OFFLINE is the only thing a failed poll says. Repainted when
  // that flips, when the list changed, or when Google signed the reader out --
  // and not otherwise, so a quiet list is a still panel.
  const bool flipped = lastPollFailed_ != !ok;
  lastPollFailed_ = !ok;
  if (changed || flipped || phase_ == Phase::SignIn) requestUpdate();
}

// --- Signing in ------------------------------------------------------------

void GTasksActivity::startSignIn() {
  client_ = library_.loadClient();
  if (!client_.complete()) {
    showNotice("NO GOOGLE CLIENT",
               "This reader has no Google sign-in client. Put client_id= and client_secret= lines in "
               "/.crosspoint/gtasks/client.cfg on the card. docs/apps/gtasks.md says how to make one.");
    return;
  }
  requestStep(Step::SignIn, "STARTING");
}

void GTasksActivity::startPhone() {
  // Developer Mode holds port 80 while its toggle is on. joinWifi() may have
  // paused it already; if the radio was up anyway, pause it here, so the one
  // release in releaseWifi() covers both.
  if (!yieldedDevMode_) {
    devmode::pause();
    yieldedDevMode_ = true;
  }
#ifdef SIMULATOR
  // The simulator's Wi-Fi joins whatever it is asked to, and then the page
  // really serves on the host (port 8080), so a sign-in can run end to end.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin("simulator");
  }
#endif
  server_ = makeUniqueNoThrow<CrossPointWebServer>(CrossPointWebServer::Surface::TasksOnly);
  if (!server_) {
    showNotice("NOT STARTED", "There was not enough memory to start the sign-in page.");
    return;
  }
  // Fresh for every attempt: an address pasted from an earlier one names a
  // different state and is refused.
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
  // The QR carries the address, never the name: see NotesActivity::startPhone.
  phoneUrl_ = std::string("http://") + WiFi.localIP().toString().c_str() + "/t";
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/t" : phoneUrl_;
#else
  phoneUrl_ = "http://127.0.0.1/t";
  phoneReadable_ = phoneUrl_;
#endif
  LOG_INF(kTag, "sign-in page up at %s", phoneUrl_.c_str());
  show(Phase::Phone);
}

void GTasksActivity::stopPhone() {
  if (!server_) return;
  server_->stop();
  server_.reset();
#ifndef SIMULATOR
  MDNS.end();
#endif
  verifier_.clear();
  state_.clear();
}

void GTasksActivity::takePaste(const std::string& pasted) {
  const gtasks::Pasted parsed = gtasks::parsePasted(pasted, state_);
  if (!parsed.ok()) {
    server_->setTasksStatus("error", parsed.message);
    return;
  }
  paintBusyNow("SIGNING IN");
  gtasks::Credentials creds;
  std::string message;
  if (!api_.exchange(client_, parsed.code, verifier_, creds, message)) {
    server_->setTasksStatus("error", message);
    show(Phase::Phone);
    return;
  }
  if (!library_.saveCredentials(creds)) {
    server_->setTasksStatus("error", "The reader's card would not take the sign-in. Check it is not full or locked.");
    show(Phase::Phone);
    return;
  }
  server_->setTasksStatus("done", "Signed in as " +
                                      (creds.account.empty() ? std::string("your account") : creds.account) +
                                      ". The reader is fetching your list. You can close this page.");
  const uint32_t until = millis() + kDoneLingerMs;
  while (static_cast<int32_t>(until - millis()) > 0 && server_->isRunning()) {
    server_->handleClient();
    delay(10);
  }
  stopPhone();
  reloadCredentials();
  everAttempted_ = false;
  LOG_INF(kTag, "signed in");
  requestRefresh();
}

// --- Ticks and pages -------------------------------------------------------

void GTasksActivity::toggle(const int index) {
  if (index < 0 || index >= static_cast<int>(tasks_.size())) return;
  gtasks::Task& t = tasks_[static_cast<size_t>(index)];
  // A tick that has not gone up can be taken back for free: Google never knew.
  {
    RenderLock lock(*this);
    t.pending = !t.pending;
  }
  library_.saveTasks(tasks_);
  LOG_INF(kTag, "%s a task; %d to send", t.pending ? "ticked" : "unticked", gtasks::pendingCount(tasks_));
  requestUpdate();
}

void GTasksActivity::stepPage(const int delta) {
  const int count = static_cast<int>(tasks_.size());
  const int pages = perPage_ > 0 ? (count + perPage_ - 1) / perPage_ : 1;
  const int next = page_ + delta;
  // Clamped, never wrapped: a wrap lands on a page that looks like any other.
  if (next < 0 || next >= pages) return;
  page_ = next;
  requestUpdate();
}

void GTasksActivity::signOut() {
  // Best effort, and only on a radio that is already up: the token is erased
  // from the card below either way, so this reader can never use it again.
  if (!creds_.refreshToken.empty() && WiFi.status() == WL_CONNECTED) {
    paintBusyNow("SIGNING OUT");
    api_.revoke(creds_.refreshToken);
  }
  library_.signOut();
  {
    RenderLock lock(*this);
    tasks_.clear();
    meta_ = gtasks::Meta{};
  }
  token_ = gtasks::AccessToken{};
  creds_ = gtasks::Credentials{};
  page_ = 0;
  signInReason_.clear();
  LOG_INF(kTag, "signed out; token and list removed from the card");
  show(Phase::SignIn);
}

// --- The loop --------------------------------------------------------------

void GTasksActivity::loop() {
  if (step_ != Step::None && phase_ == Phase::Busy) {
    const Step step = step_;
    step_ = Step::None;
    runStep(step);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (phase_) {
      case Phase::List:
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
        show(creds_.complete() ? Phase::List : Phase::SignIn);
        break;
    }
    return;
  }

  // The cable came out: put the radio down, and repaint so the band stops
  // claiming to sync on its own.
  const bool charging = gpio.isUsbConnected();
  if (charging != wasCharging_) {
    wasCharging_ = charging;
    if (!charging) releaseWifi();
    requestUpdate();
  }

  // Pumped from loop() after Back, as Notes does: there are no background
  // threads, and walking away from the QR is answered at once.
  if (server_ && server_->isRunning()) {
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    std::string pasted;
    if (server_->takeTasksPaste(pasted)) {
      takePaste(pasted);
      return;
    }
  }

  if (phase_ == Phase::List && polling() &&
      gtasks::pollDue(charging, settings_.pollMinutes, everAttempted_, millis(), lastAttemptMs_)) {
    backgroundPoll();
    return;
  }

  // The two side keys page the list, as everywhere else on this device.
  if (phase_ == Phase::List) {
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
    case gtasksui::ActionToggle:
      toggle(event.value);
      break;
    case gtasksui::ActionRefresh:
      requestRefresh();
      break;
    case gtasksui::ActionStartSignIn:
      startSignIn();
      break;
    case gtasksui::ActionCancelSignIn:
      stopPhone();
      show(Phase::SignIn);
      break;
    case gtasksui::ActionPagePrev:
      stepPage(-1);
      break;
    case gtasksui::ActionPageNext:
      stepPage(1);
      break;
    case gtasksui::ActionSettings:
      show(Phase::Settings);
      break;
    case gtasksui::ActionSettingRow:
      if (event.value == static_cast<int>(gtasksui::SettingRow::Poll)) {
        settings_.pollMinutes = gtasks::nextPollMinutes(settings_.pollMinutes);
        library_.saveSettings(settings_);
        // The new interval counts from now, not from the last attempt, so
        // stepping through the choices does not fire a poll on each one.
        lastAttemptMs_ = millis();
        everAttempted_ = true;
        requestUpdate();
      } else if (event.value == static_cast<int>(gtasksui::SettingRow::SignOut)) {
        show(Phase::SignOutConfirm);
      }
      break;
    case gtasksui::ActionCloseSettings:
    case gtasksui::ActionNotice:
      show(creds_.complete() ? Phase::List : Phase::SignIn);
      break;
    case gtasksui::ActionKeepSignedIn:
      show(Phase::Settings);
      break;
    case gtasksui::ActionSignOut:
      signOut();
      break;
    default:
      break;
  }
}

// --- Rendering -------------------------------------------------------------

void GTasksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);
  const char* what = "Tasks";

  switch (phase_) {
    case Phase::Busy: {
      gtasksui::NoticeModel model;
      model.headline = busyHeadline_.c_str();
      if (busyHeadline_ == "SYNCING") {
        model.message =
            gtasks::pendingCount(tasks_) > 0 ? "Sending ticks, then reading the list." : "Reading the list.";
      }
      gtasksui::buildNotice(screen, model);
      what = "Tasks busy";
      break;
    }
    case Phase::Notice: {
      gtasksui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.actionLabel = creds_.complete() ? "BACK TO THE LIST" : "BACK";
      gtasksui::buildNotice(screen, model);
      what = "Tasks notice";
      break;
    }
    case Phase::SignIn:
      gtasksui::buildSignIn(screen, signInReason_.c_str());
      what = "Tasks sign in";
      break;
    case Phase::Phone: {
      const fui::Rect qr = gtasksui::buildPhone(screen, phoneReadable_.c_str());
      if (qr.width > 0) QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      what = "Tasks phone";
      break;
    }
    case Phase::Settings: {
      const std::string poll = gtasks::pollLabel(settings_.pollMinutes);
      gtasksui::SettingsModel model;
      model.pollLabel = poll.c_str();
      model.signedIn = creds_.complete();
      gtasksui::buildSettings(screen, model);
      what = "Tasks settings";
      break;
    }
    case Phase::SignOutConfirm:
      gtasksui::buildSignOutConfirm(screen, gtasks::pendingCount(tasks_));
      what = "Tasks sign out";
      break;
    case Phase::List: {
      const int count = static_cast<int>(tasks_.size());
      const int single = gtasksui::listCapacity(target, device, false);
      const bool paged = count > single;
      perPage_ = paged ? gtasksui::listCapacity(target, device, true) : single;
      const int pages = (count + perPage_ - 1) / perPage_;
      if (page_ >= pages) page_ = pages > 0 ? pages - 1 : 0;
      const int first = page_ * perPage_;
      const int shown = count - first < perPage_ ? count - first : perPage_;

      rows_.clear();
      dueLabels_.clear();
      rows_.reserve(static_cast<size_t>(shown > 0 ? shown : 0));
      dueLabels_.reserve(rows_.capacity());
      for (int i = 0; i < shown; ++i) {
        const gtasks::Task& t = tasks_[static_cast<size_t>(first + i)];
        dueLabels_.push_back(gtasks::dueLabel(t.due));
      }
      for (int i = 0; i < shown; ++i) {
        const gtasks::Task& t = tasks_[static_cast<size_t>(first + i)];
        gtasksui::Row row;
        row.title = t.title.c_str();
        row.due = dueLabels_[static_cast<size_t>(i)].empty() ? nullptr : dueLabels_[static_cast<size_t>(i)].c_str();
        row.checked = t.pending;
        row.child = gtasks::isChild(t, tasks_);
        rows_.push_back(row);
      }

      // The band's one fact, in order of what a glance needs: ticks that have
      // not gone up, a charger poll that cannot get through, the charger
      // keeping it fresh, and otherwise when it last synced.
      const int pending = gtasks::pendingCount(tasks_);
      if (pending > 0) {
        std::snprintf(status_, sizeof(status_), "%d TO SEND", pending);
      } else if (lastPollFailed_) {
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
      if (paged) std::snprintf(pageLabel_, sizeof(pageLabel_), "%d / %d", page_ + 1, pages);

      std::string title = meta_.listTitle.empty() ? std::string("TASKS") : meta_.listTitle;
      for (char& c : title) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      }

      gtasksui::ListModel model;
      model.title = title.c_str();
      model.status = status_[0] != '\0' ? status_ : nullptr;
      model.rows = rows_.empty() ? nullptr : rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.firstIndex = first;
      model.pageLabel = paged ? pageLabel_ : nullptr;
      model.canPagePrev = page_ > 0;
      model.canPageNext = page_ + 1 < pages;
      model.settingsIcon = &icon_go_settings_32;
      if (meta_.lastSyncAt == 0 && !everAttempted_) {
        model.emptyHeadline = "NOT SYNCED YET";
        model.emptyMessage = "Tap REFRESH to fetch your Google Tasks.";
      }
      gtasksui::buildList(screen, model);
      what = "Tasks list";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);
  const bool phaseChanged = !everShown_ || phase_ != lastShownPhase_;
  const auto labels =
      mappedInput.mapLabels("Back", "", phase_ == Phase::List ? "Up" : "", phase_ == Phase::List ? "Down" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  if (phaseChanged) {
    lastShownPhase_ = phase_;
    phaseShownAtMs_ = millis();
    everShown_ = true;
  }
}
