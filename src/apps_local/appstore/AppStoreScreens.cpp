#include "AppStoreScreens.h"

#include <algorithm>
#include <cstdio>

namespace appstoreui {
namespace {

constexpr int16_t kTabH = 52;
constexpr int16_t kStatusH = 34;
constexpr int16_t kRowH = 78;
constexpr int16_t kRowGap = 10;
constexpr int16_t kBadgeW = 132;
constexpr int16_t kBadgeH = 40;
constexpr int16_t kFooterH = 56;
constexpr int16_t kGap = 12;
constexpr int16_t kPad = 14;

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black, const uint8_t lines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = colour;
  style.maxLines = lines;
  return style;
}

fui::Rect rect(const int x, const int y, const int w, const int h) {
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
}

const toybox::CutMetrics& cutOf(const fui::FontId font) {
  if (font == toybox::kSmallFont) return toybox::kButtonCut;
  if (font == toybox::kDisplayFont) return toybox::kDisplayCut;
  return toybox::kUiCut;
}

// All-caps and digits only: safe to ink-centre.
void label(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
           const fui::TextAlign align = fui::TextAlign::Center, const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cutOf(font)), text, textStyle(font, align, colour));
}

// Mixed-case text from a manifest: drawn in its line box, never ink-centred,
// so descenders stay inside.
void prose(toybox::Screen& screen, const fui::Rect& box, const std::string& text, const fui::FontId font,
           const uint8_t lines, const fui::TextAlign align = fui::TextAlign::Left) {
  screen.target().text(box, text.c_str(), textStyle(font, align, fui::Color::Black, lines));
}

void dotted(toybox::Screen& screen, const fui::Rect& box) {
  for (int x = box.x; x < box.right(); x += 6) {
    screen.target().fill(rect(x, box.y, 2, 2), kInk);
    screen.target().fill(rect(x, box.bottom() - 2, 2, 2), kInk);
  }
  for (int y = box.y; y < box.bottom(); y += 6) {
    screen.target().fill(rect(box.x, y, 2, 2), kInk);
    screen.target().fill(rect(box.right() - 2, y, 2, 2), kInk);
  }
}

void button(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::ActionId action,
            const bool primary, const int16_t value = 0) {
  if (primary) {
    screen.target().fill(box, kInk, 8);
  } else {
    screen.target().stroke(box, kInk, 2, 8);
  }
  label(screen, box, text, toybox::kSmallFont, fui::TextAlign::Center, primary ? fui::Color::White : fui::Color::Black);
  screen.frame().hit(box, action, value);
}

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
}

fui::Rect content(toybox::Screen& screen) {
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  return screen.contentRect();
}

}  // namespace

int buildList(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, "APP STORE");
  const fui::Rect area = content(screen);

  const char* tabs[2] = {"INSTALLED", "GET APPS"};
  const int tabW = (area.width - kGap) / 2;
  for (int t = 0; t < 2; ++t) {
    const fui::Rect tile = rect(area.x + t * (tabW + kGap), area.y, tabW, kTabH);
    const bool on = t == model.tab;
    if (on) {
      screen.target().fill(tile, kInk, 8);
    } else {
      screen.target().stroke(tile, kInk, 2, 8);
    }
    label(screen, tile, tabs[t], toybox::kSmallFont, fui::TextAlign::Center, on ? fui::Color::White : fui::Color::Black);
    screen.frame().hit(tile, ActionTab, static_cast<int16_t>(t));
  }

  int y = area.y + kTabH;
  if (!model.status.empty()) {
    prose(screen, rect(area.x, y + 4, area.width, kStatusH - 4), model.status, toybox::kSmallFont, 1);
  }
  y += kStatusH;

  const int count = model.rows == nullptr ? 0 : static_cast<int>(model.rows->size());
  const int listBottom = area.bottom() - kFooterH - kGap;
  const int perPage = std::max(1, (listBottom - y + kRowGap) / (kRowH + kRowGap));
  const int pages = std::max(1, (count + perPage - 1) / perPage);
  const int page = std::min(std::max(model.page, 0), pages - 1);

  if (count == 0) {
    prose(screen, rect(area.x, y + 40, area.width, 200), model.empty, toybox::kBodyFont, 6, fui::TextAlign::Center);
  }
  for (int i = page * perPage; i < count && i < (page + 1) * perPage; ++i) {
    const Row& row = (*model.rows)[i];
    const fui::Rect box = rect(area.x, y, area.width, kRowH);
    screen.target().stroke(box, kInk, 2, 8);
    const int textW = area.width - kBadgeW - 3 * kPad;
    prose(screen, rect(box.x + kPad, box.y + 6, textW, 40), row.title, toybox::kBodyFont, 1);
    prose(screen, rect(box.x + kPad, box.y + 44, textW, 30), row.detail, toybox::kSmallFont, 1);
    if (!row.badge.empty()) {
      const fui::Rect badge = rect(box.right() - kPad - kBadgeW, box.y + (kRowH - kBadgeH) / 2, kBadgeW, kBadgeH);
      if (row.dim) {
        dotted(screen, badge);
        label(screen, badge, row.badge.c_str(), toybox::kSmallFont);
      } else {
        screen.target().fill(badge, kInk, 8);
        label(screen, badge, row.badge.c_str(), toybox::kSmallFont, fui::TextAlign::Center, fui::Color::White);
      }
    }
    screen.frame().hit(box, ActionRow, static_cast<int16_t>(i));
    y += kRowH + kRowGap;
  }

  if (pages > 1) {
    const fui::Rect footer = rect(area.x, area.bottom() - kFooterH, area.width, kFooterH);
    const int w = (footer.width - 2 * kGap) / 3;
    if (page > 0) button(screen, rect(footer.x, footer.y, w, kFooterH), "OLDER", ActionOlder, false);
    char text[16];
    std::snprintf(text, sizeof(text), "%d / %d", page + 1, pages);
    label(screen, rect(footer.x + w + kGap, footer.y, w, kFooterH), text, toybox::kSmallFont);
    if (page + 1 < pages) {
      button(screen, rect(footer.right() - w, footer.y, w, kFooterH), "NEWER", ActionNewer, false);
    }
  }
  return pages;
}

