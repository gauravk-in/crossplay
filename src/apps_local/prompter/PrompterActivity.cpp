#include "PrompterActivity.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "../../DevMode.h"
#include "../../WifiCredentialStore.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "PrompterScreens.h"
#include "PrompterStore.h"
#include "fonts/prompter_sans_14.h"
#include "fonts/prompter_sans_17.h"
#include "fonts/prompter_sans_20.h"
#include "fonts/prompter_sans_26.h"
#include "fonts/prompter_sans_32.h"
#include "fonts/prompter_sans_40.h"

namespace fui = freeink::ui;

namespace {

constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr int kMargin = 28;
constexpr int kFooterHeight = 44;
constexpr int kBarHeight = 6;
// The fast waveform ghosts a little with each page; a clean refresh this often
// keeps the words sharp without flashing on every turn.
constexpr int kPagesPerClean = 8;
// The timer bar repaints when it has moved this far, not every second.
constexpr int kBarStepPermille = 100;
constexpr int kScanSeconds = 10;
constexpr size_t kSettingsMax = prompter::kMaxSettingsBytes;

// Arbitrary ids, clear of fontIds.h's hashes and Toybox's 0x70B0'xxxx.
constexpr int kSizeFontIds[prompter::kSizeCount] = {0x7072'0005, 0x7072'0006, 0x7072'0001,
                                                    0x7072'0002, 0x7072'0003, 0x7072'0004};

EpdFont sans14(&prompter_sans_14);
EpdFont sans17(&prompter_sans_17);
EpdFont sans20(&prompter_sans_20);
EpdFont sans26(&prompter_sans_26);
EpdFont sans32(&prompter_sans_32);
EpdFont sans40(&prompter_sans_40);
EpdFontFamily sans14Family(&sans14);
EpdFontFamily sans17Family(&sans17);
EpdFontFamily sans20Family(&sans20);
EpdFontFamily sans26Family(&sans26);
EpdFontFamily sans32Family(&sans32);
EpdFontFamily sans40Family(&sans40);
bool fontsRegistered = false;

void ensurePrompterFonts(GfxRenderer& renderer) {
  if (fontsRegistered) return;
  renderer.insertFont(kSizeFontIds[0], sans14Family);
  renderer.insertFont(kSizeFontIds[1], sans17Family);
  renderer.insertFont(kSizeFontIds[2], sans20Family);
  renderer.insertFont(kSizeFontIds[3], sans26Family);
  renderer.insertFont(kSizeFontIds[4], sans32Family);
  renderer.insertFont(kSizeFontIds[5], sans40Family);
  fontsRegistered = true;
}

struct MeasureContext {
  const GfxRenderer* renderer;
  int font;
  std::string* scratch;
};

int measure(void* context, const char* text, const size_t len) {
  auto* c = static_cast<MeasureContext*>(context);
  c->scratch->assign(text, len);
  return c->renderer->getTextWidth(c->font, c->scratch->c_str());
}

const char* turnName(const prompter::Turn turn) {
  switch (turn) {
    case prompter::Turn::Next:
      return "NEXT";
    case prompter::Turn::Back:
      return "BACK";
    case prompter::Turn::Toggle:
      return "PLAY/PAUSE";
    case prompter::Turn::None:
      break;
  }
  return "";
}

}  // namespace

std::unique_ptr<Activity> PrompterActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PrompterActivity>(renderer, mappedInput);
}

// --- Lifecycle -----------------------------------------------------------------

void PrompterActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  ensurePrompterFonts(renderer);
  if (!prompter::store::begin()) {
    showNotice("The card would not open, so there are no scripts to read.");
    return;
  }
  {
    RenderLock lock(*this);
    loadSettings();
    refreshScripts();
  }
  if (!settings_.turnerAddress.empty()) startTurner();
  setView(View::Library);
}

void PrompterActivity::onExit() {
  stopPhone();
  releaseWifi();
  turner_.end();
  if (view_ == View::Reading) settings_.page = page_;
  saveSettings();
  renderer.setOrientation(GfxRenderer::Portrait);
  Activity::onExit();
}

