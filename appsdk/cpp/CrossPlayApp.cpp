#include "CrossPlayApp.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace cpapp {
namespace {

int32_t paintOf(const fui::Paint& paint) {
  switch (paint.kind) {
    case fui::PaintKind::Solid:
      return CP_PAINT(CP_PAINT_SOLID, static_cast<int32_t>(paint.color));
    case fui::PaintKind::Dither:
      return CP_PAINT(CP_PAINT_DITHER, static_cast<int32_t>(paint.color));
    default:
      // Bitmap fills are not in API 1.0.
      return CP_PAINT(CP_PAINT_NONE, 0);
  }
}

int32_t styleOf(const fui::TextStyle& style) {
  return CP_TEXT_STYLE(static_cast<int32_t>(style.align), static_cast<int32_t>(style.color), style.maxLines, style.bold,
                       style.inverted, static_cast<int32_t>(style.rotation));
}

int32_t lengthOf(const char* text) { return text == nullptr ? 0 : static_cast<int32_t>(std::strlen(text)); }

}  // namespace

// --- Target -----------------------------------------------------------------

int32_t Target::faceFor(const fui::FontId slot) const {
  switch (slot) {
    case FONT_BODY:
      return faces_.body;
    case FONT_TITLE:
      return faces_.title;
    case FONT_LABEL:
      return faces_.label >= 0 ? faces_.label : faces_.small;
    default:
      return faces_.small;
  }
}

fui::DeviceContext Target::deviceContext() const {
  cp_device_info_t info{};
  info.size = sizeof(info);
  cp_device_info(&info);
  fui::DeviceContext device;
  device.width = static_cast<int16_t>(info.width);
  device.height = static_cast<int16_t>(info.height);
  device.hasTouch = info.hasTouch != 0;
  device.hasButtons = info.hasButtons != 0;
  device.safeArea = fui::Insets{static_cast<int16_t>(info.safeTop), static_cast<int16_t>(info.safeRight),
                                static_cast<int16_t>(info.safeBottom), static_cast<int16_t>(info.safeLeft)};
  device.minTouchSize = static_cast<int16_t>(info.minTouchSize);
  return device;
}

bool Target::setClipRect(const fui::Rect rect) {
  if (cp_set_clip(rect.x, rect.y, rect.width, rect.height) == 0) return false;
  clip_ = rect.width > 0 ? rect : fui::Rect{0, 0, 32767, 32767};
  return true;
}

fui::Size Target::measureText(const fui::FontId font, const char* text, const fui::TextStyle style) const {
  cp_size_t size{};
  cp_measure_text(faceFor(font), text, lengthOf(text), styleOf(style), &size);
  return fui::Size{static_cast<int16_t>(size.width), static_cast<int16_t>(size.height)};
}

int16_t Target::lineHeight(const fui::FontId font) const { return static_cast<int16_t>(cp_line_height(faceFor(font))); }

fui::Rect Target::inkBounds(const fui::FontId font, const char* text, const fui::TextStyle style) const {
  cp_rect_t r{};
  cp_ink_bounds(faceFor(font), text, lengthOf(text), styleOf(style), &r);
  return fui::Rect{static_cast<int16_t>(r.x), static_cast<int16_t>(r.y), static_cast<int16_t>(r.width),
                   static_cast<int16_t>(r.height)};
}

void Target::fill(const fui::Rect rect, const fui::Paint paint, const uint8_t radius, const uint8_t corners) {
  cp_fill(rect.x, rect.y, rect.width, rect.height, paintOf(paint), radius, corners);
}

void Target::stroke(const fui::Rect rect, const fui::Paint paint, const uint8_t width, const uint8_t radius,
                    const uint8_t corners) {
  cp_stroke(rect.x, rect.y, rect.width, rect.height, paintOf(paint), width, radius, corners);
}

void Target::line(const fui::Point from, const fui::Point to, const uint8_t width, const fui::Paint paint) {
  cp_line(from.x, from.y, to.x, to.y, width, paintOf(paint));
}

void Target::triangle(const fui::Point a, const fui::Point b, const fui::Point c, const fui::Paint paint) {
  cp_triangle(a.x, a.y, b.x, b.y, c.x, c.y, paintOf(paint));
}

void Target::text(const fui::Rect rect, const char* text, const fui::TextStyle style) {
  cp_text(rect.x, rect.y, rect.width, rect.height, text, lengthOf(text), faceFor(style.font), styleOf(style));
}

