#include "GCalScreens.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"

namespace gcalui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;
// The day column: weekday over the date, as Google's Schedule view has it.
constexpr int16_t kDateCol = 64;
// Inside a card, and between cards; a day starts a little further down.
constexpr int16_t kPadY = 8;
constexpr int16_t kPadX = 12;
constexpr int16_t kCardGap = 8;
constexpr int16_t kDayGap = 18;
constexpr uint8_t kRadius = 10;
constexpr uint8_t kCardStroke = 2;

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const fui::Color color = fui::Color::Black, const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  style.maxLines = maxLines;
  return style;
}

int16_t pageWidth(const fui::DeviceContext& device) { return static_cast<int16_t>(device.width - 2 * toybox::kMargin); }

fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       pageWidth(device), kFooterHeight);
}

// Header band, rule, and the page margin. `status` is paper on the black band.
void chrome(toybox::Screen& screen, const char* title, const char* status = nullptr,
            const freeink::Icon* trailing = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = status;
  header.borderEdges = fui::EdgesNone;
  if (status != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.font = toybox::kTileFont;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  if (trailing != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(*trailing);
    header.trailingAction = ActionSettings;
    header.trailingStyles = toybox::rowStyles();
  }
  fui::TextStyle titleStyle = screen.theme().titleText;
  const fui::Rect band = toybox::headerBandRect(screen);
  const std::string fitted =
      toybox::fittedTitle(screen.target(), title, toybox::headerTitleWidth(screen, band, header), titleStyle);
  header.title = fitted.c_str();
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined, const bool enabled = true) {
  fui::ButtonProps button;
  button.label = label;
  button.action = enabled ? action : fui::NO_ACTION;
  if (outlined) button.styles = toybox::rowStyles();
  // Dimmed rather than removed: a control that vanishes moves its neighbours.
  if (!enabled) button.styles = toybox::disabledButtonStyles();
  screen.button(button, box);
}

int16_t bodyH(const fui::DrawTarget& t) { return t.lineHeight(toybox::kBodyFont); }
int16_t tileH(const fui::DrawTarget& t) { return t.lineHeight(toybox::kTileFont); }
int16_t numberH(const fui::DrawTarget& t) { return t.lineHeight(toybox::kDisplayFont); }

// Weekday over the date number: what a day's first row needs beside it.
int16_t dateColumnH(const fui::DrawTarget& t) { return static_cast<int16_t>(tileH(t) + numberH(t)); }

int16_t spacingAbove(const gcal::Item& item) {
  if (item.kind == gcal::Item::Kind::Month) return kDayGap;
  return item.firstOfDay ? kDayGap : kCardGap;
}

int16_t contentHeight(const fui::DrawTarget& t, const gcal::Item& item) {
  switch (item.kind) {
    case gcal::Item::Kind::Month:
      return static_cast<int16_t>(numberH(t) + toybox::kGutter / 2 + toybox::kRule);
    case gcal::Item::Kind::Nothing:
      return dateColumnH(t);
    case gcal::Item::Kind::Event:
      break;
  }
  if (item.allDayRow) return static_cast<int16_t>(2 * kPadY + bodyH(t));
  return static_cast<int16_t>(2 * kPadY + bodyH(t) + tileH(t));
}

void drawDate(toybox::Screen& screen, const int16_t x, const int16_t y, const int64_t day, const bool today,
              const bool compact) {
  fui::DrawTarget& t = screen.target();
  const std::string number = gcal::dayNumber(day);
  if (compact) {
    // A day carried over from the page before: one small line, so it never
    // reaches into the next day's rows.
    const std::string label = std::string(gcal::weekdayShort(day)) + " " + number;
    t.text(fui::makeRect(x, static_cast<int16_t>(y + kPadY), kDateCol, tileH(t)), label.c_str(),
           plain(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray));
    return;
  }
  t.text(fui::makeRect(x, y, kDateCol, tileH(t)), gcal::weekdayShort(day),
         plain(toybox::kTileFont, fui::TextAlign::Center, today ? fui::Color::Black : fui::Color::DarkGray));
  const int16_t numberY = static_cast<int16_t>(y + tileH(t));
  fui::TextStyle style = plain(toybox::kDisplayFont, fui::TextAlign::Center);
  if (today) {
    // Round the ink, not the line box: the glyphs sit low in it.
    const fui::Rect ink = t.inkBounds(style.font, number.c_str(), style);
    const int16_t inkMid = ink.height > 0 ? static_cast<int16_t>(ink.y + ink.height / 2) : numberH(t) / 2;
    const int16_t r = static_cast<int16_t>(std::min<int>(kDateCol / 2 - 2, numberH(t) / 2 + 8));
    toybox::disc(screen, static_cast<int16_t>(x + kDateCol / 2), static_cast<int16_t>(numberY + inkMid), r,
                 fui::Color::Black);
    style.color = fui::Color::White;
  }
  t.text(fui::makeRect(x, numberY, kDateCol, numberH(t)), number.c_str(), style);
}

void drawEvent(toybox::Screen& screen, const fui::Rect& card, const gcal::Item& item, const gcal::Event& event) {
  fui::DrawTarget& t = screen.target();
  const int16_t textX = static_cast<int16_t>(card.x + kPadX);
  const int16_t textW = static_cast<int16_t>(card.width - 2 * kPadX);
  if (item.allDayRow) {
    // A solid bar, Google's all-day chip in ink. Its note ("DAY 2 / 3") sits
    // at the right end of the bar and the title takes what is left.
    t.fill(card, fui::Paint::solid(fui::Color::Black), kRadius);
    int16_t noteW = 0;
    if (!item.when.empty()) {
      const fui::TextStyle note = plain(toybox::kTileFont, fui::TextAlign::Right, fui::Color::White);
      noteW = static_cast<int16_t>(t.measureText(note.font, item.when.c_str(), note).width + toybox::kGutter);
      const int16_t y = static_cast<int16_t>(card.y + (card.height - tileH(t)) / 2);
      t.text(fui::makeRect(textX, y, textW, tileH(t)), item.when.c_str(), note);
    }
    const fui::TextStyle title = plain(toybox::kBodyFont, fui::TextAlign::Left, fui::Color::White);
    const int16_t titleW = static_cast<int16_t>(textW - noteW);
    const std::string line = toybox::fitLines(t, event.title.c_str(), titleW, 1, title);
    t.text(fui::makeRect(textX, static_cast<int16_t>(card.y + kPadY), titleW, bodyH(t)), line.c_str(), title);
    return;
  }
  t.stroke(card, fui::Paint::solid(fui::Color::Black), kCardStroke, kRadius);
  const fui::TextStyle title = plain(toybox::kBodyFont);
  const std::string line = toybox::fitLines(t, event.title.c_str(), textW, 1, title);
  t.text(fui::makeRect(textX, static_cast<int16_t>(card.y + kPadY), textW, bodyH(t)), line.c_str(), title);
  std::string detail = item.when;
  if (!event.location.empty()) detail += detail.empty() ? event.location : ", " + event.location;
  if (!detail.empty()) {
    const fui::TextStyle small = plain(toybox::kTileFont, fui::TextAlign::Left, fui::Color::DarkGray);
    const std::string cut = toybox::fitLines(t, detail.c_str(), textW, 1, small);
    t.text(fui::makeRect(textX, static_cast<int16_t>(card.y + kPadY + bodyH(t)), textW, tileH(t)), cut.c_str(), small);
  }
}

}  // namespace