// --- Data ----------------------------------------------------------------------

void PrompterActivity::loadSettings() {
  settings_ = prompter::parseSettings(prompter::store::read(prompter::store::kSettingsPath, kSettingsMax));
  timer_.setSeconds(settings_.autoSeconds);
}

void PrompterActivity::saveSettings() {
  if (!prompter::store::write(prompter::store::kSettingsPath, prompter::formatSettings(settings_))) {
    LOG_ERR("PROMPT", "settings were not written");
  }
}

void PrompterActivity::refreshScripts() {
  scripts_ = prompter::store::listScripts();
  names_.clear();
  names_.reserve(scripts_.size());
  for (const std::string& s : scripts_) names_.push_back(prompter::displayName(s));
  if (listTop_ >= static_cast<int>(scripts_.size())) listTop_ = 0;
}

bool PrompterActivity::openScript(const std::string& name, const bool resume) {
  std::string raw = prompter::store::read(prompter::store::scriptPath(name).c_str(), prompter::kMaxScriptBytes);
  if (raw.empty()) {
    showNotice("That script is empty, or too long for the reader (128 KB at most).");
    return false;
  }
  {
    RenderLock lock(*this);
    text_ = prompter::cleanScript(raw);
    raw.clear();
    raw.shrink_to_fit();
    // Resuming picks up where this script was left; anything else starts at
    // the top.
    const int resumeAt = resume && settings_.script == name ? settings_.page : 0;
    scriptName_ = name;
    settings_.script = name;
    settings_.page = resumeAt;
    pagedSize_ = -1;
    page_ = settings_.page;
    repaginate();
    timer_.stop();
    cleanNext_ = true;
  }
  saveSettings();
  setView(View::Reading);
  return true;
}

void PrompterActivity::repaginate() {
  const bool landscape = settings_.landscape;
  if (pagedSize_ == settings_.size && pagedLandscape_ == landscape && !paged_.pages.empty()) return;
  // Keep the first word on screen in view across a size change.
  const uint32_t keep =
      paged_.pageCount() > 0 && page_ < paged_.pageCount() && paged_.firstLine(page_) < paged_.lines.size()
          ? paged_.lines[paged_.firstLine(page_)].start
          : 0;
  const bool hadPages = pagedSize_ >= 0;

  const GfxRenderer::Orientation before = renderer.getOrientation();
  renderer.setOrientation(landscape ? GfxRenderer::LandscapeCounterClockwise : GfxRenderer::Portrait);
  const int width = renderer.getScreenWidth() - 2 * kMargin;
  int height = renderer.getScreenHeight() - 2 * kMargin;
  height -= kFooterHeight;
  const int font = fontIdFor(settings_.size);
  int lineHeight = renderer.getLineHeight(font);
  if (lineHeight <= 0) lineHeight = 1;
  int lines = height / lineHeight;
  // The carried line takes a small cut's height at the top.
  lines = (height - renderer.getLineHeight(toybox::kReadingFontId)) / lineHeight;
  if (lines < 1) lines = 1;
  MeasureContext context{&renderer, font, &scratch_};
  paged_ = prompter::paginate(text_, width, lines, &measure, &context);
  pagedSize_ = settings_.size;
  pagedLandscape_ = landscape;
  if (hadPages) page_ = prompter::pageOfOffset(paged_, keep);
  if (page_ >= paged_.pageCount()) page_ = paged_.pageCount() - 1;
  if (page_ < 0) page_ = 0;
  renderer.setOrientation(before);
  LOG_INF("PROMPT", "%s: %d pages of %d lines at size %d", scriptName_.c_str(), paged_.pageCount(), lines,
          settings_.size + 1);
}

int PrompterActivity::fontIdFor(const int size) const {
  const int s = size < 0 ? 0 : size >= prompter::kSizeCount ? prompter::kSizeCount - 1 : size;
  return kSizeFontIds[s];
}

int PrompterActivity::fontId() const { return fontIdFor(settings_.size); }

// --- Navigation ----------------------------------------------------------------