void Target::bitmap(const fui::Rect rect, const fui::BitmapRef bitmap, const fui::BitmapMode mode,
                    const fui::Paint foreground, fui::Rotation) {
  int32_t format;
  switch (bitmap.format) {
    case fui::BitmapFormat::Mask1:
      format = CP_BITMAP_MASK1;
      break;
    case fui::BitmapFormat::BW1:
      format = CP_BITMAP_BW1;
      break;
    default:
      return;  // not in API 1.0
  }
  int32_t fit;
  switch (mode) {
    case fui::BitmapMode::Stretch:
      fit = CP_FIT_STRETCH;
      break;
    case fui::BitmapMode::Contain:
      fit = CP_FIT_CONTAIN;
      break;
    case fui::BitmapMode::Cover:
      fit = CP_FIT_COVER;
      break;
    default:
      fit = CP_FIT_CENTER;
      break;
  }
  cp_bitmap(rect.x, rect.y, rect.width, rect.height, bitmap.data, bitmap.width, bitmap.height, format, fit,
            paintOf(foreground));
}

// --- Helpers ----------------------------------------------------------------

std::string readFile(const char* name, const size_t cap) {
  const int32_t nameLen = lengthOf(name);
  const int32_t size = cp_file_size(name, nameLen);
  if (size <= 0 || static_cast<size_t>(size) > cap) return std::string();
  std::string out(static_cast<size_t>(size), '\0');
  const int32_t got = cp_file_read(name, nameLen, out.data(), size);
  if (got < 0) return std::string();
  out.resize(static_cast<size_t>(got));
  return out;
}

bool writeFile(const char* name, const std::string& data) {
  return cp_file_write(name, lengthOf(name), data.data(), static_cast<int32_t>(data.size())) == 0;
}

uint32_t nowMs() { return static_cast<uint32_t>(cp_now_ms()); }

int currentDay() {
  // Below this the clock was never set; the floor the built-in apps use.
  constexpr int64_t kClockFloor = 1700000000;
  const int64_t now = cp_unix_time();
  if (now < kClockFloor) return -1;
  const int64_t local = now + static_cast<int64_t>(cp_utc_offset_minutes()) * 60;
  return static_cast<int>(local / 86400);
}

void logf(const int32_t level, const char* format, ...) {
  char line[160];
  va_list args;
  va_start(args, format);
  const int n = std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (n < 0) return;
  cp_log(level, line, n < static_cast<int>(sizeof(line)) ? n : static_cast<int>(sizeof(line)) - 1);
}

}  // namespace cpapp

// --- The exported entry points ----------------------------------------------

namespace {
cpapp::App* gApp = nullptr;
cpapp::Target gTarget;
}  // namespace

extern "C" {

CP_EXPORT(cp_app_api_version) int32_t cp_app_api_version(void) {
  return (CP_API_VERSION_MAJOR << 16) | CP_API_VERSION_MINOR;
}

CP_EXPORT(cp_app_init) int32_t cp_app_init(void) {
  gApp = cpapp::createApp();
  if (gApp == nullptr) return 1;
  return gApp->init() ? 0 : 2;
}

CP_EXPORT(cp_app_render) void cp_app_render(void) {
  if (gApp != nullptr) gApp->render(gTarget);
}

CP_EXPORT(cp_app_event) int32_t cp_app_event(const int32_t type, const int32_t a, const int32_t b, const int32_t) {
  if (gApp == nullptr) return 0;
  switch (type) {
    case CP_EVENT_BACK:
      return gApp->onBack() ? 1 : 0;
    case CP_EVENT_TAP:
      gApp->onTap(a, b);
      return 1;
    case CP_EVENT_BUTTON:
      gApp->onButton(a);
      return 1;
    case CP_EVENT_SWIPE:
      gApp->onSwipe(a);
      return 1;
    case CP_EVENT_TIMER:
      gApp->onTimer();
      return 1;
    case CP_EVENT_RESUME:
      gApp->onResume();
      return 1;
    case CP_EVENT_SHOWN:
      gApp->onShown();
      return 1;
    default:
      return 0;
  }
}

CP_EXPORT(cp_app_suspend) void cp_app_suspend(void) {
  if (gApp != nullptr) gApp->suspend();
}

}  // extern "C"
