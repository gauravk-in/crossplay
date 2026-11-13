#include "BambuScreens.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../ui/ToyboxText.h"
#include "BambuIcons.h"

namespace bambuui {
namespace {

using bambu::AmsUnit;
using bambu::PrinterState;
using bambu::Tray;

constexpr int16_t kFooterHeight = toybox::kPillHeight;

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  style.maxLines = 1;
  return style;
}

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);
const fui::Paint kPaper = fui::Paint::solid(fui::Color::White);

// The Toybox cuts above 10px carry ASCII only, and a glyph the face lacks
// draws as nothing. A plate named in another script keeps its digits.
std::string ascii(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x20 && u < 0x7F) out.push_back(c);
  }
  return out;
}

void line(toybox::Screen& screen, const fui::Rect& box, const std::string& text, const fui::FontId font,
          const fui::TextAlign align = fui::TextAlign::Left, const fui::Color color = fui::Color::Black) {
  fui::TextStyle style = plain(font, align, color);
  const std::string drawn = toybox::fittedTitle(screen.target(), text.c_str(), box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

int16_t lineHeight(toybox::Screen& screen, const fui::FontId font) { return screen.target().lineHeight(font); }

int16_t prose(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
              const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style = plain(font, align);
  const int16_t height = lineHeight(screen, font);
  int lines = height > 0 ? box.height / height : 1;
  if (lines < 1) lines = 1;
  if (lines > 10) lines = 10;
  style.maxLines = static_cast<uint8_t>(lines);
  // fitLines hands back the text untouched when it fits and lets the component
  // wrap it, so the lines it takes are found by asking for fewer.
  const std::string drawn = toybox::fitLines(screen.target(), text, box.width, lines, style);
  int used = lines;
  if (drawn == text) {
    for (int n = 1; n < lines; ++n) {
      if (toybox::fitLines(screen.target(), text, box.width, n, style) == drawn) {
        used = n;
        break;
      }
    }
  }
  style.maxLines = static_cast<uint8_t>(used);
  // Drawn in a box exactly its own height, so it sits at the top of `box`.
  screen.target().text(fui::makeRect(box.x, box.y, box.width, static_cast<int16_t>(used * height)), drawn.c_str(),
                       style);
  return static_cast<int16_t>(used * height);
}

void outline(toybox::Screen& screen, const fui::Rect& r, const int16_t width) {
  screen.target().fill(fui::makeRect(r.x, r.y, r.width, width), kInk);
  screen.target().fill(fui::makeRect(r.x, static_cast<int16_t>(r.y + r.height - width), r.width, width), kInk);
  screen.target().fill(fui::makeRect(r.x, r.y, width, r.height), kInk);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(r.x + r.width - width), r.y, width, r.height), kInk);
}

fui::Paint inkFor(const Tray& tray) {
  switch (bambu::inkLevel(tray.rgb)) {
    case 0:
      return kPaper;
    case 1:
      return fui::Paint::dither(fui::Color::LightGray);
    case 2:
      return fui::Paint::dither(fui::Color::DarkGray);
    default:
      return kInk;
  }
}

// A filled bar with a ruled frame. `percent` below zero draws an empty frame.
void bar(toybox::Screen& screen, const fui::Rect& r, const int percent, const int16_t frame = 2) {
  outline(screen, r, frame);
  if (percent <= 0) return;
  const int p = percent > 100 ? 100 : percent;
  const int16_t inner = static_cast<int16_t>(r.width - 2 * (frame + 1));
  const int16_t filled = static_cast<int16_t>(inner * p / 100);
  if (filled > 0) {
    screen.target().fill(fui::makeRect(static_cast<int16_t>(r.x + frame + 1), static_cast<int16_t>(r.y + frame + 1),
                                       filled, static_cast<int16_t>(r.height - 2 * (frame + 1))),
                         kInk);
  }
}

// A square of the spool's colour, inked as close as black and white can get.
void swatch(toybox::Screen& screen, const fui::Rect& r, const Tray& tray) {
  if (!tray.loaded) {
    outline(screen, r, 1);
    return;
  }
  screen.target().fill(r, tray.hasColor ? inkFor(tray) : kPaper);
  outline(screen, r, 2);
}

std::string remainText(const Tray& tray) {
  if (!tray.loaded) return "";
  if (tray.remain < 0) return "?";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d%%", tray.remain > 100 ? 100 : tray.remain);
  return buf;
}

std::string material(const Tray& tray) {
  if (!tray.loaded) return "EMPTY";
  return ascii(tray.type.empty() ? std::string("?") : tray.type);
}

