#include "PomodoroActivity.h"

#include <HalFrontlight.h>
#include <Logging.h>
#include <Memory.h>

#include <cmath>
#include <cstdio>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

// Gap either side of a wedge, in minutes of arc, so sixty wedges read as
// sixty rather than as one band.
constexpr float kWedgeGap = 0.09f;
constexpr int kOutline = 2;
constexpr int kMarkWeight = 5;
// The fast waveform ghosts a little with every paint; a half refresh this
// often keeps the clock crisp without flashing every second.
constexpr int kPaintsPerClean = 300;
constexpr int kBlinkToggles = 12;
constexpr uint32_t kBlinkMs = 350;

struct Pt {
  int x, y;
};

Pt polar(const pomo::Layout& l, const float minutes, const float r) {
  const float a = pomo::angleOf(minutes);
  return Pt{l.cx + static_cast<int>(std::lround(r * std::sin(a))),
            l.cy - static_cast<int>(std::lround(r * std::cos(a)))};
}

void wedge(const GfxRenderer& renderer, const pomo::Layout& l, const int minute, const bool filled) {
  const float a0 = static_cast<float>(minute) + kWedgeGap;
  const float a1 = static_cast<float>(minute + 1) - kWedgeGap;
  const Pt p[4] = {polar(l, a0, static_cast<float>(l.rInner)), polar(l, a0, static_cast<float>(l.rOuter)),
                   polar(l, a1, static_cast<float>(l.rOuter)), polar(l, a1, static_cast<float>(l.rInner))};
  if (filled) {
    const int xs[4] = {p[0].x, p[1].x, p[2].x, p[3].x};
    const int ys[4] = {p[0].y, p[1].y, p[2].y, p[3].y};
    renderer.fillPolygon(xs, ys, 4, true);
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const Pt& a = p[i];
    const Pt& b = p[(i + 1) % 4];
    renderer.drawLine(a.x, a.y, b.x, b.y, kOutline, true);
  }
}

void disc(const GfxRenderer& renderer, const int cx, const int cy, const int r, const bool ink) {
  renderer.fillRoundedRect(cx - r, cy - r, 2 * r, 2 * r, r, ink ? Black : White);
}

void centredText(const GfxRenderer& renderer, const int font, const int cx, const int cy, const int boxH,
                 const char* text, const bool black) {
  const int w = renderer.getTextWidth(font, text);
  toybox::drawCapsCentered(renderer, font, cx - w / 2, cy - boxH / 2, boxH, text, black);
}

}  // namespace

std::unique_ptr<Activity> PomodoroActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PomodoroActivity>(renderer, mappedInput);
}

void PomodoroActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  cleanNext = true;
  requestUpdate();
}

void PomodoroActivity::onExit() {
  if (blinksLeft > 0) Frontlight.setOn(lightWasOn);
  blinksLeft = 0;
  Activity::onExit();
}

void PomodoroActivity::drawBand(toybox::Frame& frame) {
  toybox::Screen screen(frame);
  fui::HeaderProps header;
  header.title = "POMODORO";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
}

void PomodoroActivity::drawDial(const pomo::Layout& l, const int lit) {
  for (int m = 0; m < pomo::kMaxMinutes; ++m) wedge(renderer, l, m, m < lit);

  // The mark where the time was set: a heavy spoke through the whole band, so
  // it stays visible as the lit wedges fall away from it.
  const float d = static_cast<float>(timer.durationMinutes());
  const Pt a = polar(l, d, static_cast<float>(l.rInner > 0 ? l.rInner - 12 : 0));
  const Pt b = polar(l, d, static_cast<float>(l.rOuter + 14));
  renderer.drawLine(a.x, a.y, b.x, b.y, kMarkWeight, true);
}

void PomodoroActivity::drawLabels(const pomo::Layout& l) {
  const auto m = toybox::metricsFor(toybox::kUiFontId);
  const int boxH = m.capHeight + 4;
  for (int mark = pomo::kStepMinutes; mark <= pomo::kMaxMinutes; mark += pomo::kStepMinutes) {
    const Pt p = polar(l, static_cast<float>(mark), static_cast<float>(l.rLabel));
    char text[4];
    snprintf(text, sizeof(text), "%d", mark);
    const bool chosen = mark == timer.durationMinutes();
    if (chosen) {
      // The chosen number sits in a filled chip: the one thing on the dial
      // that says what was picked, readable before the ring is.
      const int w = renderer.getTextWidth(toybox::kUiFontId, text) + 14;
      const int h = boxH + 14;
      renderer.fillRoundedRect(p.x - w / 2, p.y - h / 2, w, h, h / 2, Black);
    }
    centredText(renderer, toybox::kUiFontId, p.x, p.y, boxH, text, !chosen);
  }
}