// --- The schedule ------------------------------------------------------------

int pageHeight(const fui::DeviceContext& device, const bool asleep) {
  const int bottom = asleep ? device.height - toybox::kMargin : footerBand(device).y - toybox::kGutter;
  // The first row's spacing tucks into the gap the chrome already leaves.
  return bottom - kBodyTop + kDayGap;
}

std::vector<int> itemHeights(const fui::DrawTarget& target, const std::vector<gcal::Item>& items) {
  std::vector<int> heights;
  heights.reserve(items.size());
  const int dateH = dateColumnH(target);
  int dayStart = -1;  // index of the current day's first row
  int dayContent = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const gcal::Item& item = items[i];
    const int content = contentHeight(target, item);
    heights.push_back(spacingAbove(item) + content);
    if (item.kind == gcal::Item::Kind::Month) {
      dayStart = -1;
      continue;
    }
    if (item.firstOfDay) {
      dayStart = static_cast<int>(i);
      dayContent = content;
    } else {
      dayContent += kCardGap + content;
    }
    // A day shorter than its own date column (one all-day bar) is padded at
    // its last row, so the date never runs into the next day.
    const bool lastOfDay =
        i + 1 == items.size() || items[i + 1].kind == gcal::Item::Kind::Month || items[i + 1].firstOfDay;
    if (lastOfDay && dayStart >= 0 && dayContent < dateH) heights.back() += dateH - dayContent;
  }
  return heights;
}