std::string colorText(const Tray& tray) {
  if (!tray.loaded) return "";
  return tray.hasColor ? bambu::colorName(tray.rgb) : "No color";
}

bool isActive(const PrinterState& state, const int unit, const int slot) {
  if (unit < 0) return state.trayNow == bambu::kTrayExternal;
  return state.trayNow < bambu::kTrayExternal && state.trayNow == unit * bambu::kTraysPerAms + slot;
}

std::string unitTitle(const AmsUnit& unit, const int index) {
  std::string title = "AMS ";
  title.push_back(static_cast<char>('A' + index));
  const std::string humidity = bambu::humidityPhrase(unit);
  if (!humidity.empty()) title += "   " + humidity;
  if (unit.hasTemp) title += "   " + bambu::wholeDegrees(unit.tempTenths) + " C";
  return title;
}

std::string pair(const int now, const int target) {
  if (target <= 0) return bambu::wholeDegrees(now);
  return bambu::wholeDegrees(now) + "/" + bambu::wholeDegrees(target);
}

std::string layers(const PrinterState& s) {
  if (s.layer < 0 || s.totalLayers <= 0) return "";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "LAYER %d / %d", s.layer, s.totalLayers);
  return buf;
}

std::string timeLeft(const PrinterState& s) {
  if (s.remainingMin < 0) return "";
  const bambu::Stage stage = s.stage();
  if (stage != bambu::Stage::Printing && stage != bambu::Stage::Paused && stage != bambu::Stage::Preparing) return "";
  return bambu::durationPhrase(s.remainingMin) + " left";
}

bool printing(const PrinterState& s) {
  const bambu::Stage stage = s.stage();
  return stage == bambu::Stage::Printing || stage == bambu::Stage::Paused || stage == bambu::Stage::Preparing;
}

// Header band and the phone on its right; returns the bottom of the chrome.
void chrome(toybox::Screen& screen, const bool phone) {
  fui::HeaderProps header;
  header.title = "BAMBU";
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.borderEdges = fui::EdgesNone;
  if (phone) {
    header.trailingIcon = fui::bitmapFromIcon(icon_b_phone_32);
    header.trailingAction = ActionUsePhone;
    header.trailingStyles = toybox::rowStyles();
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), kFooterHeight);
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  screen.button(button, box);
}

// The connection line along the bottom, inverted while the data is live so
// "is this current" is answered from across the room. Tapping it asks the
// printer for everything again.
// Why the data is not live, in up to two lines over the band.
constexpr int kReasonLines = 2;

bool showsReason(const MonitorModel& model) { return !model.live && model.reason[0] != '\0'; }

void linkLine(toybox::Screen& screen, const MonitorModel& model) {
  const fui::DeviceContext& device = screen.device();
  const int16_t h = static_cast<int16_t>(lineHeight(screen, toybox::kTileFont) + 10);
  if (showsReason(model)) {
    const int16_t small = lineHeight(screen, toybox::kTileFont);
    const int16_t top = static_cast<int16_t>(device.height - toybox::kMargin - h - 6 - small * kReasonLines);
    prose(screen,
          fui::makeRect(toybox::kMargin, top, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                        static_cast<int16_t>(small * kReasonLines)),
          model.reason, toybox::kTileFont);
  }
  const fui::Rect band = fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - h),
                                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), h);
  if (model.live) {
    screen.target().fill(band, kInk, 6);
  } else {
    outline(screen, band, 2);
  }
  line(screen,
       fui::makeRect(static_cast<int16_t>(band.x + 8), static_cast<int16_t>(band.y + 5),
                     static_cast<int16_t>(band.width - 16), static_cast<int16_t>(h - 10)),
       model.link, toybox::kTileFont, fui::TextAlign::Center, model.live ? fui::Color::White : fui::Color::Black);
  fui::ButtonProps hit;
  hit.label = "";
  hit.action = ActionRefresh;
  hit.styles = toybox::rowStyles();
  hit.styles.normal.background = fui::Paint::none();
  hit.styles.normal.border = fui::Paint::none();
  screen.button(hit, band);
}

int16_t linkTop(toybox::Screen& screen, const MonitorModel& model) {
  const int16_t small = lineHeight(screen, toybox::kTileFont);
  int16_t top = static_cast<int16_t>(screen.device().height - toybox::kMargin - small - 10);
  if (showsReason(model)) top = static_cast<int16_t>(top - 6 - small * kReasonLines);
  return top;
}

