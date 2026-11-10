#include "ChessPuzzleScreens.h"

#include <cstdio>
#include <cstring>

#include "../ui/ToyboxFormat.h"

namespace chesspuzui {
namespace {

constexpr int16_t kMargin = 16;
constexpr int16_t kFooterHeight = 96;
constexpr int16_t kGap = 12;
constexpr int16_t kBoardFrame = toybox::kBoardFrame;

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);
const fui::Paint kPaper = fui::Paint::solid(fui::Color::White);

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
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
  return toybox::kLargeCut;
}

// All-caps and digits only: safe to ink-centre.
void label(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
           const fui::TextAlign align = fui::TextAlign::Center, const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cutOf(font)), text, textStyle(font, align, colour));
}

int16_t textWidth(toybox::Screen& screen, const fui::FontId font, const char* text) {
  return screen.target().measureText(font, text, textStyle(font, fui::TextAlign::Left)).width;
}

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  if (rightLabel != nullptr && *rightLabel != '\0') {
    const fui::Rect box = toybox::headerInkRect(screen).inset(fui::Insets{0, kMargin, 0, 0});
    label(screen, box, rightLabel, toybox::kSmallFont, fui::TextAlign::Right, fui::Color::White);
  }
}

int16_t footerTop(toybox::Screen& screen) {
  const fui::Rect body = screen.body();
  return static_cast<int16_t>(body.y + body.height - kFooterHeight);
}

fui::Rect inner(toybox::Screen& screen) {
  const fui::Rect body = screen.body();
  return rect(body.x + kMargin, body.y, body.width - 2 * kMargin, body.height);
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
            const bool primary, const bool enabled = true) {
  if (primary) {
    screen.target().fill(box, kInk, 8);
  } else if (enabled) {
    screen.target().stroke(box, kInk, 2, 8);
  } else {
    // There is no grey ink, and leaving the button out would move its
    // neighbours under the finger.
    dotted(screen, box);
  }
  label(screen, box, text, toybox::kSmallFont, fui::TextAlign::Center, primary ? fui::Color::White : fui::Color::Black);
  if (enabled) screen.frame().hit(box, action);
}

const char* plural(const int n) { return n == 1 ? "" : "S"; }

struct Status {
  char headline[24] = {};
  char detail[48] = {};
};

Status statusFor(const PuzzleModel& m) {
  Status s;
  const char* side = m.playerWhite ? "WHITE" : "BLACK";
  const char* other = m.playerWhite ? "BLACK" : "WHITE";
  switch (m.state) {
    case chesspuz::State::Playing:
      if (m.movesMade == 0) {
        std::snprintf(s.headline, sizeof(s.headline), "%s TO MOVE", side);
        std::snprintf(s.detail, sizeof(s.detail), "FIND MATE IN %d", m.mateIn);
      } else {
        std::snprintf(s.headline, sizeof(s.headline), "YOUR MOVE");
        std::snprintf(s.detail, sizeof(s.detail), "MOVE %d OF %d", m.movesMade + 1, m.mateIn);
      }
      break;
    case chesspuz::State::Reply:
      std::snprintf(s.headline, sizeof(s.headline), "%s REPLIES", other);
      std::snprintf(s.detail, sizeof(s.detail), "MOVE %d OF %d", m.movesMade, m.mateIn);
      break;
    case chesspuz::State::Wrong:
      std::snprintf(s.headline, sizeof(s.headline), "NOT THE MOVE");
      std::snprintf(s.detail, sizeof(s.detail), "UNDO IT AND TRY ANOTHER");
      break;
    case chesspuz::State::NoMate:
      std::snprintf(s.headline, sizeof(s.headline), "NO MATE");
      std::snprintf(s.detail, sizeof(s.detail), "UNDO AND LOOK AGAIN");
      break;
    case chesspuz::State::Solved:
      std::snprintf(s.headline, sizeof(s.headline), "CHECKMATE!");
      if (m.hintsUsed == 0 && m.undosUsed == 0) {
        std::snprintf(s.detail, sizeof(s.detail), "NO HINTS, NO UNDOS");
      } else {
        std::snprintf(s.detail, sizeof(s.detail), "%d HINT%s, %d UNDO%s", m.hintsUsed, plural(m.hintsUsed), m.undosUsed,
                      plural(m.undosUsed));
      }
      break;
  }
  return s;
}