void PrompterActivity::setView(const View view) {
  {
    RenderLock lock(*this);
    if (view == View::Reading && view_ != View::Reading) cleanNext_ = true;
    if (view != View::Reading && view_ == View::Reading) cleanNext_ = true;
    view_ = view;
    interactionsReady_ = false;
  }
  requestUpdate();
}

void PrompterActivity::showNotice(const std::string& text) {
  {
    RenderLock lock(*this);
    notice_ = text;
  }
  setView(View::Notice);
}

void PrompterActivity::leaveReading() {
  {
    RenderLock lock(*this);
    timer_.stop();
    settings_.page = page_;
  }
  saveSettings();
  setView(View::Library);
}

void PrompterActivity::turnPage(const int delta, const bool byHand) {
  {
    RenderLock lock(*this);
    const int next = page_ + delta;
    if (next < 0 || next >= paged_.pageCount()) {
      // Past the last page the timer has nothing left to turn.
      if (next >= paged_.pageCount()) timer_.stop();
      return;
    }
    page_ = next;
    if (byHand) timer_.restart(millis());
    shownPermille_ = -1;
  }
  requestUpdate();
}

void PrompterActivity::toggleTimer() {
  {
    RenderLock lock(*this);
    if (timer_.seconds() <= 0) return;
    if (timer_.running()) {
      timer_.stop();
    } else {
      timer_.start(millis());
    }
    shownPermille_ = -1;
  }
  requestUpdate();
}

// --- The page turner -----------------------------------------------------------

void PrompterActivity::startTurner() {
  if (!prompter::TurnerLink::available()) return;
  if (!turner_.running() && !turner_.begin()) {
    LOG_ERR("PROMPT", "page turner radio did not start");
    return;
  }
  applyTurnerSettings();
}

void PrompterActivity::applyTurnerSettings() {
  turner_.follow(settings_.turnerAddress, settings_.turnerType, settings_.swapTurner);
}

std::string PrompterActivity::turnerLine() const {
  if (!prompter::TurnerLink::available()) return "";
  const std::string name = settings_.turnerName.empty() ? settings_.turnerAddress : settings_.turnerName;
  if (settings_.turnerAddress.empty()) return "NO PAGE TURNER PAIRED";
  switch (turner_.state()) {
    case prompter::TurnerLink::State::Connected:
      return "PAGE TURNER: " + name + " CONNECTED";
    case prompter::TurnerLink::State::Connecting:
      return "PAGE TURNER: CONNECTING TO " + name;
    default:
      return "PAGE TURNER: " + name + " (PRESS A KEY TO WAKE IT)";
  }
}

std::string PrompterActivity::turnerStatus() const {
  if (!prompter::TurnerLink::available()) {
    return "This build has no Bluetooth, so a page turner cannot connect. The side keys and taps still turn pages.";
  }
  if (!turner_.running()) {
    const std::string why = turner_.failure();
    return "Bluetooth did not start" + (why.empty() ? std::string() : ": " + why) +
           ". Leave Prompter and open it again.";
  }
  const std::string name = settings_.turnerName.empty() ? settings_.turnerAddress : settings_.turnerName;
  if (turner_.scanning()) return "Looking for page turners. Put yours in pairing mode now, then tap it below.";
  switch (turner_.state()) {
    case prompter::TurnerLink::State::Connected:
      return "Connected to " + name + ". Press a key on it to test it." +
             (lastTurn_.empty() ? std::string() : " Last key: " + lastTurn_ + ".");
    case prompter::TurnerLink::State::Connecting:
      return "Connecting to " + name + ". The first time, this is the pairing.";
    default:
      break;
  }
  if (!settings_.turnerAddress.empty()) {
    const std::string why = turner_.detail();
    return name + " is paired but not connected. " + (why.empty() ? "Press a key on it to wake it." : why);
  }
  if (!found_.empty()) return "Tap PAIR next to your page turner.";
  return "Put your page turner in pairing mode, then tap SCAN. Most go there by holding a button until a light "
         "blinks.";
}

// --- The phone -----------------------------------------------------------------

