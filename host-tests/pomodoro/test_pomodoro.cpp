#include <cstdio>
#include <cstring>

#include "PomodoroCore.h"

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

void testCountdown() {
  pomo::Timer t;
  check(t.state() == pomo::State::Idle, "starts idle");
  check(t.secondsLeft(0) == 25 * 60, "default is 25 minutes");
  t.start(1000);
  check(t.secondsLeft(1000) == 1500, "reads 25:00 at the start");
  check(t.secondsLeft(1001) == 1500, "still 25:00 within the first second");
  check(t.secondsLeft(2000) == 1499, "24:59 after a second");
  check(t.minutesLit(1000 + 60000) == 24, "a whole minute gone unlights one wedge");
  check(t.minutesLit(1000 + 59000) == 25, "a started minute stays lit");
  t.pause(1000 + 90000);
  check(t.state() == pomo::State::Paused, "pauses");
  check(t.secondsLeft(999999) == 1410, "paused time does not move");
  t.start(500000);
  check(t.secondsLeft(500000) == 1410, "resumes where it paused");
  check(!t.tick(500000 + 1409000), "not done a second early");
  check(t.tick(500000 + 1410000), "done at zero");
  check(!t.tick(500000 + 1411000), "done reported once");
  check(t.state() == pomo::State::Done && t.secondsLeft(0) == 0, "done reads 00:00");
  t.start(0);
  check(t.state() == pomo::State::Running && t.secondsLeft(0) == 1500, "play after done starts over");
}

void testWrap() {
  pomo::Timer t;
  t.setDuration(5);
  const uint32_t nearWrap = 0xFFFFFFFFu - 1000;
  t.start(nearWrap);
  check(t.secondsLeft(nearWrap + 2000) == 298, "counts across the millis() rollover");
  check(t.tick(nearWrap + 300000), "finishes across the rollover");
}

void testSetDuration() {
  pomo::Timer t;
  t.setDuration(40);
  check(t.durationMinutes() == 40 && t.secondsLeft(0) == 2400, "sets 40");
  t.setDuration(0);
  check(t.durationMinutes() == pomo::kStepMinutes, "clamps up to 5");
  t.setDuration(90);
  check(t.durationMinutes() == 60, "clamps down to 60");
  t.start(0);
  t.setDuration(10);
  check(t.durationMinutes() == 60, "ignored while running");
  t.pause(30000);
  t.setDuration(10);
  check(t.state() == pomo::State::Idle && t.secondsLeft(0) == 600, "paused: setting rewinds");
  t.start(0);
  t.pause(5000);
  t.reset();
  check(t.state() == pomo::State::Idle && t.secondsLeft(0) == 600, "reset rewinds to the set time");
}

void testFormat() {
  char b[6];
  pomo::formatClock(1500, b);
  check(std::strcmp(b, "25:00") == 0, "25:00");
  pomo::formatClock(3600, b);
  check(std::strcmp(b, "60:00") == 0, "60:00");
  pomo::formatClock(59, b);
  check(std::strcmp(b, "00:59") == 0, "00:59");
}

void testDial() {
  {
    const int w = 480;
    const int h = 800;
    const pomo::Layout l = pomo::layoutFor(w, h);
    for (int mark = pomo::kStepMinutes; mark <= pomo::kMaxMinutes; mark += pomo::kStepMinutes) {
      const float a = pomo::angleOf(static_cast<float>(mark));
      const float r = static_cast<float>(l.rLabel);
      const int x = l.cx + static_cast<int>(std::lround(r * std::sin(a)));
      const int y = l.cy - static_cast<int>(std::lround(r * std::cos(a)));
      check(pomo::markAt(l, x, y) == mark, "a tap on a number picks that number");
      const float rw = static_cast<float>(l.rOuter + l.rInner) / 2;
      const float a2 = pomo::angleOf(static_cast<float>(mark) + 2.0f);
      const int x2 = l.cx + static_cast<int>(std::lround(rw * std::sin(a2)));
      const int y2 = l.cy - static_cast<int>(std::lround(rw * std::cos(a2)));
      check(pomo::markAt(l, x2, y2) == mark, "a tap on the ring snaps to the nearest five");
    }
    check(pomo::markAt(l, l.cx, l.cy) == 0 || pomo::layoutFor(w, h).rInner == 0, "the centre is not the ring");
    check(!pomo::onPlay(l, l.cx, l.cy - l.rOuter), "play is not on the ring");
    check(pomo::onPlay(l, l.playX, l.playY), "play is where it is drawn");
    // Nothing tappable leaves the panel.
    check(l.cy - l.rLabel - 20 >= pomo::kChromeHeight, "numbers clear the header");
    check(l.playY + l.playR <= h, "play is on the panel");
    check(l.cx - l.rLabel - 20 >= 0 && l.cx + l.rLabel + 20 <= w, "numbers fit the width");
  }
}

}  // namespace

int main() {
  testCountdown();
  testWrap();
  testSetDuration();
  testFormat();
  testDial();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("pomodoro: all checks passed\n");
  return 0;
}
