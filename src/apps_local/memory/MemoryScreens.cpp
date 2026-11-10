#include "MemoryScreens.h"

#include <cstdio>
#include <cstring>

#include "../link/LinkScreens.h"
#include "MemoryFaces.h"

namespace memoryui {
namespace {

namespace mem = memory;

constexpr int16_t kMargin = 16;
constexpr int16_t kGap = 12;
constexpr int16_t kStripH = 76;
constexpr int16_t kNewsH = 44;

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);
const fui::Paint kPaper = fui::Paint::solid(fui::Color::White);

// One picture per pair, in deal order. Eighteen, for the 6 x 6 board.
const freeink::Icon* const kFaces[mem::kMaxPairs] = {
    &icon_star_48,   &icon_heart_48, &icon_moon_48,  &icon_sun_48,  &icon_cloud_48,  &icon_umbrella_48,
    &icon_anchor_48, &icon_bell_48,  &icon_key_48,   &icon_leaf_48, &icon_fish_48,   &icon_bird_48,
    &icon_flower_48, &icon_music_48, &icon_apple_48, &icon_cat_48,  &icon_rocket_48, &icon_snowflake_48,
};

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;  // named even when it is the slot the component defaults to
  style.align = align;
  style.color = colour;
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

int16_t textWidth(toybox::Screen& screen, const fui::FontId font, const char* text) {
  return screen.target().measureText(font, text, textStyle(font, fui::TextAlign::Left)).width;
}

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
}

fui::Rect inner(toybox::Screen& screen) {
  const fui::Rect body = screen.body();
  return rect(body.x + kMargin, body.y, body.width - 2 * kMargin, body.height - kMargin);
}

void button(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::ActionId action,
            const bool primary) {
  if (primary) {
    screen.target().fill(box, kInk, 8);
  } else {
    screen.target().stroke(box, kInk, 2, 8);
  }
  label(screen, box, text, toybox::kSmallFont, fui::TextAlign::Center, primary ? fui::Color::White : fui::Color::Black);
  screen.frame().hit(box, action);
}

// --- Cards ---------------------------------------------------------------

void picture(toybox::Screen& screen, const fui::Rect& card, const int face, const fui::Color colour) {
  if (face < 0 || face >= mem::kMaxPairs) return;
  const int16_t side = card.width < 56 || card.height < 56 ? 40 : 48;
  const fui::Rect box = rect(card.x + (card.width - side) / 2, card.y + (card.height - side) / 2, side, side);
  screen.target().bitmap(box, fui::bitmapFromIcon(*kFaces[face]), fui::BitmapMode::Contain, fui::Paint::solid(colour));
}

// The back every card shares: dark dither inside a paper rim. Dithered
// rather than solid, because the board repaints every turn and solid black
// that changes is what ghosts; dark, so what is still in play outweighs the
// open cards that have been taken.
void back(toybox::Screen& screen, const fui::Rect& card) {
  screen.target().fill(card, fui::Paint::dither(fui::Color::DarkGray), 8);
  screen.target().stroke(card.inset(fui::Insets{5, 5, 5, 5}), kPaper, 2, 5);
  screen.target().stroke(card, kInk, 2, 8);
}

void ownerBadge(toybox::Screen& screen, const fui::Rect& card, const char* text) {
  const fui::Rect tab = rect(card.right() - 24, card.y + 4, 20, 22);
  screen.target().fill(tab, kInk, 4);
  label(screen, tab, text, toybox::kSmallFont, fui::TextAlign::Center, fui::Color::White);
}

// A taken card stays on the table face up and unframed, so it recedes behind
// the cards still in play. The pairs taken in the turn just ended are
// bracketed, so whoever plays next can see what changed.
void taken(toybox::Screen& screen, const fui::Rect& card, const int face, const bool fresh, const char* badge) {
  picture(screen, card, face, fui::Color::Black);
  if (fresh) toybox::bracket(screen, card, 14, 4);
  if (badge != nullptr) ownerBadge(screen, card, badge);
}

void faceUp(toybox::Screen& screen, const fui::Rect& card, const int face) {
  screen.target().fill(card, kPaper, 8);
  screen.target().stroke(card, kInk, 4, 8);
  picture(screen, card, face, fui::Color::Black);
}

// --- The score strip -----------------------------------------------------