void buildSchedule(toybox::Screen& screen, const ScheduleModel& model) {
  chrome(screen, model.title, model.status, model.asleep ? nullptr : model.settingsIcon);
  const fui::DeviceContext& device = screen.device();
  fui::DrawTarget& t = screen.target();
  const fui::Rect footer = footerBand(device);

  if (!model.asleep) {
    // REFRESH keeps the left edge, the fork-wide home of the primary action;
    // TODAY and the page arrows take the right, where a thumb pages the shelf.
    const int16_t arrow = static_cast<int16_t>(kFooterHeight + toybox::kGutter);
    const int16_t todayW = 112;
    const int16_t refreshW = static_cast<int16_t>(footer.width - todayW - 2 * arrow - 3 * toybox::kGutter);
    int16_t x = footer.x;
    footerButton(screen, fui::makeRect(x, footer.y, refreshW, footer.height), "REFRESH", ActionRefresh, false);
    x = static_cast<int16_t>(x + refreshW + toybox::kGutter);
    footerButton(screen, fui::makeRect(x, footer.y, todayW, footer.height), "TODAY", ActionToday, true,
                 model.canGoToday);
    x = static_cast<int16_t>(x + todayW + toybox::kGutter);
    footerButton(screen, fui::makeRect(x, footer.y, arrow, footer.height), "<", ActionPagePrev, true,
                 model.canPagePrev);
    x = static_cast<int16_t>(x + arrow + toybox::kGutter);
    footerButton(screen, fui::makeRect(x, footer.y, arrow, footer.height), ">", ActionPageNext, true,
                 model.canPageNext);
  }

  const int16_t left = toybox::kMargin;
  const int16_t width = pageWidth(device);
  if (model.count <= 0 || model.items == nullptr || model.heights == nullptr) {
    const int16_t headlineH = t.lineHeight(toybox::kDisplayFont);
    const int16_t proseH = t.lineHeight(toybox::kUiFont);
    const int16_t top = static_cast<int16_t>(kBodyTop + toybox::kMargin * 2);
    t.text(fui::makeRect(left, top, width, headlineH), model.emptyHeadline,
           plain(toybox::kDisplayFont, fui::TextAlign::Center));
    t.text(fui::makeRect(left, static_cast<int16_t>(top + headlineH + toybox::kGutter), width,
                         static_cast<int16_t>(proseH * 3)),
           model.emptyMessage, plain(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));
    return;
  }

  const int16_t cardX = static_cast<int16_t>(left + kDateCol + toybox::kGutter / 2);
  const int16_t cardW = static_cast<int16_t>(left + width - cardX);
  const int start = kBodyTop - kDayGap;
  const int bottom = start + pageHeight(device, model.asleep);
  int y = start;
  for (int i = 0; i < model.count; ++i) {
    const gcal::Item& item = model.items[i];
    const int16_t content = contentHeight(t, item);
    if (i > 0 && y + model.heights[i] > bottom) break;
    const int16_t top = static_cast<int16_t>(y + spacingAbove(item));
    switch (item.kind) {
      case gcal::Item::Kind::Month: {
        const std::string month = gcal::monthTitle(item.day);
        t.text(fui::makeRect(left, top, width, numberH(t)), month.c_str(), plain(toybox::kDisplayFont));
        t.fill(fui::makeRect(left, static_cast<int16_t>(top + numberH(t) + toybox::kGutter / 2), width, toybox::kRule),
               fui::Paint::solid(fui::Color::Black));
        break;
      }
      case gcal::Item::Kind::Nothing: {
        drawDate(screen, left, top, item.day, item.day == model.today, false);
        const fui::TextStyle note = plain(toybox::kUiFont, fui::TextAlign::Left, fui::Color::DarkGray);
        const int16_t noteH = t.lineHeight(note.font);
        t.text(fui::makeRect(static_cast<int16_t>(cardX + kPadX),
                             static_cast<int16_t>(top + tileH(t) + (numberH(t) - noteH) / 2), cardW, noteH),
               "Nothing planned", note);
        break;
      }
      case gcal::Item::Kind::Event: {
        if (item.firstOfDay || i == 0) {
          drawDate(screen, left, top, item.day, item.day == model.today, !item.firstOfDay);
        }
        if (item.event >= 0 && item.event < model.eventCount) {
          drawEvent(screen, fui::makeRect(cardX, top, cardW, content), item, model.events[item.event]);
        }
        break;
      }
    }
    y += model.heights[i];
  }
}