bool PrompterActivity::joinWifi() {
#ifdef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) WiFi.begin();
  return WiFi.status() == WL_CONNECTED;
#else
  if (WiFi.status() == WL_CONNECTED) return true;
  if (devmode::holdsRadio()) return false;
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) return false;
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential.has_value()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), credential->password.empty() ? nullptr : credential->password.c_str());
  broughtRadioUp_ = true;
  const unsigned long deadline = millis() + kJoinTimeoutMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) return true;
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return false;
    delay(100);
  }
  return false;
#endif
}

void PrompterActivity::releaseWifi() {
#ifndef SIMULATOR
  if (server_ && server_->isRunning()) return;
  if (broughtRadioUp_) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    broughtRadioUp_ = false;
  }
#endif
}

void PrompterActivity::startPhone() {
  // One radio job at a time: Bluetooth and Wi-Fi share the antenna and the
  // internal memory, and nobody turns pages while sending a script.
  turner_.end();
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) {
    {
      RenderLock lock(*this);
      notice_ = "Joining Wi-Fi...";
    }
    if (!joinWifi()) {
      WiFi.mode(WIFI_STA);
      startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                                 if (!settings_.turnerAddress.empty()) startTurner();
                                 showNotice("Sending a script from your phone needs Wi-Fi. Nothing changed.");
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
  server_ = makeUniqueNoThrow<PrompterServer>();
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
  const bool started = server_->begin();
#ifndef SIMULATOR
  if (!started) {
    stopPhone();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
#else
  (void)started;
#endif

#ifdef SIMULATOR
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  {
    RenderLock lock(*this);
    phoneUrl_ = "http://" + dotted + "/prompter";
#ifdef SIMULATOR
    phoneReadable_ = phoneUrl_;
    (void)mdnsUp;
#else
    phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/prompter" : phoneUrl_;
#endif
    phoneSaved_.clear();
  }
  setView(View::Phone);
}

void PrompterActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
}

// --- Input ---------------------------------------------------------------------

void PrompterActivity::readingInput() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::PageForward) ||
      mappedInput.wasReleased(Button::Right)) {
    turnPage(1, true);
    return;
  }
  if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::PageBack) ||
      mappedInput.wasReleased(Button::Left)) {
    turnPage(-1, true);
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    toggleTimer();
    return;
  }
  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  // The panel stays turned while reading, so the tap is already in the frame
  // the words read in.
  (void)y;
  const int w = renderer.getScreenWidth();
  if (x < w / 3) {
    turnPage(-1, true);
  } else if (x > (2 * w) / 3) {
    turnPage(1, true);
  } else {
    toggleTimer();
  }
}