void recallText(const mem::Game& g, const int seat, char* out, const size_t capacity) {
  const int chances = mem::chancesOf(g, seat);
  if (chances == 0) {
    std::snprintf(out, capacity, "RECALL -");
  } else {
    std::snprintf(out, capacity, "RECALL %d/%d", g.hits[seat], chances);
  }
}

void soloStrip(toybox::Screen& screen, const fui::Rect& box, const mem::Game& g) {
  char pairs[24];
  std::snprintf(pairs, sizeof(pairs), "PAIRS %d/%d", g.pairs[0], mem::pairCount(g));
  char recall[24];
  recallText(g, 0, recall, sizeof(recall));
  const int half = box.width / 2;
  label(screen, rect(box.x, box.y, half, box.height), pairs, toybox::kBodyFont, fui::TextAlign::Left);
  label(screen, rect(box.x + half, box.y, box.width - half, box.height), recall, toybox::kBodyFont,
        fui::TextAlign::Right);
}

void seatBox(toybox::Screen& screen, const fui::Rect& box, const mem::Game& g, const int seat, const char* name,
             const bool active) {
  screen.target().stroke(box, kInk, static_cast<uint8_t>(active ? 4 : 1), 8);
  // A tab on the seat to play, so whose turn it is reads from across a table.
  if (active) screen.target().fill(rect(box.x + 8, box.y + 10, 6, box.height - 20), kInk);
  const fui::Rect text = rect(box.x + 22, box.y + 6, box.width - 34, box.height - 12);
  const int16_t half = static_cast<int16_t>(text.height / 2);
  label(screen, rect(text.x, text.y, text.width, half), name, toybox::kBodyFont, fui::TextAlign::Left);
  char pairs[16];
  std::snprintf(pairs, sizeof(pairs), "%d", g.pairs[seat]);
  label(screen, rect(text.x, text.y, text.width, half), pairs, toybox::kBodyFont, fui::TextAlign::Right);
  char recall[24];
  recallText(g, seat, recall, sizeof(recall));
  label(screen, rect(text.x, text.y + half, text.width, text.height - half), recall, toybox::kSmallFont,
        fui::TextAlign::Left);
}

void strip(toybox::Screen& screen, const fui::Rect& box, const BoardModel& model) {
  const mem::Game& g = *model.game;
  if (g.players != 2) {
    soloStrip(screen, box, g);
    return;
  }
  const int w = (box.width - kGap) / 2;
  for (int s = 0; s < mem::kSeats; ++s) {
    seatBox(screen, rect(box.x + s * (w + kGap), box.y, w, box.height), g, s, model.names[s], !g.over && g.turn == s);
  }
}

// --- The chart -----------------------------------------------------------

void dashed(toybox::Screen& screen, const int x0, const int x1, const int y) {
  for (int x = x0; x < x1; x += 12) {
    const int w = x + 7 > x1 ? x1 - x : 7;
    screen.target().fill(rect(x, y - 1, w, 3), kInk);
  }
}

int heightOf(const int v, const int plotH) {
  const int clamped = v > 100 ? 100 : (v < 0 ? 0 : v);
  const int h = clamped * plotH / 100;
  return h < 3 ? 3 : h;
}