// UNDO, HINT, SKIP while playing; SCORES and NEXT once it is solved.
void puzzleFooter(toybox::Screen& screen, const PuzzleModel& m) {
  const fui::Rect area = inner(screen);
  const int y = footerTop(screen) + 16;
  constexpr int kH = 64;
  if (m.state == chesspuz::State::Solved) {
    constexpr int kScoresW = 150;
    button(screen, rect(area.x, y, kScoresW, kH), "SCORES", ActionScores, false);
    button(screen, rect(area.x + kScoresW + kGap, y, area.width - kScoresW - kGap, kH), "NEXT PUZZLE", ActionNext,
           true);
    return;
  }
  const bool busy = m.thinking || m.state == chesspuz::State::Reply;
  const bool mustUndo = m.state == chesspuz::State::Wrong || m.state == chesspuz::State::NoMate;
  const int buttons = m.hintsOn ? 3 : 2;
  const int w = (area.width - kGap * (buttons - 1)) / buttons;
  int x = area.x;
  // Black when it is the only way on, so the next tap is obvious.
  button(screen, rect(x, y, w, kH), "UNDO", ActionUndo, mustUndo, m.canUndo && !busy);
  x += w + kGap;
  if (m.hintsOn) {
    button(screen, rect(x, y, w, kH), "HINT", ActionHint, false, m.canHint && !busy);
    x += w + kGap;
  }
  button(screen, rect(x, y, area.right() - x, kH), "SKIP", ActionSkip, false, !busy);
}

fui::Rect boardRect(toybox::Screen& screen, const int top) {
  const fui::Rect area = inner(screen);
  const int size = ((area.width - 2 * kBoardFrame) / 8) * 8;
  return rect(area.x + (area.width - size) / 2, top + kBoardFrame, size, size);
}

// A pip per move to find, filled as they are made, beside the side to move.
void movePips(toybox::Screen& screen, const fui::Rect& row, const PuzzleModel& m) {
  constexpr int kPip = 18;
  constexpr int kPipGap = 10;
  const int total = m.mateIn * kPip + (m.mateIn - 1) * kPipGap;
  int x = row.right() - total;
  const int y = row.y + (row.height - kPip) / 2;
  const int made = m.state == chesspuz::State::Solved ? m.mateIn : m.movesMade;
  for (int i = 0; i < m.mateIn; ++i) {
    const fui::Rect pip = rect(x, y, kPip, kPip);
    if (i < made) {
      screen.target().fill(pip, kInk, kPip / 2);
    } else {
      screen.target().stroke(pip, kInk, 2, kPip / 2);
    }
    x += kPip + kPipGap;
  }
  char text[24];
  std::snprintf(text, sizeof(text), "%s TO MATE", m.playerWhite ? "WHITE" : "BLACK");
  // A swatch in the side's colour: hollow for White, solid for Black.
  const fui::Rect swatch = rect(row.x, row.y + (row.height - 20) / 2, 20, 20);
  if (m.playerWhite) {
    screen.target().stroke(swatch, kInk, 3, 4);
  } else {
    screen.target().fill(swatch, kInk, 4);
  }
  label(screen, rect(row.x + 30, row.y, row.width - 30 - total, row.height), text, toybox::kSmallFont,
        fui::TextAlign::Left);
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "CHECKMATE", nullptr);
  const fui::Rect area = inner(screen);
  label(screen, rect(area.x, area.y + 20, area.width, 50), "FIND THE MATE", toybox::kDisplayFont);

  static constexpr const char* kWord[chesspuz::kLevels] = {"ONE MOVE", "TWO MOVES", "THREE MOVES"};
  constexpr int kTileH = 112;
  int y = area.y + 90;
  for (int i = 0; i < chesspuz::kLevels; ++i) {
    const fui::Rect tile = rect(area.x, y, area.width, kTileH);
    screen.target().stroke(tile, kInk, 3, 10);
    // The numeral in a solid block at the left, the words beside it.
    const fui::Rect block = rect(tile.x, tile.y, 104, tile.height);
    screen.target().fill(block, kInk, 10);
    char n[12];
    std::snprintf(n, sizeof(n), "%d", i + 1);
    label(screen, block, n, toybox::kBodyFont, fui::TextAlign::Center, fui::Color::White);
    const fui::Rect words = rect(block.right() + 20, tile.y + 18, tile.width - block.width - 36, 40);
    char title[24];
    std::snprintf(title, sizeof(title), "MATE IN %d", i + 1);
    label(screen, words, title, toybox::kDisplayFont, fui::TextAlign::Left);
    char sub[40];
    if (model.count[i] > 0) {
      std::snprintf(sub, sizeof(sub), "%s  -  %d OF %d", kWord[i], model.next[i], model.count[i]);
    } else {
      std::snprintf(sub, sizeof(sub), "%s", kWord[i]);
    }
    label(screen, rect(words.x, tile.y + 64, words.width, 28), sub, toybox::kSmallFont, fui::TextAlign::Left);
    screen.frame().hit(tile, ActionLevel, static_cast<int16_t>(i + 1));
    y += kTileH + kGap;
  }

  // HINTS as a two-state switch: the label, then ON and OFF with the live one
  // solid.
  y += 8;
  const fui::Rect row = rect(area.x, y, area.width, 64);
  label(screen, rect(row.x, row.y, 150, row.height), "HINTS", toybox::kDisplayFont, fui::TextAlign::Left);
  const int sw = 108;
  const fui::Rect on = rect(row.right() - 2 * sw, row.y, sw, row.height);
  const fui::Rect off = rect(row.right() - sw, row.y, sw, row.height);
  screen.target().stroke(rect(on.x, on.y, 2 * sw, row.height), kInk, 2, 8);
  screen.target().fill(model.hintsOn ? on : off, kInk, 8);
  label(screen, on, "ON", toybox::kSmallFont, fui::TextAlign::Center,
        model.hintsOn ? fui::Color::White : fui::Color::Black);
  label(screen, off, "OFF", toybox::kSmallFont, fui::TextAlign::Center,
        model.hintsOn ? fui::Color::Black : fui::Color::White);
  screen.frame().hit(row, ActionHints);
  y += row.height + 6;
  label(screen, rect(area.x, y, area.width, 28), model.hintsOn ? "WRONG MOVES ARE MARKED" : "NO MARKS, NO HINTS",
        toybox::kSmallFont, fui::TextAlign::Left);

  const int top = footerTop(screen) + 16;
  char today[40];
  if (model.haveToday && model.todayPlayed > 0) {
    std::snprintf(today, sizeof(today), "TODAY %d/%d", model.todayClean, model.todayPlayed);
  } else {
    std::snprintf(today, sizeof(today), "MY SCORES");
  }
  button(screen, rect(area.x, top, area.width, 64), today, ActionScores, false);
  // The credit sits under the footer button, on every visit to the menu.
  label(screen, rect(area.x, top - 40, area.width, 26), "PUZZLES FROM LICHESS.ORG", toybox::kSmallFont);
}

