// The Bambu screens, built against a fake target: what they draw where, and
// what they make tappable.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "BambuScreens.h"

namespace fui = freeink::ui;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

class FakeTarget final : public fui::DrawTarget {
 public:
  struct TextRun {
    fui::Rect rect;
    std::string text;
  };
  std::vector<TextRun> texts;

  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(text == nullptr ? 0 : std::strlen(text) * 10), 20};
  }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle) override {
    texts.push_back({rect, text == nullptr ? "" : text});
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}

  bool drew(const char* needle) const {
    for (const TextRun& run : texts) {
      if (run.text.find(needle) != std::string::npos) return true;
    }
    return false;
  }
};

struct Rendered {
  FakeTarget target;
  toybox::Interactions interactions;

  bool has(const fui::ActionId action) const {
    for (size_t i = 0; i < interactions.count(); i++) {
      if (interactions.data()[i].action == action) return true;
    }
    return false;
  }
  bool allOnScreen(const fui::DeviceContext& device) const {
    for (size_t i = 0; i < interactions.count(); i++) {
      const fui::Rect& r = interactions.data()[i].rect;
      if (r.x < 0 || r.y < 0 || r.x + r.width > device.width || r.y + r.height > device.height) return false;
    }
    for (const FakeTarget::TextRun& run : target.texts) {
      if (run.rect.x < 0 || run.rect.x + run.rect.width > device.width) return false;
      if (run.rect.y < 0 || run.rect.y + run.rect.height > device.height) return false;
    }
    return true;
  }
};

fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

std::string readFile(const char* path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void testMonitor() {
  bambu::PrinterState state;
  bambu::mergeReport(state, readFile("pushall.json"));
  for (const bool landscape : {false, true}) {
    Rendered out;
    fui::DeviceContext ctx = device();
    if (landscape) std::swap(ctx.width, ctx.height);
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    bambuui::MonitorModel model;
    model.state = &state;
    model.link = "LIVE  /  192.168.1.42";
    model.live = true;
    model.endsAt = "ENDS 14:35";
    const bambuui::Hero hero = bambuui::buildMonitor(screen, model);
    CHECK(std::string(hero.text) == "42%");
    CHECK(hero.rect.width > 0);
    CHECK(out.has(bambuui::ActionUsePhone));
    CHECK(out.has(bambuui::ActionRefresh));
    CHECK(!out.interactions.overflowed());
    CHECK(out.allOnScreen(ctx));
    CHECK(out.target.drew("PRINTING"));
    CHECK(out.target.drew("Benchy"));
    CHECK(out.target.drew("1 h 23 min left"));
    CHECK(out.target.drew("LIVE"));
    if (!landscape) {
      CHECK(out.target.drew("AMS A"));
      CHECK(out.target.drew("21% RH"));
      CHECK(out.target.drew("PLA"));
      CHECK(out.target.drew("White"));
    }
    // Nothing outside ASCII reaches a face that cannot draw it.
    for (const FakeTarget::TextRun& run : out.target.texts) {
      for (const char c : run.text) CHECK(static_cast<unsigned char>(c) < 0x80);
    }
  }
  // Not live: the reason is drawn, and the spools make room for it.
  {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    bambuui::MonitorModel model;
    model.state = &state;
    model.link = "NOT LIVE";
    model.reason = "The printer refused the access code.";
    bambuui::buildMonitor(screen, model);
    CHECK(out.target.drew("refused the access code"));
    CHECK(out.allOnScreen(ctx));
  }
  // Before the first report.
  bambu::PrinterState empty;
  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame);
  bambuui::MonitorModel model;
  model.state = &empty;
  model.link = "CONNECTING";
  const bambuui::Hero hero = bambuui::buildMonitor(screen, model);
  CHECK(hero.rect.width == 0);
  CHECK(out.target.drew("Nothing from the printer yet"));
}

void testSetupPhoneNotice() {
  {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    bambuui::buildSetup(screen);
    CHECK(out.has(bambuui::ActionUsePhone));
    CHECK(out.allOnScreen(ctx));
  }
  {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    bambuui::PhoneModel model;
    model.url = "http://192.168.1.40/bambu";
    model.readable = "http://crossplay.local/bambu";
    const fui::Rect qr = bambuui::buildPhone(screen, model);
    CHECK(qr.width == qr.height && qr.width >= 120);
    CHECK(out.has(bambuui::ActionDismiss));
    CHECK(out.target.drew("crossplay.local"));
    CHECK(out.allOnScreen(ctx));
  }
  {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame);
    bambuui::buildNotice(screen, "The printer refused the access code.");
    CHECK(out.has(bambuui::ActionDismiss));
    CHECK(out.allOnScreen(ctx));
  }
}

}  // namespace

int main() {
  testMonitor();
  testSetupPhoneNotice();
  std::printf("bambu screens: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
