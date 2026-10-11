#pragma once

// Pomodoro: a kitchen timer drawn as a ring of minute wedges.
//
// Tap a number on the ring to set the time, tap play to start. The wedges still
// lit are the minutes left; the clock in the middle counts seconds. Paused, a
// RESET pill appears above the clock.
//
// PomodoroCore holds the countdown and every position on the screen, so the
// taps here are tested against the same geometry the drawing used.

#include <cstdint>
#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "PomodoroCore.h"

class PomodoroActivity final : public Activity {
 public:
  PomodoroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Pomodoro", renderer, mappedInput) {}
  ~PomodoroActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // A running timer that let the reader fall asleep would stop counting.
  bool preventAutoSleep() override { return timer.state() == pomo::State::Running || blinksLeft > 0; }

 private:
  void drawBand(toybox::Frame& frame);
  void drawDial(const pomo::Layout& l, int lit);
  void drawLabels(const pomo::Layout& l);
  void drawClock(const pomo::Layout& l, int seconds);
  void drawControls(const pomo::Layout& l);
  void finish();
  void stepBlink(uint32_t now);

  pomo::Timer timer;
  int shownSeconds = -1;
  int shownMinutes = -1;
  int paintsSinceClean = 0;
  bool cleanNext = true;

  // The finish alert: the frontlight toggles this many more times.
  int blinksLeft = 0;
  uint32_t nextBlinkMs = 0;
  bool lightWasOn = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
