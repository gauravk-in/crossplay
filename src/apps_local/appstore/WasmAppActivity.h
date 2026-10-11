#pragma once

// A downloaded app on screen. The app draws and decides; this activity owns
// the panel and the input, and turns both into the events of the app API.
//
// Back always leaves in the end: the app sees it first and may keep it (to go
// up a level in its own screens), but if it says it did not handle it, or
// traps, the activity closes. An app cannot trap the person inside it.

#include <cstdint>
#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "AppLibrary.h"
#include "WasmApp.h"

class WasmAppActivity final : public Activity {
 public:
  WasmAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, appstore::InstalledApp app)
      : Activity("WasmApp", renderer, mappedInput), installed_(std::move(app)) {}
  ~WasmAppActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void deliver(int32_t type, int32_t a = 0, int32_t b = 0);
  void collectRequests();
  void close();

  appstore::InstalledApp installed_;
  std::unique_ptr<appstore::WasmApp> app_;
  bool showingError_ = false;
  bool closing_ = false;

  int32_t refresh_ = 0;  // CP_REFRESH_* for the next paint
  bool firstPaint_ = true;
  bool timerArmed_ = false;
  uint32_t timerAt_ = 0;
  // Set by render() on the render task, read by loop(): SHOWN is delivered
  // from the loop so every app call starts from the same place.
  volatile bool shownPending_ = false;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