fui::Rect buildPuzzle(toybox::Screen& screen, const PuzzleModel& m) {
  char title[24];
  std::snprintf(title, sizeof(title), "MATE IN %d", m.mateIn);
  char count[24];
  std::snprintf(count, sizeof(count), "%d/%d", m.number, m.count);
  chrome(screen, title, count);
  const fui::Rect area = inner(screen);
  const Status s = statusFor(m);

  movePips(screen, rect(area.x, area.y + 6, area.width, 32), m);
  const fui::Rect board = boardRect(screen, area.y + 46);
  // The status as a capsule: outlined while playing, solid at the end.
  const int capY = board.bottom() + kBoardFrame + 18;
  const fui::Rect cap = rect(area.x, capY, area.width, footerTop(screen) - capY - 4);
  const bool loud =
      m.state == chesspuz::State::Solved || m.state == chesspuz::State::Wrong || m.state == chesspuz::State::NoMate;
  if (loud) {
    screen.target().fill(cap, kInk, 12);
  } else {
    screen.target().stroke(cap, kInk, 3, 12);
  }
  const fui::Color ink = loud ? fui::Color::White : fui::Color::Black;
  label(screen, rect(cap.x, cap.y + 6, cap.width, cap.height / 2), s.headline, toybox::kDisplayFont,
        fui::TextAlign::Center, ink);
  label(screen, rect(cap.x, cap.y + cap.height / 2, cap.width, cap.height / 2 - 6), s.detail, toybox::kSmallFont,
        fui::TextAlign::Center, ink);

  puzzleFooter(screen, m);
  screen.frame().hit(board, ActionBoard);
  return board;
}

// --- Scores ----------------------------------------------------------------