// Fourteen days of best recall, today solid and the fortnight's average as a
// dashed line, the same chart Math Quiz draws.
void chart(toybox::Screen& screen, const fui::Rect& area, const mem::Window& w) {
  const int today = mem::kWindowDays - 1;
  label(screen, rect(area.x, area.y, area.width, 30), "BEST RECALL", toybox::kDisplayFont, fui::TextAlign::Left);

  // The legend: today's figure beside a solid swatch, the fortnight's beside a
  // dash, so it is the key to the plot under it.
  const fui::Rect row = rect(area.x, area.y + 36, area.width, 26);
  int right = row.right();
  char text[24];
  if (w.hasAverage) {
    std::snprintf(text, sizeof(text), "2 WEEKS %d%%", w.average);
    const int16_t tw = textWidth(screen, toybox::kSmallFont, text);
    label(screen, rect(right - tw, row.y, tw, row.height), text, toybox::kSmallFont, fui::TextAlign::Left);
    right -= tw + 8;
    dashed(screen, right - 22, right, row.y + row.height / 2);
    right -= 22 + 18;
  }
  if (w.days[today].has) {
    std::snprintf(text, sizeof(text), "TODAY %d%%", w.days[today].best);
    const int16_t tw = textWidth(screen, toybox::kSmallFont, text);
    label(screen, rect(right - tw, row.y, tw, row.height), text, toybox::kSmallFont, fui::TextAlign::Left);
    right -= tw + 8;
    screen.target().fill(rect(right - 14, row.y + row.height / 2 - 7, 14, 14), kInk);
  }

  constexpr int kHead = 36 + 26 + 22;
  const fui::Rect plot = rect(area.x, area.y + kHead, area.width, area.height - kHead - 32);
  for (int x = plot.x; x < plot.right(); x += 6) screen.target().fill(rect(x, plot.y, 2, 1), kInk);
  label(screen, rect(plot.x, plot.y - 26, plot.width / 3, 24), "100%", toybox::kSmallFont, fui::TextAlign::Left);
  screen.target().fill(rect(plot.x, plot.bottom(), plot.width, 2), kInk);

  const int slot = plot.width / mem::kWindowDays;
  bool any = false;
  for (int i = 0; i < mem::kWindowDays; ++i) {
    const mem::ChartDay& d = w.days[i];
    if (!d.has) continue;
    any = true;
    const int h = heightOf(d.best, plot.height);
    const fui::Rect bar = rect(plot.x + i * slot + 5, plot.bottom() - h, slot - 10, h);
    if (i == today) {
      screen.target().fill(bar, kInk);
    } else {
      screen.target().fill(bar, fui::Paint::solid(fui::Color::DarkGray));
      screen.target().stroke(bar, kInk, 1);
    }
  }
  if (!any) {
    label(screen, rect(plot.x, plot.y, plot.width, plot.height), "NO GAMES ON THIS BOARD YET", toybox::kSmallFont);
  }
  if (w.hasAverage) dashed(screen, plot.x, plot.right(), plot.bottom() - heightOf(w.average, plot.height));

  static constexpr const char* kInitial[] = {"M", "T", "W", "T", "F", "S", "S"};
  for (int i = 0; i < mem::kWindowDays; ++i) {
    const fui::Rect box = rect(plot.x + i * slot, plot.bottom() + 4, slot, 26);
    const bool chip = i == today;
    if (chip) screen.target().fill(rect(box.x + 3, box.y, box.width - 6, box.height), kInk, 4);
    label(screen, box, kInitial[mem::weekdayOf(w.firstDay + i)], toybox::kSmallFont, fui::TextAlign::Center,
          chip ? fui::Color::White : fui::Color::Black);
  }
}

void noClock(toybox::Screen& screen, const fui::Rect& area) {
  fui::TextStyle prose = textStyle(toybox::kSmallFont, fui::TextAlign::Center);
  prose.maxLines = 4;
  screen.target().text(area, "THE CLOCK IS NOT SET, SO SCORES CANNOT BE KEPT BY DAY. CONNECT TO WIFI ONCE TO SET IT.",
                       prose);
}

}  // namespace

// --- Layout --------------------------------------------------------------

BoardLayout layoutBoard(const fui::Rect& area, const memory::Size size) {
  BoardLayout l;
  l.area = area;
  l.columns = memory::columnsOf(size);
  l.rows = memory::rowsOf(size);
  l.gap = static_cast<int16_t>(l.columns > 4 ? 8 : 12);
  l.cardW = static_cast<int16_t>((area.width - (l.columns - 1) * l.gap) / l.columns);
  l.cardH = static_cast<int16_t>((area.height - (l.rows - 1) * l.gap) / l.rows);
  // No taller than a playing card, so a small board does not draw planks.
  const int16_t tallest = static_cast<int16_t>(l.cardW * 7 / 5);
  if (l.cardH > tallest) l.cardH = tallest;
  const int usedW = l.columns * l.cardW + (l.columns - 1) * l.gap;
  const int usedH = l.rows * l.cardH + (l.rows - 1) * l.gap;
  l.left = static_cast<int16_t>(area.x + (area.width - usedW) / 2);
  l.top = static_cast<int16_t>(area.y + (area.height - usedH) / 2);
  return l;
}

fui::Rect BoardLayout::cardRect(const int index) const {
  const int c = index % columns;
  const int r = index / columns;
  return rect(left + c * (cardW + gap), top + r * (cardH + gap), cardW, cardH);
}