void PomodoroActivity::drawClock(const pomo::Layout& l, const int seconds) {
  char text[6];
  pomo::formatClock(seconds, text);
  const int font = toybox::kLargeFontId;
  const auto m = toybox::metricsFor(font);
  centredText(renderer, font, l.cx, l.clockY, m.capHeight, text, true);
}

void PomodoroActivity::drawControls(const pomo::Layout& l) {
  const pomo::State s = timer.state();
  disc(renderer, l.playX, l.playY, l.playR, true);
  if (s == pomo::State::Running) {
    const int bw = l.playR / 4;
    const int bh = l.playR;
    renderer.fillRect(l.playX - bw - bw / 2, l.playY - bh / 2, bw, bh, false);
    renderer.fillRect(l.playX + bw / 2, l.playY - bh / 2, bw, bh, false);
  } else {
    // The triangle's centroid sits a third of its width right of its back
    // edge; centring the box instead looks pushed left.
    const int h = l.playR;
    const int w = (h * 7) / 8;
    const int x0 = l.playX - w / 3;
    const int xs[3] = {x0, x0, x0 + w};
    const int ys[3] = {l.playY - h / 2, l.playY + h / 2, l.playY};
    renderer.fillPolygon(xs, ys, 3, false);
  }

  if (s == pomo::State::Paused || s == pomo::State::Done) {
    const pomo::Box& r = l.reset;
    renderer.fillRoundedRect(r.x, r.y, r.w, r.h, r.h / 2, White);
    renderer.drawRoundedRect(r.x, r.y, r.w, r.h, 3, r.h / 2, true);
    centredText(renderer, toybox::kButtonFontId, r.x + r.w / 2, r.y + r.h / 2,
                toybox::metricsFor(toybox::kButtonFontId).capHeight, "RESET", true);
  }
}

void PomodoroActivity::render(RenderLock&&) {
  const uint32_t now = millis();
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);

  const pomo::Layout l = pomo::layoutFor(renderer.getScreenWidth(), renderer.getScreenHeight());
  const int seconds = timer.secondsLeft(now);
  const int lit = timer.minutesLit(now);
  drawBand(frame);
  drawDial(l, lit);
  drawLabels(l);
  drawClock(l, seconds);
  drawControls(l);

  shownSeconds = seconds;
  shownMinutes = lit;
  interactionsReady = true;
  noteSurfaceBuilt();

  const bool clean = cleanNext || ++paintsSinceClean >= kPaintsPerClean;
  cleanNext = false;
  if (clean) paintsSinceClean = 0;
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

void PomodoroActivity::finish() {
  LOG_INF("POMO", "Timer finished");
  if (Frontlight.present()) {
    lightWasOn = Frontlight.isOn();
    blinksLeft = kBlinkToggles;
    nextBlinkMs = millis();
  }
  cleanNext = true;
  requestUpdate();
}

void PomodoroActivity::stepBlink(const uint32_t now) {
  if (blinksLeft <= 0 || static_cast<int32_t>(now - nextBlinkMs) < 0) return;
  --blinksLeft;
  // Toggling from the state it was in, and ending on it: an even count leaves
  // the light exactly as it was found.
  Frontlight.setOn(blinksLeft % 2 == 0 ? lightWasOn : !lightWasOn);
  nextBlinkMs = now + kBlinkMs;
}

void PomodoroActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  const uint32_t now = millis();
  stepBlink(now);
  if (timer.tick(now)) {
    finish();
    return;
  }
  if (timer.state() == pomo::State::Running && timer.secondsLeft(now) != shownSeconds) requestUpdate();

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady) return;

  const pomo::Layout l = pomo::layoutFor(renderer.getScreenWidth(), renderer.getScreenHeight());
  const pomo::State s = timer.state();
  if (pomo::onPlay(l, tapX, tapY)) {
    if (s == pomo::State::Running) {
      timer.pause(now);
    } else {
      timer.start(now);
    }
    requestUpdate();
    return;
  }
  if ((s == pomo::State::Paused || s == pomo::State::Done) && l.reset.contains(tapX, tapY)) {
    timer.reset();
    requestUpdate();
    return;
  }
  if (s == pomo::State::Running) return;
  const int mark = pomo::markAt(l, tapX, tapY);
  if (mark > 0) {
    timer.setDuration(mark);
    requestUpdate();
  }
}