namespace {

void dashed(toybox::Screen& screen, const int x0, const int x1, const int y) {
  for (int x = x0; x < x1; x += 12) {
    const int w = x + 7 > x1 ? x1 - x : 7;
    screen.target().fill(rect(x, y - 1, w, 3), kInk);
  }
}

void weekdayLabels(toybox::Screen& screen, const fui::Rect& plot, const int firstDay, const int highlight) {
  static constexpr const char* kInitial[] = {"M", "T", "W", "T", "F", "S", "S"};
  const int slot = plot.width / chesspuz::kWindowDays;
  for (int i = 0; i < chesspuz::kWindowDays; ++i) {
    const fui::Rect box = rect(plot.x + i * slot, plot.bottom() + 4, slot, 26);
    const bool chip = i == highlight;
    if (chip) screen.target().fill(rect(box.x + 3, box.y, box.width - 6, box.height), kInk, 4);
    label(screen, box, kInitial[chesspuz::weekdayOf(firstDay + i)], toybox::kSmallFont, fui::TextAlign::Center,
          chip ? fui::Color::White : fui::Color::Black);
  }
}

void scaleLine(toybox::Screen& screen, const fui::Rect& plot, const char* top) {
  for (int x = plot.x; x < plot.right(); x += 6) screen.target().fill(rect(x, plot.y, 2, 1), kInk);
  const int16_t topW = textWidth(screen, toybox::kSmallFont, top);
  screen.target().fill(rect(plot.right() - topW - 8, plot.y - 13, topW + 8, 26), kPaper);
  label(screen, rect(plot.right() - topW - 4, plot.y - 13, topW + 4, 26), top, toybox::kSmallFont,
        fui::TextAlign::Right);
  screen.target().fill(rect(plot.x, plot.bottom(), plot.width, 2), kInk);
}

int barHeight(const int v, const int top, const int plotH) {
  const int clamped = v > top ? top : v;
  const int h = top > 0 ? clamped * plotH / top : 0;
  return h < 3 ? 3 : h;
}

int highlightIndex(const chesspuz::Window& w) {
  for (int i = 0; i < chesspuz::kWindowDays; ++i) {
    if (w.days[i].day == w.highlight) return i;
  }
  return -1;
}

// The score: the share solved without a hint, today solid against the
// fortnight's dashed average.
void scoreChart(toybox::Screen& screen, const fui::Rect& area, const chesspuz::Window& w) {
  label(screen, rect(area.x, area.y, area.width, 34), "NO-HINT SOLVES", toybox::kDisplayFont, fui::TextAlign::Left);
  const int hi = highlightIndex(w);
  char text[24];
  int right = area.right();
  const fui::Rect row = rect(area.x, area.y + 40, area.width, 28);
  if (w.hasAverage) {
    std::snprintf(text, sizeof(text), "2 WEEKS %d%%", w.avgScore);
    const int16_t tw = textWidth(screen, toybox::kSmallFont, text);
    label(screen, rect(right - tw, row.y, tw, row.height), text, toybox::kSmallFont, fui::TextAlign::Left);
    right -= tw + 8;
    dashed(screen, right - 22, right, row.y + row.height / 2);
    right -= 22 + 18;
  }
  if (hi >= 0 && w.days[hi].has) {
    std::snprintf(text, sizeof(text), "TODAY %d%%", w.days[hi].score);
    const int16_t tw = textWidth(screen, toybox::kSmallFont, text);
    label(screen, rect(right - tw, row.y, tw, row.height), text, toybox::kSmallFont, fui::TextAlign::Left);
    right -= tw + 8;
    screen.target().fill(rect(right - 14, row.y + row.height / 2 - 7, 14, 14), kInk);
  }

  constexpr int kHead = 40 + 28 + 22;
  const fui::Rect plot = rect(area.x, area.y + kHead, area.width, area.height - kHead - 32);
  scaleLine(screen, plot, "100%");
  const int slot = plot.width / chesspuz::kWindowDays;
  bool any = false;
  for (int i = 0; i < chesspuz::kWindowDays; ++i) {
    const chesspuz::ChartDay& d = w.days[i];
    if (!d.has) continue;
    any = true;
    const int h = barHeight(d.score, 100, plot.height);
    const fui::Rect bar = rect(plot.x + i * slot + 5, plot.bottom() - h, slot - 10, h);
    if (i == hi) {
      screen.target().fill(bar, kInk);
    } else {
      screen.target().fill(bar, fui::Paint::solid(fui::Color::DarkGray));
      screen.target().stroke(bar, kInk, 1);
    }
  }
  if (!any) label(screen, plot, "NO PUZZLES THESE TWO WEEKS", toybox::kSmallFont);
  if (w.hasAverage) dashed(screen, plot.x, plot.right(), plot.bottom() - barHeight(w.avgScore, 100, plot.height));
  weekdayLabels(screen, plot, w.firstDay, hi);
}

// How many were played each day, the solved part filled.
void playedChart(toybox::Screen& screen, const fui::Rect& area, const chesspuz::Window& w) {
  label(screen, rect(area.x, area.y, area.width, 34), "PUZZLES PLAYED", toybox::kDisplayFont, fui::TextAlign::Left);
  const int hi = highlightIndex(w);
  const fui::Rect row = rect(area.x, area.y + 40, area.width, 28);
  // The key: an outline for played, a fill for solved.
  int x = row.right();
  const char* keys[2] = {"SOLVED", "PLAYED"};
  for (int k = 0; k < 2; ++k) {
    const int16_t tw = textWidth(screen, toybox::kSmallFont, keys[k]);
    label(screen, rect(x - tw, row.y, tw, row.height), keys[k], toybox::kSmallFont, fui::TextAlign::Left);
    x -= tw + 8;
    const fui::Rect sw = rect(x - 14, row.y + row.height / 2 - 7, 14, 14);
    if (k == 0) {
      screen.target().fill(sw, fui::Paint::solid(fui::Color::DarkGray));
      screen.target().stroke(sw, kInk, 1);
    } else {
      screen.target().stroke(sw, kInk, 2);
    }
    x -= 14 + 18;
  }

  constexpr int kHead = 40 + 28 + 22;
  const fui::Rect plot = rect(area.x, area.y + kHead, area.width, area.height - kHead - 32);
  int top = w.mostPlayed < 5 ? 5 : ((w.mostPlayed + 4) / 5) * 5;
  char topText[12];
  std::snprintf(topText, sizeof(topText), "%d", top);
  scaleLine(screen, plot, topText);
  const int slot = plot.width / chesspuz::kWindowDays;
  for (int i = 0; i < chesspuz::kWindowDays; ++i) {
    const chesspuz::ChartDay& d = w.days[i];
    if (!d.has) continue;
    const int h = barHeight(d.played, top, plot.height);
    const fui::Rect bar = rect(plot.x + i * slot + 5, plot.bottom() - h, slot - 10, h);
    screen.target().stroke(bar, kInk, 2);
    if (d.solved > 0) {
      const int sh = barHeight(d.solved, top, plot.height);
      const fui::Rect fill = rect(bar.x, plot.bottom() - sh, bar.width, sh);
      screen.target().fill(fill, i == hi ? kInk : fui::Paint::solid(fui::Color::DarkGray));
      screen.target().stroke(fill, kInk, 1);
    }
  }
  weekdayLabels(screen, plot, w.firstDay, hi);
}

void dateRange(const int firstDay, const int lastDay, char* out, const size_t capacity) {
  static constexpr const char* kMonth[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  int y0 = 0, m0 = 1, d0 = 1, y1 = 0, m1 = 1, d1 = 1;
  chesspuz::civilFromDays(firstDay, y0, m0, d0);
  chesspuz::civilFromDays(lastDay, y1, m1, d1);
  std::snprintf(out, capacity, "%d %s - %d %s", d0, kMonth[m0 - 1], d1, kMonth[m1 - 1]);
}

}  // namespace

void buildResults(toybox::Screen& screen, const ResultsModel& model) {
  const chesspuz::Window* w = model.window;
  chrome(screen, "MY SCORES", nullptr);
  const fui::Rect area = inner(screen);

  int y = area.y + 12;
  if (w != nullptr) {
    char range[32];
    dateRange(w->firstDay, w->lastDay, range, sizeof(range));
    label(screen, rect(area.x, y, area.width, 28), range, toybox::kSmallFont, fui::TextAlign::Left);
    y += 44;
  }

  if (w == nullptr) {
    fui::TextStyle prose = textStyle(toybox::kSmallFont, fui::TextAlign::Center);
    prose.maxLines = 4;
    screen.target().text(rect(area.x, y + 120, area.width, 120),
                         "THE CLOCK IS NOT SET, SO SCORES CANNOT BE KEPT BY DAY. CONNECT TO WIFI ONCE TO SET IT.",
                         prose);
  } else {
    const int chartH = (footerTop(screen) - y - 24) / 2;
    scoreChart(screen, rect(area.x, y, area.width, chartH), *w);
    playedChart(screen, rect(area.x, y + chartH + 24, area.width, chartH), *w);
  }

  const int top = footerTop(screen) + 16;
  constexpr int kPagerW = 112;
  button(screen, rect(area.x, top, kPagerW, 64), "OLDER", ActionOlder, false, w != nullptr && w->hasOlder);
  button(screen, rect(area.x + kPagerW + 10, top, kPagerW, 64), "NEWER", ActionNewer, false,
         w != nullptr && model.page > 0);
  const int px = area.x + 2 * (kPagerW + 10);
  button(screen, rect(px, top, area.right() - px, 64), "PLAY", ActionPlay, true);
}

}  // namespace chesspuzui