// --- Settings --------------------------------------------------------------

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "SETTINGS");
  const fui::DeviceContext& device = screen.device();
  footerButton(screen, footerBand(device), "BACK TO CALENDAR", ActionCloseSettings, false);

  fui::ListItem rows[3] = {};
  rows[0].label = "AUTO SYNC";
  rows[0].value = model.pollLabel;
  rows[0].actionValue = static_cast<int16_t>(SettingRow::Poll);
  rows[1].label = "SLEEP SCREEN";
  rows[1].value = model.sleepLabel;
  rows[1].actionValue = static_cast<int16_t>(SettingRow::Sleep);
  int count = 2;
  if (model.signedIn) {
    rows[2].label = "SIGN OUT";
    rows[2].actionValue = static_cast<int16_t>(SettingRow::SignOut);
    count = 3;
  }
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = -1;
  list.action = ActionSettingRow;
  screen.list(list);

  const fui::Rect footer = footerBand(device);
  const fui::TextStyle note = plain(toybox::kUiFont, fui::TextAlign::Left, fui::Color::DarkGray, 7);
  const int16_t h = static_cast<int16_t>(screen.target().lineHeight(note.font) * 7);
  const int16_t width = pageWidth(device);
  std::string text =
      "Auto sync checks Google this often while the app is open on the charger. Sleep screen shows today's "
      "schedule while the reader is off.";
  if (model.signedIn && model.account != nullptr && model.account[0] != '\0') {
    text += " Signed in as ";
    text += model.account;
    text += ".";
  }
  const std::string drawn = toybox::fitLines(screen.target(), text.c_str(), width, 7, note);
  screen.target().text(
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(footer.y - toybox::kGutter * 2 - h), width, h), drawn.c_str(),
      note);
}

// --- Sign out confirm ------------------------------------------------------

void buildSignOutConfirm(toybox::Screen& screen) {
  chrome(screen, "SIGN OUT?");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const char* body =
      "This signs the reader out of Google, for Tasks as well, and removes the calendar from the card. Signing in "
      "again takes your phone.";
  const fui::TextStyle prose = plain(toybox::kBodyFont, fui::TextAlign::Left, fui::Color::Black, 8);
  const int16_t h = static_cast<int16_t>(footerBand(device).y - toybox::kGutter * 2 - kBodyTop);
  const int lines = h / screen.target().lineHeight(prose.font);
  const std::string drawn = toybox::fitLines(screen.target(), body, width, lines < 1 ? 1 : lines, prose);
  screen.target().text(fui::makeRect(toybox::kMargin, kBodyTop, width, h), drawn.c_str(), prose);

  const fui::Rect footer = footerBand(device);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "KEEP IT", ActionKeepSignedIn, false);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
               "SIGN OUT", ActionSignOut, true);
}

}  // namespace gcalui