int BoardLayout::cardAt(const int x, const int y) const {
  if (columns <= 0 || rows <= 0) return -1;
  // The gaps belong to the nearer card: a thumb aimed at a card lands a little
  // off it as often as on it, and a tap that does nothing reads as a freeze.
  const int c = (x - left + gap / 2) / (cardW + gap);
  const int r = (y - top + gap / 2) / (cardH + gap);
  if (x < left - gap / 2 || y < top - gap / 2 || c < 0 || r < 0 || c >= columns || r >= rows) return -1;
  return r * columns + c;
}

// --- Screens -------------------------------------------------------------

BoardLayout buildBoard(toybox::Screen& screen, const BoardModel& model) {
  const mem::Game& g = *model.game;
  char title[32];
  if (g.players != 2) {
    std::snprintf(title, sizeof(title), "MEMORY");
  } else if (model.seat >= 0) {
    std::snprintf(title, sizeof(title), "%s", g.turn == model.seat ? "YOUR TURN" : "THEIR TURN");
  } else {
    std::snprintf(title, sizeof(title), "%s TO PLAY", model.names[g.turn]);
  }
  chrome(screen, title);
  const fui::Rect area = inner(screen);

  const fui::Rect stripBox = rect(area.x, area.y, area.width, kStripH);
  const fui::Rect newsBox = rect(area.x, area.bottom() - kNewsH, area.width, kNewsH);
  const fui::Rect boardBox =
      rect(area.x, area.y + kStripH + kGap, area.width, area.height - kStripH - kNewsH - 2 * kGap);
  strip(screen, stripBox, model);

  const BoardLayout layout = layoutBoard(boardBox, static_cast<mem::Size>(g.size));
  const int n = mem::cardsOf(static_cast<mem::Size>(g.size));
  for (int i = 0; i < n; ++i) {
    const fui::Rect card = layout.cardRect(i);
    if (g.owner[i] != 0) {
      taken(screen, card, g.face[i], mem::isFresh(g, i), g.players == 2 ? model.badges[g.owner[i] - 1] : nullptr);
    } else if (mem::isUp(g, i)) {
      faceUp(screen, card, g.face[i]);
    } else {
      back(screen, card);
    }
  }
  if (!g.over && !model.waiting) screen.frame().hit(boardBox, ActionBoard);

  if (model.news != nullptr && *model.news != '\0') {
    label(screen, newsBox, model.news, toybox::kBodyFont, fui::TextAlign::Center);
  }
  return layout;
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "MEMORY");
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect content = screen.contentRect();

  // The board size, as three tiles: the chart below follows it.
  constexpr int16_t kTileH = 56;
  const int tileW = (content.width - 2 * kGap) / mem::kSizes;
  for (int s = 0; s < mem::kSizes; ++s) {
    const fui::Rect tile = rect(content.x + s * (tileW + kGap), content.y, tileW, kTileH);
    const bool on = s == model.size;
    if (on) {
      screen.target().fill(tile, kInk, 8);
    } else {
      screen.target().stroke(tile, kInk, 2, 8);
    }
    label(screen, tile, mem::sizeLabel(static_cast<mem::Size>(s)), toybox::kBodyFont, fui::TextAlign::Center,
          on ? fui::Color::White : fui::Color::Black);
    screen.frame().hit(tile, ActionSize, static_cast<int16_t>(s));
  }

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Solo)].label = "PLAY ALONE";
  rows[static_cast<int>(MenuRow::Pass)].label = "PASS AND PLAY";
  rows[static_cast<int>(MenuRow::Nearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::Nearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW IT SCORES";
  for (int i = 0; i < static_cast<int>(MenuRow::Count); ++i) rows[i].actionValue = static_cast<int16_t>(i);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = 0;
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::Nearby), 0, linkui::nearbyMark(), false);

  const int chartTop = content.y + kTileH + 28;
  const fui::Rect chartBox = rect(content.x, chartTop, content.width, listBand.y - chartTop - 24);
  if (model.window == nullptr) {
    noClock(screen, chartBox);
  } else {
    chart(screen, chartBox, *model.window);
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const mem::Game& g = *model.game;
  const bool two = g.players == 2;
  const int win = mem::winner(g);
  char title[32];
  if (!two) {
    std::snprintf(title, sizeof(title), "ALL PAIRS FOUND");
  } else if (win < 0) {
    std::snprintf(title, sizeof(title), "A DRAW");
  } else if (model.seat >= 0) {
    std::snprintf(title, sizeof(title), "%s", win == model.seat ? "YOU WIN" : "THEY WIN");
  } else {
    std::snprintf(title, sizeof(title), "%s WINS", model.names[win]);
  }
  chrome(screen, title);
  const fui::Rect area = inner(screen);
  int y = area.y + 8;

  // The recall of each seat this device can speak for, biggest first.
  const int seats = two ? 2 : 1;
  const int colW = (area.width - (seats - 1) * kGap) / seats;
  for (int s = 0; s < seats; ++s) {
    const int x = area.x + s * (colW + kGap);
    const int pct = mem::recallPercent(g, s);
    char big[16];
    if (pct < 0) {
      std::snprintf(big, sizeof(big), "-");
    } else {
      std::snprintf(big, sizeof(big), "%d%%", pct);
    }
    char line[40];
    if (two) {
      std::snprintf(line, sizeof(line), "%s: %d PAIRS", model.names[s], g.pairs[s]);
      label(screen, rect(x, y, colW, 30), line, toybox::kBodyFont, fui::TextAlign::Center);
    } else {
      label(screen, rect(x, y, colW, 30), "RECALL", toybox::kBodyFont, fui::TextAlign::Center);
    }
    label(screen, rect(x, y + 36, colW, 64), big, toybox::kDisplayFont, fui::TextAlign::Center);
    std::snprintf(line, sizeof(line), "%d HIT%s  %d MISS%s", g.hits[s], g.hits[s] == 1 ? "" : "S", g.misses[s],
                  g.misses[s] == 1 ? "" : "ES");
    label(screen, rect(x, y + 104, colW, 28), line, toybox::kSmallFont, fui::TextAlign::Center);
  }
  y += 148;

  const char* note = nullptr;
  if (model.charted) {
    note = "ON TODAY'S CHART";
  } else if (model.tooFew) {
    note = "TOO FEW RECALLS TO CHART";
  } else if (model.seat < 0) {
    note = "SHARED GAMES ARE NOT CHARTED";
  }
  if (note != nullptr) {
    label(screen, rect(area.x, y, area.width, 28), note, toybox::kSmallFont, fui::TextAlign::Center);
    y += 40;
  }
  screen.target().fill(rect(area.x, y, area.width, toybox::kRule), kInk);
  y += toybox::kRule + 24;

  const int buttonsTop = area.bottom() - 64;
  if (model.window != nullptr) chart(screen, rect(area.x, y, area.width, buttonsTop - y - 24), *model.window);

  const int half = (area.width - kGap) / 2;
  button(screen, rect(area.x, buttonsTop, half, 64), "MENU", ActionMenu, false);
  button(screen, rect(area.x + half + kGap, buttonsTop, area.width - half - kGap, 64), "PLAY AGAIN", ActionAgain, true);
}