// The slot label, inverted on the slot that is feeding the nozzle.
void slotLabel(toybox::Screen& screen, const fui::Rect& box, const std::string& name, const bool active,
               const fui::TextAlign align = fui::TextAlign::Center) {
  if (active) screen.target().fill(box, kInk, 4);
  line(screen, box, name, toybox::kTileFont, align, active ? fui::Color::White : fui::Color::Black);
}

void setHero(Hero& hero, const fui::Rect& rect, const PrinterState& s) {
  hero.rect = rect;
  if (s.percent < 0) {
    std::snprintf(hero.text, sizeof(hero.text), "--");
  } else {
    std::snprintf(hero.text, sizeof(hero.text), "%u%%",
                  static_cast<unsigned>(s.percent > 100 ? 100 : s.percent) % 1000);
  }
}

// --- The top half: what the print is doing ------------------------------------

// Stage and plate name over the hero number and the bar.
int16_t progressBlock(toybox::Screen& screen, const MonitorModel& model, int16_t y, const int16_t heroHeight,
                      Hero& hero) {
  const PrinterState& s = *model.state;
  const int16_t width = static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin);
  const int16_t x = toybox::kMargin;
  const int16_t small = lineHeight(screen, toybox::kTileFont);
  const int16_t body = lineHeight(screen, toybox::kBodyFont);

  std::string stage = bambu::stageLabel(s.stage());
  if (s.printError != 0 && s.stage() != bambu::Stage::Finished) stage += "  /  ERROR";
  line(screen, fui::makeRect(x, y, width, small), stage, toybox::kTileFont);
  const std::string lay = layers(s);
  if (!lay.empty() && printing(s)) {
    line(screen, fui::makeRect(x, y, width, small), lay, toybox::kTileFont, fui::TextAlign::Right);
  }
  y = static_cast<int16_t>(y + small + 4);
  const std::string job = ascii(s.job);
  line(screen, fui::makeRect(x, y, width, body), job.empty() ? std::string("No plate") : job, toybox::kBodyFont);
  y = static_cast<int16_t>(y + body + toybox::kGutter);

  // The hero on the left, time left and the finish on the right.
  const int16_t heroWidth = static_cast<int16_t>(width / 2);
  setHero(hero, fui::makeRect(x, y, heroWidth, heroHeight), s);
  const std::string left = timeLeft(s);
  const int16_t rightX = static_cast<int16_t>(x + heroWidth);
  const int16_t rightW = static_cast<int16_t>(width - heroWidth);
  const int16_t mid = static_cast<int16_t>(y + heroHeight / 2);
  if (!left.empty()) {
    line(screen, fui::makeRect(rightX, static_cast<int16_t>(mid - body), rightW, body), left, toybox::kBodyFont,
         fui::TextAlign::Right);
  }
  if (model.endsAt[0] != '\0' && printing(s)) {
    line(screen, fui::makeRect(rightX, static_cast<int16_t>(mid + 2), rightW, small), model.endsAt, toybox::kTileFont,
         fui::TextAlign::Right);
  }
  y = static_cast<int16_t>(y + heroHeight + 6);
  bar(screen, fui::makeRect(x, y, width, 26), s.percent, toybox::kRule);
  return static_cast<int16_t>(y + 26);
}

void rule(toybox::Screen& screen, const int16_t y, const int16_t thickness = 1) {
  screen.target().fill(
      fui::makeRect(toybox::kMargin, y, static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin), thickness),
      kInk);
}

// The external spool, as one line, when one is loaded.
int16_t externalLine(toybox::Screen& screen, const PrinterState& s, int16_t y, const int16_t bottom) {
  if (!s.hasExternal || !s.external.loaded) return y;
  const int16_t width = static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin);
  const int16_t body = lineHeight(screen, toybox::kBodyFont);
  if (y + body > bottom) return y;
  const int16_t sw = body;
  swatch(screen, fui::makeRect(toybox::kMargin, y, sw, sw), s.external);
  const int16_t labelW = 56;
  slotLabel(screen, fui::makeRect(static_cast<int16_t>(toybox::kMargin + sw + 8), y, labelW, body), "EXT",
            isActive(s, -1, 0));
  const int16_t textX = static_cast<int16_t>(toybox::kMargin + sw + 8 + labelW + 8);
  line(screen, fui::makeRect(textX, y, static_cast<int16_t>(width - (textX - toybox::kMargin)), body),
       material(s.external) + "  " + colorText(s.external), toybox::kBodyFont);
  return static_cast<int16_t>(y + body + toybox::kGutter);
}

