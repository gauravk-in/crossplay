#pragma once

// The C++ side of the app SDK: a FreeInkUI DrawTarget over the crossplay
// imports, and an App base class that turns the four exported entry points into
// virtual calls.
//
// An app links its own copy of FreeInkUI and the Toybox helpers, so its screens
// are the same freestanding builders the built-in apps use. Only the
// primitives below cross into the firmware; a firmware with a different
// FreeInkUI does not change how an installed app looks.

#include <FreeInkUI.h>
#include <crossplay_app.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cpapp {

namespace fui = freeink::ui;

// Which CP_FACE_* each FreeInkUI font slot speaks. Mirrors toybox::Faces: a
// screen binds three (or four) faces and its components name slots.
struct Faces {
  int32_t small = CP_FACE_TILE;
  int32_t body = CP_FACE_UI;
  int32_t title = CP_FACE_DISPLAY;
  int32_t label = -1;  // -1 follows small, as on the firmware's own target
};

class Target final : public fui::DrawTarget {
 public:
  static constexpr fui::FontId FONT_SMALL = 0;
  static constexpr fui::FontId FONT_BODY = 1;
  static constexpr fui::FontId FONT_TITLE = 2;
  static constexpr fui::FontId FONT_LABEL = 3;

  void setFaces(const Faces& faces) { faces_ = faces; }
  fui::DeviceContext deviceContext() const;

  fui::Rect clipRect() const override { return clip_; }
  bool setClipRect(fui::Rect rect) override;
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle style) const override;
  int16_t lineHeight(fui::FontId font) const override;
  fui::Rect inkBounds(fui::FontId font, const char* text, fui::TextStyle style) const override;
  void fill(fui::Rect rect, fui::Paint paint, uint8_t radius = 0, uint8_t corners = fui::CornersAll) override;
  void stroke(fui::Rect rect, fui::Paint paint, uint8_t width, uint8_t radius = 0,
              uint8_t corners = fui::CornersAll) override;
  void line(fui::Point from, fui::Point to, uint8_t width, fui::Paint paint) override;
  void triangle(fui::Point a, fui::Point b, fui::Point c, fui::Paint paint) override;
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override;
  void bitmap(fui::Rect rect, fui::BitmapRef bitmap, fui::BitmapMode mode,
              fui::Paint foreground = fui::Paint::solid(fui::Color::Black),
              fui::Rotation rotation = fui::Rotation::None) override;

 private:
  int32_t faceFor(fui::FontId slot) const;

  Faces faces_{};
  fui::Rect clip_{0, 0, 32767, 32767};
};

class App {
 public:
  virtual ~App() = default;

  // Once, before the first render. False closes the app.
  virtual bool init() { return true; }
  // Draw the whole screen. The firmware cleared it to white and presents it
  // afterwards with the refresh the last requestRender asked for.
  virtual void render(Target& target) = 0;
  // Return true to stay open (to step back inside the app).
  virtual bool onBack() { return false; }
  virtual void onTap(int x, int y) {}
  virtual void onButton(int button) {}
  virtual void onSwipe(int direction) {}
  virtual void onTimer() {}
  // The frame drawn by the last render is now on the panel.
  virtual void onShown() {}
  virtual void onResume() { requestRender(CP_REFRESH_HALF); }
  // About to close or sleep: save anything unsaved.
  virtual void suspend() {}

 protected:
  static void requestRender(int32_t refresh = CP_REFRESH_FAST) { cp_request_render(refresh); }
};

// Defined once by every app.
App* createApp();

// --- Helpers over the imports -----------------------------------------------

// The whole file, or "" when it is missing or larger than `cap`.
std::string readFile(const char* name, size_t cap);
// Atomic replace. False on failure.
bool writeFile(const char* name, const std::string& data);
uint32_t nowMs();
// Days since 1970-01-01 in local time, or -1 while the clock is unset.
int currentDay();
void logf(int32_t level, const char* format, ...) __attribute__((format(printf, 2, 3)));

}  // namespace cpapp