void buildHowTo(toybox::Screen& screen) {
  chrome(screen, "HOW IT SCORES");
  const fui::Rect area = inner(screen);
  static constexpr const char* kLines[] = {
      "TURN TWO CARDS. A PAIR IS YOURS AND YOU GO AGAIN.",
      "TURNING CARDS BLIND COSTS NOTHING. NOBODY CAN REMEMBER A CARD THEY HAVE NOT SEEN.",
      "WHEN YOU TURN A CARD WHOSE PARTNER HAS ALREADY BEEN SEEN, THAT IS A RECALL. FIND THE PARTNER FOR A HIT; ANY "
      "OTHER CARD IS A MISS.",
      "YOUR RECALL IS HITS OUT OF RECALLS. THE CHART KEEPS EACH DAY'S BEST GAME OF THREE RECALLS OR MORE.",
  };
  fui::TextStyle prose = textStyle(toybox::kSmallFont, fui::TextAlign::Left);
  prose.maxLines = 5;
  int y = area.y + 8;
  for (const char* line : kLines) {
    screen.target().text(rect(area.x, y, area.width, 132), line, prose);
    y += 140;
  }
  button(screen, rect(area.x, area.bottom() - 64, area.width, 64), "GOT IT", ActionMenu, true);
}

}  // namespace memoryui