// How big each unit's picture can be so `units` of them fit in `room`, and how
// many fit at all: as many as the floor allows, each as large as the ceiling.
int16_t fitSize(const int16_t room, const int total, const int16_t fixed, const int16_t gap, const int16_t floor,
                const int16_t ceiling, int& shown) {
  for (shown = total; shown > 0; --shown) {
    const int16_t size = static_cast<int16_t>(room / shown - gap - fixed);
    if (size >= floor) return size > ceiling ? ceiling : size;
  }
  shown = 0;
  return floor;
}

int16_t externalHeight(toybox::Screen& screen, const PrinterState& s) {
  if (!s.hasExternal || !s.external.loaded) return 0;
  return static_cast<int16_t>(lineHeight(screen, toybox::kBodyFont) + toybox::kGutter);
}

void moreUnits(toybox::Screen& screen, const int drawn, const int total, const int16_t y, const int16_t bottom) {
  if (drawn >= total) return;
  const int16_t small = lineHeight(screen, toybox::kTileFont);
  if (y + small > bottom) return;
  char buf[48];
  std::snprintf(buf, sizeof(buf), "+%d MORE AMS NOT SHOWN", total - drawn);
  line(screen,
       fui::makeRect(toybox::kMargin, y, static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin), small), buf,
       toybox::kTileFont);
}

// --- The body: the print as one big number, each AMS as a row of spools. -----

Hero monitorBody(toybox::Screen& screen, const MonitorModel& model) {
  const PrinterState& s = *model.state;
  Hero hero;
  int16_t y = progressBlock(screen, model, toybox::kBodyTop, 76, hero);
  y = static_cast<int16_t>(y + toybox::kGutter);
  const int16_t width = static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin);
  const int16_t small = lineHeight(screen, toybox::kTileFont);
  // Temperatures as one line, because the spools want the room.
  std::string temps =
      "NOZZLE " + pair(s.nozzleTenths, s.nozzleTargetTenths) + "   BED " + pair(s.bedTenths, s.bedTargetTenths);
  if (s.hasChamber) temps += "   CHAMBER " + bambu::wholeDegrees(s.chamberTenths);
  line(screen, fui::makeRect(toybox::kMargin, y, width, small), temps, toybox::kTileFont);
  y = static_cast<int16_t>(y + small + toybox::kGutter);
  const int16_t bottom = static_cast<int16_t>(linkTop(screen, model) - toybox::kGutter);
  const int16_t column = static_cast<int16_t>(width / 4);
  int shown = 0;
  const int16_t diameter = fitSize(static_cast<int16_t>(bottom - y - externalHeight(screen, s)), s.amsCount,
                                   static_cast<int16_t>(6 + small + 8 + 8 + small * 3), toybox::kGutter, 48,
                                   static_cast<int16_t>(column - 16 > 92 ? 92 : column - 16), shown);
  const int16_t radius = static_cast<int16_t>(diameter / 2);
  int drawn = 0;
  for (int i = 0; i < shown; ++i) {
    rule(screen, y, 2);
    y = static_cast<int16_t>(y + 6);
    line(screen, fui::makeRect(toybox::kMargin, y, width, small), unitTitle(s.ams[i], i), toybox::kTileFont);
    y = static_cast<int16_t>(y + small + 8);
    for (int t = 0; t < bambu::kTraysPerAms; ++t) {
      const Tray& tray = s.ams[i].trays[t];
      const int16_t cx = static_cast<int16_t>(toybox::kMargin + column * t + column / 2);
      const int16_t cy = static_cast<int16_t>(y + radius);
      // A spool seen end on: the rim, the colour, and the hub.
      toybox::disc(screen, cx, cy, radius, kInk);
      toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 3),
                   tray.loaded && tray.hasColor ? inkFor(tray) : kPaper);
      toybox::disc(screen, cx, cy, static_cast<int16_t>(radius / 3 + 2), kInk);
      toybox::disc(screen, cx, cy, static_cast<int16_t>(radius / 3), kPaper);
      int16_t ty = static_cast<int16_t>(y + radius * 2 + 8);
      const int16_t colX = static_cast<int16_t>(toybox::kMargin + column * t);
      slotLabel(screen, fui::makeRect(static_cast<int16_t>(colX + 4), ty, static_cast<int16_t>(column - 8), small),
                bambu::slotName(i, t) + (tray.loaded ? "  " + remainText(tray) : std::string()), isActive(s, i, t));
      ty = static_cast<int16_t>(ty + small);
      line(screen, fui::makeRect(colX, ty, column, small), material(tray), toybox::kTileFont, fui::TextAlign::Center);
      ty = static_cast<int16_t>(ty + small);
      line(screen, fui::makeRect(colX, ty, column, small), colorText(tray), toybox::kTileFont, fui::TextAlign::Center);
    }
    y = static_cast<int16_t>(y + radius * 2 + 8 + small * 3 + toybox::kGutter);
    ++drawn;
  }
  y = externalLine(screen, s, y, bottom);
  moreUnits(screen, drawn, s.amsCount, y, bottom);
  return hero;
}

}  // namespace