void buildDetail(toybox::Screen& screen, const DetailModel& model) {
  chrome(screen, model.title.c_str());
  const fui::Rect area = content(screen);
  int y = area.y;

  if (!model.summary.empty()) {
    prose(screen, rect(area.x, y, area.width, 120), model.summary, toybox::kBodyFont, 4);
    y += 132;
  }

  constexpr int kFactH = 40;
  for (const auto& fact : model.facts) {
    const fui::Rect line = rect(area.x, y, area.width, kFactH);
    label(screen, line, fact.first.c_str(), toybox::kSmallFont, fui::TextAlign::Left);
    prose(screen, rect(area.x + area.width / 3, y + 4, area.width - area.width / 3, kFactH - 4), fact.second,
          toybox::kSmallFont, 1, fui::TextAlign::Right);
    screen.target().fill(rect(area.x, line.bottom() - 1, area.width, 1), kInk);
    y += kFactH;
  }

  if (!model.warning.empty()) {
    y += kGap;
    const fui::Rect box = rect(area.x, y, area.width, 104);
    screen.target().stroke(box, kInk, 3, 8);
    prose(screen, box.inset(fui::Insets{8, kPad, 8, kPad}), model.warning, toybox::kSmallFont, 3);
  }

  const fui::Rect footer = rect(area.x, area.bottom() - kFooterH, area.width, kFooterH);
  if (model.primary != nullptr && model.secondary != nullptr) {
    const int w = (footer.width - kGap) / 2;
    button(screen, rect(footer.x, footer.y, w, kFooterH), model.secondary, ActionSecondary, false);
    button(screen, rect(footer.right() - w, footer.y, w, kFooterH), model.primary, ActionPrimary, true);
  } else if (model.primary != nullptr) {
    button(screen, footer, model.primary, ActionPrimary, true);
  } else if (model.secondary != nullptr) {
    button(screen, footer, model.secondary, ActionSecondary, false);
  }
}

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, model.title.c_str());
  const fui::Rect area = content(screen);
  prose(screen, rect(area.x, area.y + 40, area.width, 240), model.message, toybox::kBodyFont, 7,
        fui::TextAlign::Center);
  if (model.progress >= 0) {
    const fui::Rect bar = rect(area.x + 20, area.y + 300, area.width - 40, 28);
    screen.target().stroke(bar, kInk, 2, 6);
    const int fill = (bar.width - 8) * std::min(model.progress, 100) / 100;
    if (fill > 0) screen.target().fill(rect(bar.x + 4, bar.y + 4, fill, bar.height - 8), kInk, 4);
  }
  if (model.button != nullptr) {
    button(screen, rect(area.x, area.bottom() - kFooterH, area.width, kFooterH), model.button, ActionPrimary, true);
  }
}

}  // namespace appstoreui