void PrompterActivity::loop() {
  using Button = MappedInputManager::Button;

  // The remote first: it is the one input that arrives while nobody touches
  // the reader.
  for (prompter::Turn turn = turner_.takeTurn(); turn != prompter::Turn::None; turn = turner_.takeTurn()) {
    {
      RenderLock lock(*this);
      lastTurn_ = turnName(turn);
    }
    if (view_ == View::Reading) {
      if (turn == prompter::Turn::Next) turnPage(1, true);
      if (turn == prompter::Turn::Back) turnPage(-1, true);
      if (turn == prompter::Turn::Toggle) toggleTimer();
    } else if (view_ == View::Turner) {
      requestUpdate();
    }
  }

  // The radio's state, shown wherever it is shown.
  const uint32_t gen = turner_.generation();
  const bool scanning = turner_.scanning();
  if (gen != turnerGen_ || scanning != turnerScanning_ || (scanning && view_ == View::Turner)) {
    bool changed = gen != turnerGen_ || scanning != turnerScanning_;
    turnerGen_ = gen;
    turnerScanning_ = scanning;
    if (view_ == View::Turner) {
      std::vector<prompter::TurnerLink::Found> found = turner_.found();
      if (found.size() != found_.size()) changed = true;
      if (changed) {
        RenderLock lock(*this);
        found_ = std::move(found);
        foundLabels_.clear();
        foundLabels_.reserve(found_.size());
        for (const auto& f : found_) foundLabels_.push_back(f.name.empty() ? f.address : f.name);
      }
    }
    if (changed && view_ != View::Reading) requestUpdate();
  }

  if (view_ == View::Reading) {
    if (mappedInput.wasReleased(Button::Back)) {
      leaveReading();
      return;
    }
    const uint32_t now = millis();
    bool turned = false;
    {
      RenderLock lock(*this);
      turned = timer_.due(now);
    }
    if (turned) {
      if (page_ + 1 >= paged_.pageCount()) {
        RenderLock lock(*this);
        timer_.stop();
        shownPermille_ = -1;
      } else {
        turnPage(1, false);
        return;
      }
      requestUpdate();
      return;
    }
    if (timer_.running()) {
      const int permille = timer_.permille(now);
      if (shownPermille_ < 0 || permille / kBarStepPermille != shownPermille_ / kBarStepPermille) requestUpdate();
    }
    readingInput();
    return;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    switch (view_) {
      case View::Library:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Phone:
        stopPhone();
        releaseWifi();
        if (!settings_.turnerAddress.empty()) startTurner();
        setView(View::Library);
        return;
      case View::Turner:
        setView(View::Settings);
        return;
      case View::Settings:
        setView(settingsReturn_);
        return;
      default:
        setView(View::Library);
        return;
    }
  }

  if (view_ == View::Library) {
    if (mappedInput.wasReleased(Button::Down) && listTop_ + listFit_ < static_cast<int>(scripts_.size())) {
      listTop_ += listFit_;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Up) && listTop_ > 0) {
      listTop_ = listTop_ > listFit_ ? listTop_ - listFit_ : 0;
      requestUpdate();
      return;
    }
  }

  if (server_ && server_->isRunning()) {
    // Pumped from loop(): there are no background threads serving pages.
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    std::string saved;
    if (server_->takeChanged(saved)) {
      RenderLock lock(*this);
      refreshScripts();
      phoneSaved_ = prompter::displayName(saved);
      if (!saved.empty()) {
        settings_.script = saved;
        settings_.page = 0;
      }
      interactionsReady_ = false;
      requestUpdate();
    }
  }

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
  handleAction(action);
}

