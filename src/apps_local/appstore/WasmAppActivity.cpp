#include "WasmAppActivity.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include "../../../appsdk/include/crossplay_app.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "AppStoreScreens.h"

namespace fui = freeink::ui;

void WasmAppActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  appstore::WasmApp::Device device;
  device.hasTouch = mappedInput.hasTouch();
  device.hasButtons = true;
  app_ = makeUniqueNoThrow<appstore::WasmApp>(renderer, device);
  if (!app_ || !app_->start(installed_.packageDir, installed_.manifest, installed_.dataDir)) {
    showingError_ = true;
  } else {
    collectRequests();
  }
  firstPaint_ = true;
  requestUpdate();
}

void WasmAppActivity::onExit() {
  if (app_) {
    app_->suspend();
    app_.reset();  // stops the app's task and frees its memory before we go
  }
  Activity::onExit();
}

void WasmAppActivity::prepareForSleep() {
  if (app_) app_->suspend();
}

void WasmAppActivity::close() {
  if (closing_) return;
  closing_ = true;
  finish();
}

void WasmAppActivity::deliver(const int32_t type, const int32_t a, const int32_t b) {
  if (!app_ || showingError_) return;
  const int32_t handled = app_->event(type, a, b);
  if (handled < 0 || app_->failed()) {
    showingError_ = true;
    requestUpdate();
    return;
  }
  if (type == CP_EVENT_BACK && handled == 0) {
    close();
    return;
  }
  collectRequests();
}

void WasmAppActivity::collectRequests() {
  if (!app_) return;
  if (app_->failed()) {
    showingError_ = true;
    requestUpdate();
    return;
  }
  if (app_->exitRequested()) {
    close();
    return;
  }
  int32_t ms = 0;
  if (app_->takeTimer(ms)) {
    timerArmed_ = ms > 0;
    timerAt_ = millis() + static_cast<uint32_t>(ms);
  }
  int32_t refresh = 0;
  if (app_->takeRenderRequest(refresh)) {
    refresh_ = std::max(refresh_, refresh);
    requestUpdate();
  }
}

void WasmAppActivity::loop() {
  if (closing_) return;

  if (showingError_) {
    int x = 0;
    int y = 0;
    bool tapped = false;
    if (mappedInput.wasScreenTapped(x, y) && interactionsReady_) {
      fui::InputSnapshot input;
      input.touchReleased = true;
      input.touchX = static_cast<int16_t>(x);
      input.touchY = static_cast<int16_t>(y);
      tapped = interactions_.route(input).action == appstoreui::ActionPrimary;
    }
    if (tapped || mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      close();
    }
    return;
  }

  if (shownPending_) {
    shownPending_ = false;
    deliver(CP_EVENT_SHOWN);
    if (closing_ || showingError_) return;
  }

  // Back is also the left-edge swipe on a touch board, so it is read before
  // the other swipes can claim it.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    deliver(CP_EVENT_BACK);
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    deliver(CP_EVENT_TAP, x, y);
    return;
  }

  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Left:
      deliver(CP_EVENT_SWIPE, CP_SWIPE_LEFT);
      return;
    case MappedInputManager::SwipeDir::Right:
      deliver(CP_EVENT_SWIPE, CP_SWIPE_RIGHT);
      return;
    case MappedInputManager::SwipeDir::Up:
      deliver(CP_EVENT_SWIPE, CP_SWIPE_UP);
      return;
    case MappedInputManager::SwipeDir::Down:
      deliver(CP_EVENT_SWIPE, CP_SWIPE_DOWN);
      return;
    default:
      break;
  }

  struct Mapping {
    MappedInputManager::Button button;
    int32_t code;
  };
  static constexpr Mapping kButtons[] = {
      {MappedInputManager::Button::Up, CP_BUTTON_UP},
      {MappedInputManager::Button::Down, CP_BUTTON_DOWN},
      {MappedInputManager::Button::Left, CP_BUTTON_LEFT},
      {MappedInputManager::Button::Right, CP_BUTTON_RIGHT},
      {MappedInputManager::Button::Confirm, CP_BUTTON_CONFIRM},
      {MappedInputManager::Button::PageBack, CP_BUTTON_PAGE_BACK},
      {MappedInputManager::Button::PageForward, CP_BUTTON_PAGE_FORWARD},
  };
  for (const Mapping& m : kButtons) {
    if (mappedInput.wasReleased(m.button)) {
      deliver(CP_EVENT_BUTTON, m.code);
      return;
    }
  }

  if (timerArmed_ && static_cast<int32_t>(millis() - timerAt_) >= 0) {
    timerArmed_ = false;
    deliver(CP_EVENT_TIMER);
  }
}

void WasmAppActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (!showingError_ && app_ && app_->render()) {
    HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
    if (firstPaint_ || refresh_ == CP_REFRESH_HALF) mode = HalDisplay::HALF_REFRESH;
    if (refresh_ == CP_REFRESH_FULL) mode = HalDisplay::FULL_REFRESH;
    renderer.displayBuffer(mode);
    firstPaint_ = false;
    refresh_ = CP_REFRESH_FAST;
    shownPending_ = true;
    return;
  }

  // The app failed: before its first frame, or in the middle of one. Whatever
  // it half drew is gone; the reason takes the screen.
  showingError_ = true;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  appstoreui::NoticeModel model;
  model.title = "APP STOPPED";
  model.message = installed_.manifest.name + ": " + (app_ ? app_->error() : std::string("not enough memory."));
  model.button = "CLOSE";
  appstoreui::buildNotice(screen, model);
  interactionsReady_ = true;
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
