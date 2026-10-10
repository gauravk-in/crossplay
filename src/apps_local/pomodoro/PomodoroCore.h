#pragma once

// Pomodoro's rules and geometry, freestanding so host-tests/pomodoro can drive
// them without a panel: the countdown, and where every tappable thing on the
// dial is. The activity draws from the same Layout it hit-tests against.

#include <cmath>
#include <cstdint>

namespace pomo {

constexpr int kMaxMinutes = 60;
constexpr int kStepMinutes = 5;
constexpr int kDefaultMinutes = 25;
constexpr uint32_t kMinuteMs = 60000;

enum class State : uint8_t { Idle, Running, Paused, Done };

// Counts down against a deadline rather than by decrementing, so a slow paint
// or a busy loop pass never loses time. All arithmetic is unsigned and
// wrap-safe: millis() rolls over after 49 days.
class Timer {
 public:
  State state() const { return state_; }
  int durationMinutes() const { return durationMin_; }

  // Ignored while running; otherwise sets the dial and rewinds to it.
  void setDuration(const int minutes) {
    if (state_ == State::Running) return;
    durationMin_ = minutes < kStepMinutes ? kStepMinutes : minutes > kMaxMinutes ? kMaxMinutes : minutes;
    reset();
  }

  void start(const uint32_t now) {
    if (state_ == State::Running) return;
    if (state_ == State::Done) remainingMs_ = static_cast<uint32_t>(durationMin_) * kMinuteMs;
    deadline_ = now + remainingMs_;
    state_ = State::Running;
  }

  void pause(const uint32_t now) {
    if (state_ != State::Running) return;
    remainingMs_ = remainingMs(now);
    state_ = State::Paused;
  }

  void reset() {
    remainingMs_ = static_cast<uint32_t>(durationMin_) * kMinuteMs;
    state_ = State::Idle;
  }

  // True exactly once, on the pass that reaches zero.
  bool tick(const uint32_t now) {
    if (state_ != State::Running || remainingMs(now) > 0) return false;
    remainingMs_ = 0;
    state_ = State::Done;
    return true;
  }

  uint32_t remainingMs(const uint32_t now) const {
    if (state_ != State::Running) return remainingMs_;
    const int32_t left = static_cast<int32_t>(deadline_ - now);
    return left > 0 ? static_cast<uint32_t>(left) : 0;
  }

  // Whole seconds still to go, rounded up, so the display reads 25:00 for the
  // whole first second and only shows 00:00 when it is actually over.
  int secondsLeft(const uint32_t now) const { return static_cast<int>((remainingMs(now) + 999) / 1000); }

  // Minute wedges still lit: a minute that has started is still owed.
  int minutesLit(const uint32_t now) const { return static_cast<int>((remainingMs(now) + kMinuteMs - 1) / kMinuteMs); }

 private:
  State state_ = State::Idle;
  int durationMin_ = kDefaultMinutes;
  uint32_t remainingMs_ = static_cast<uint32_t>(kDefaultMinutes) * kMinuteMs;
  uint32_t deadline_ = 0;
};

// "MM:SS" into a buffer of at least 6 bytes. 60:00 is the largest it is given.
inline void formatClock(const int seconds, char* out) {
  const int s = seconds < 0 ? 0 : seconds;
  const int m = s / 60 > 99 ? 99 : s / 60;
  out[0] = static_cast<char>('0' + m / 10);
  out[1] = static_cast<char>('0' + m % 10);
  out[2] = ':';
  out[3] = static_cast<char>('0' + (s % 60) / 10);
  out[4] = static_cast<char>('0' + s % 10);
  out[5] = '\0';
}

struct Box {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(const int px, const int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Every position on the screen, from the panel size alone.
struct Layout {
  int cx = 0, cy = 0;  // dial centre
  int rInner = 0;      // inside edge of the minute wedges
  int rOuter = 0;      // outside edge of the minute wedges
  int rLabel = 0;      // centre of the 5-minute numbers
  int clockY = 0;      // vertical centre of MM:SS
  int playX = 0, playY = 0, playR = 0;
  Box reset;
};

// The panel row the body starts on: Toybox's band, its gap and its rule.
constexpr int kChromeHeight = 76 + 4 + 3;

inline Layout layoutFor(const int w, const int h) {
  Layout l;
  const int bodyH = h - kChromeHeight;
  l.cx = w / 2;
  l.rLabel = w / 2 - 34;
  l.rOuter = l.rLabel - 30;
  l.rInner = l.rOuter - 48;
  l.cy = kChromeHeight + bodyH / 2 - 20;
  l.clockY = l.cy;
  l.playR = 34;
  l.playX = l.cx;
  l.playY = l.cy + 84;
  l.reset = Box{l.cx - 62, l.cy - 108, 124, 44};
  return l;
}

// Angle of a minute mark, clockwise from twelve o'clock, in radians.
inline float angleOf(const float minutes) { return minutes * (6.2831853f / kMaxMinutes); }

// The 5-minute mark nearest a point, 5..60, or 0 when the point is not on the
// dial (the wedge band out to the numbers, with a thumb's slack either side).
// Twelve o'clock is 60, the top of the scale, so the dial reads like a kitchen
// timer turned to its stop.
inline int markAt(const Layout& l, const int x, const int y) {
  const float dx = static_cast<float>(x - l.cx);
  const float dy = static_cast<float>(y - l.cy);
  const float r = std::sqrt(dx * dx + dy * dy);
  if (r < l.rInner - 20 || r > l.rLabel + 34) return 0;
  float a = std::atan2(dx, -dy);
  if (a < 0) a += 6.2831853f;
  const int step = static_cast<int>(std::lround(a / angleOf(kStepMinutes)));
  const int mark = step * kStepMinutes;
  return mark <= 0 || mark > kMaxMinutes ? kMaxMinutes : mark;
}

inline bool onPlay(const Layout& l, const int x, const int y) {
  const int dx = x - l.playX;
  const int dy = y - l.playY;
  const int reach = l.playR + 16;
  return dx * dx + dy * dy <= reach * reach;
}

}  // namespace pomo