void PrompterActivity::handleAction(const fui::ActionEvent& action) {
  switch (action.action) {
    case prompterui::ActionOpenScript:
      if (action.value >= 0 && action.value < static_cast<int>(scripts_.size())) {
        openScript(scripts_[static_cast<size_t>(action.value)], true);
      }
      return;
    case prompterui::ActionPhone:
      startPhone();
      return;
    case prompterui::ActionSettings:
      settingsReturn_ = View::Library;
      setView(View::Settings);
      return;
    case prompterui::ActionListUp:
      listTop_ = listTop_ > listFit_ ? listTop_ - listFit_ : 0;
      requestUpdate();
      return;
    case prompterui::ActionListDown:
      if (listTop_ + listFit_ < static_cast<int>(scripts_.size())) listTop_ += listFit_;
      requestUpdate();
      return;
    case prompterui::ActionSizeDown:
    case prompterui::ActionSizeUp: {
      {
        RenderLock lock(*this);
        const int step = action.action == prompterui::ActionSizeUp ? 1 : -1;
        const int size = settings_.size + step;
        if (size >= 0 && size < prompter::kSizeCount) settings_.size = size;
      }
      saveSettings();
      requestUpdate();
      return;
    }
    case prompterui::ActionAutoDown:
    case prompterui::ActionAutoUp: {
      {
        RenderLock lock(*this);
        settings_.autoSeconds = action.action == prompterui::ActionAutoUp ? prompter::nextAuto(settings_.autoSeconds)
                                                                          : prompter::prevAuto(settings_.autoSeconds);
        timer_.setSeconds(settings_.autoSeconds);
        timer_.stop();
      }
      saveSettings();
      requestUpdate();
      return;
    }
    case prompterui::ActionOrientation: {
      RenderLock lock(*this);
      settings_.landscape = !settings_.landscape;
    }
      saveSettings();
      requestUpdate();
      return;
    case prompterui::ActionColors: {
      RenderLock lock(*this);
      settings_.dark = !settings_.dark;
    }
      saveSettings();
      requestUpdate();
      return;
    case prompterui::ActionSwap: {
      RenderLock lock(*this);
      settings_.swapTurner = !settings_.swapTurner;
    }
      turner_.setSwap(settings_.swapTurner);
      applyTurnerSettings();
      saveSettings();
      requestUpdate();
      return;
    case prompterui::ActionTurner:
      startTurner();
      {
        RenderLock lock(*this);
        found_.clear();
        foundLabels_.clear();
        lastTurn_.clear();
      }
      setView(View::Turner);
      return;
    case prompterui::ActionScan:
      startTurner();
      turner_.startScan(kScanSeconds);
      requestUpdate();
      return;
    case prompterui::ActionPair:
      if (action.value >= 0 && action.value < static_cast<int>(found_.size())) {
        {
          RenderLock lock(*this);
          const auto& f = found_[static_cast<size_t>(action.value)];
          settings_.turnerAddress = f.address;
          settings_.turnerType = f.type;
          settings_.turnerName = f.name;
          found_.clear();
          foundLabels_.clear();
        }
        applyTurnerSettings();
        saveSettings();
        requestUpdate();
      }
      return;
    case prompterui::ActionForget: {
      RenderLock lock(*this);
      settings_.turnerAddress.clear();
      settings_.turnerName.clear();
      settings_.turnerType = 0;
    }
      turner_.forget();
      saveSettings();
      requestUpdate();
      return;
    case prompterui::ActionDismiss:
      switch (view_) {
        case View::Phone:
          stopPhone();
          releaseWifi();
          if (!settings_.turnerAddress.empty()) startTurner();
          setView(View::Library);
          return;
        case View::Turner:
          setView(View::Settings);
          return;
        case View::Settings:
          setView(settingsReturn_);
          return;
        default:
          setView(View::Library);
          return;
      }
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------------

void PrompterActivity::drawLine(const int font, const int x, const int y, const prompter::Line& line,
                                const bool black) {
  if (line.length == 0 || line.start >= text_.size()) return;
  scratch_.assign(text_, line.start, line.length);
  renderer.drawText(font, x, y, scratch_.c_str(), black);
}

void PrompterActivity::drawReading() {
  repaginate();
  renderer.setOrientation(settings_.landscape ? GfxRenderer::LandscapeCounterClockwise : GfxRenderer::Portrait);
  const bool dark = settings_.dark;
  const bool ink = !dark;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.clearScreen(dark ? 0x00 : 0xFF);

  const int font = fontId();
  const int lineHeight = renderer.getLineHeight(font);
  const int pages = paged_.pageCount();
  const uint32_t now = millis();
  const int permille = timer_.permille(now);
  int top = kMargin;

  char where[24];
  std::snprintf(where, sizeof(where), "%d / %d", page_ + 1, pages);
  std::string status;
  if (timer_.seconds() > 0) {
    status = timer_.running() ? prompter::autoLabel(timer_.seconds()) + " A PAGE"
                              : (page_ + 1 >= pages ? std::string("END") : std::string("TAP MIDDLE TO START"));
  }
  if (turner_.state() == prompter::TurnerLink::State::Connected) status += status.empty() ? "REMOTE" : "  /  REMOTE";
  const int small = toybox::kTileFontId;
  const int smallHeight = renderer.getLineHeight(small);

  // The last line of the page before, in a small cut: where the eye was a moment ago.
  if (page_ > 0) {
    const uint32_t prevEnd = paged_.endLine(page_ - 1);
    for (uint32_t i = prevEnd; i > paged_.firstLine(page_ - 1); --i) {
      const prompter::Line& line = paged_.lines[i - 1];
      if (line.length == 0) continue;
      scratch_.assign(text_, line.start, line.length);
      std::string carried = "... " + scratch_;
      renderer.drawText(toybox::kReadingFontId, kMargin, top, carried.c_str(), ink);
      break;
    }
  }
  top += renderer.getLineHeight(toybox::kReadingFontId);

  const uint32_t first = paged_.firstLine(page_);
  const uint32_t end = paged_.endLine(page_);
  int y = top;
  for (uint32_t i = first; i < end; ++i) {
    drawLine(font, kMargin, y, paged_.lines[i], ink);
    y += lineHeight;
  }

  const int footerTop = height - kFooterHeight;
  renderer.drawText(small, kMargin, footerTop + (kFooterHeight - smallHeight) / 2, where, ink);
  if (!status.empty()) {
    const int statusW = renderer.getTextWidth(small, status.c_str());
    renderer.drawText(small, width - kMargin - statusW, footerTop + (kFooterHeight - smallHeight) / 2, status.c_str(),
                      ink);
  }
  if (timer_.running()) renderer.fillRect(0, height - kBarHeight, width * permille / 1000, kBarHeight, ink);

  shownPermille_ = timer_.running() ? permille : -1;
  const bool clean = cleanNext_ || ++paintsSinceClean_ >= kPagesPerClean;
  cleanNext_ = false;
  if (clean) paintsSinceClean_ = 0;
  // Left turned while reading, so taps map into the frame the words are in.
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

void PrompterActivity::drawSizeSample(const fui::Rect& box) {
  const int font = fontId();
  const char* sample = "Good evening.";
  const int h = renderer.getLineHeight(font);
  const int y = box.y + (box.height - h) / 2;
  renderer.drawText(font, box.x + 6, y, sample, true);
}

void PrompterActivity::render(RenderLock&&) {
  if (view_ == View::Reading) {
    interactionsReady_ = false;
    drawReading();
    return;
  }
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Library: {
      std::vector<const char*> names;
      names.reserve(names_.size());
      for (const std::string& n : names_) names.push_back(n.c_str());
      int current = -1;
      for (size_t i = 0; i < scripts_.size(); ++i) {
        if (scripts_[i] == settings_.script) current = static_cast<int>(i);
      }
      const std::string line = turnerLine();
      prompterui::LibraryModel model;
      model.names = names.data();
      model.count = static_cast<int>(names.size());
      model.top = listTop_;
      model.current = current;
      model.turner = line.c_str();
      const int fit = prompterui::buildLibrary(screen, model);
      if (fit > 0) listFit_ = fit;
      break;
    }
    case View::Settings: {
      const std::string timer = prompter::autoLabel(settings_.autoSeconds);
      const std::string line = turnerLine();
      prompterui::SettingsModel model;
      model.size = settings_.size;
      model.sizeCount = prompter::kSizeCount;
      model.timer = timer.c_str();
      model.canTimerDown = settings_.autoSeconds > 0;
      model.canTimerUp = settings_.autoSeconds < prompter::kAutoChoices[prompter::kAutoChoiceCount - 1];
      model.landscape = settings_.landscape;
      model.dark = settings_.dark;
      model.swap = settings_.swapTurner;
      model.turner = line.c_str();
      model.bluetooth = prompter::TurnerLink::available();
      const fui::Rect sample = prompterui::buildSettings(screen, model);
      drawSizeSample(sample);
      break;
    }
    case View::Turner: {
      const std::string status = turnerStatus();
      std::vector<const char*> labels;
      labels.reserve(foundLabels_.size());
      for (const std::string& l : foundLabels_) labels.push_back(l.c_str());
      prompterui::TurnerModel model;
      model.status = status.c_str();
      model.found = labels.data();
      model.foundCount = static_cast<int>(labels.size());
      model.scanning = turner_.scanning();
      model.paired = !settings_.turnerAddress.empty();
      prompterui::buildTurner(screen, model);
      break;
    }
    case View::Phone: {
      prompterui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.saved = phoneSaved_.c_str();
      const fui::Rect qr = prompterui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice:
      prompterui::buildNotice(screen, notice_.c_str());
      break;
    case View::Reading:
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Prompter");
  noteSurfaceBuilt();
  const bool clean = cleanNext_;
  cleanNext_ = false;
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