// --- The monitor ---------------------------------------------------------------

Hero buildMonitor(toybox::Screen& screen, const MonitorModel& model) {
  chrome(screen, true);
  Hero hero;
  if (model.state == nullptr || !model.state->seen) {
    const fui::DeviceContext& device = screen.device();
    const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
    prose(screen,
          fui::makeRect(toybox::kMargin, toybox::kBodyTop, width,
                        static_cast<int16_t>(linkTop(screen, model) - toybox::kBodyTop - toybox::kGutter)),
          "Nothing from the printer yet. It answers within a few seconds once the reader is on the same Wi-Fi.",
          toybox::kBodyFont);
  } else {
    hero = monitorBody(screen, model);
  }
  linkLine(screen, model);
  return hero;
}

void drawHero(toybox::Screen& screen, const Hero& hero) {
  if (hero.rect.width <= 0 || hero.text[0] == '\0') return;
  screen.target().text(hero.rect, hero.text, plain(toybox::kDisplayFont));
}

// --- Setting up ---------------------------------------------------------------------

void buildSetup(toybox::Screen& screen) {
  chrome(screen, true);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;
  int16_t y = toybox::kBodyTop;
  const int16_t small = lineHeight(screen, toybox::kTileFont);
  line(screen, fui::makeRect(toybox::kMargin, y, width, small), "WATCH YOUR PRINTER FROM HERE", toybox::kTileFont);
  y = static_cast<int16_t>(y + small + toybox::kGutter);
  y = static_cast<int16_t>(y + prose(screen,
                                     fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(footer.y - y)),
                                     "It reads the printer over your Wi-Fi, the way Bambu Studio does in LAN mode, or "
                                     "through your Bambu account when you are away.",
                                     toybox::kBodyFont));
  y = static_cast<int16_t>(y + toybox::kGutter * 2);
  // Where each of the three things is, on the printer's own screen.
  static constexpr const char* kWhere[][2] = {
      {"IP ADDRESS AND ACCESS CODE", "Settings > Network, or WLAN on the A1."},
      {"SERIAL NUMBER", "Settings > Device, or the sticker on the back."},
      {"AWAY FROM HOME", "Sign in to your Bambu account on the same page as well."},
      {"THEN", "Tap below and point your phone's camera at the code to type them in."},
  };
  for (const auto& where : kWhere) {
    if (y + small * 2 > footer.y - toybox::kGutter) break;
    line(screen, fui::makeRect(toybox::kMargin, y, width, small), where[0], toybox::kTileFont);
    y = static_cast<int16_t>(y + small + 2);
    y = static_cast<int16_t>(
        y + prose(screen,
                  fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(footer.y - toybox::kGutter - y)),
                  where[1], toybox::kBodyFont));
    y = static_cast<int16_t>(y + toybox::kGutter * 2);
  }
  footerButton(screen, footer, "SET UP ON PHONE", ActionUsePhone);
}

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, false);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;
  const int16_t small = lineHeight(screen, toybox::kTileFont);
  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop + toybox::kGutter), width, small);
  line(screen, caption, "POINT YOUR PHONE CAMERA HERE", toybox::kTileFont, fui::TextAlign::Center);

  const int16_t top = static_cast<int16_t>(caption.y + caption.height + toybox::kGutter * 2);
  const int16_t room = static_cast<int16_t>(footer.y - toybox::kGutter * 2 - top - small * 5);
  int16_t side = room < width ? room : width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), top, side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), width, small);
  line(screen, url, model.readable, toybox::kTileFont, fui::TextAlign::Center);
  const fui::Rect state =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, small);
  line(screen, state,
       model.busy    ? "TALKING TO BAMBU, ONE MOMENT"
       : model.saved ? "SAVED FROM YOUR PHONE"
                     : "WAITING FOR YOUR PHONE",
       toybox::kTileFont, fui::TextAlign::Center);

  footerButton(screen, footer, "DONE", ActionDismiss);
  return qr;
}

void buildNotice(toybox::Screen& screen, const char* text) {
  chrome(screen, false);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  prose(screen,
        fui::makeRect(footer.x, toybox::kBodyTop, footer.width,
                      static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop)),
        text, toybox::kBodyFont);
  footerButton(screen, footer, "BACK", ActionDismiss);
}

}  // namespace bambuui
