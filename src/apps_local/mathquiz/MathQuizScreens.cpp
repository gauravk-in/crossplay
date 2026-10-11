#include "MathQuizScreens.h"

#include <cstdio>
#include <cstring>

#include "../ui/ToyboxFormat.h"

namespace mathquizui {
namespace {

constexpr int16_t kMargin = 16;
constexpr int16_t kFooterHeight = 96;
constexpr int16_t kGap = 16;
constexpr int16_t kAsideWidth = 132;

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);
const fui::Paint kPaper = fui::Paint::solid(fui::Color::White);

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
  // Drawn by hand rather than as header.rightLabel, which centres on its own
  // line box and so sits visibly apart from the title.
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

void button(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::ActionId action,
            const bool primary, const bool enabled = true) {
  if (primary) {
    screen.target().fill(box, kInk, 8);
  } else if (enabled) {
    screen.target().stroke(box, kInk, 2, 8);
  } else {
    // A dotted outline: there is no grey ink, and leaving the button out would
    // move its neighbours under the finger.
    for (int x = box.x; x < box.right(); x += 6) {
      screen.target().fill(rect(x, box.y, 2, 2), kInk);
      screen.target().fill(rect(x, box.bottom() - 2, 2, 2), kInk);
    }
    for (int y = box.y; y < box.bottom(); y += 6) {
      screen.target().fill(rect(box.x, y, 2, 2), kInk);
      screen.target().fill(rect(box.right() - 2, y, 2, 2), kInk);
    }
  }
  label(screen, box, text, toybox::kSmallFont, fui::TextAlign::Center, primary ? fui::Color::White : fui::Color::Black);
  if (enabled) screen.frame().hit(box, action);
}

// --- Sums ----------------------------------------------------------------

// The operators are shapes, sized from the digits beside them: the UI faces
// are ASCII, and a child should see the signs from the textbook.
bool isOperator(const char* token) { return token[1] == '\0' && std::strchr("+-*/=", token[0]) != nullptr; }

int16_t operatorWidth(const toybox::CutMetrics& cut) { return static_cast<int16_t>(cut.inkHeight * 3 / 4); }

void drawOperator(toybox::Screen& screen, const char op, const int cx, const int cy, const toybox::CutMetrics& cut,
                  const fui::Paint& paint) {
  const int half = operatorWidth(cut) / 2;
  const int t = cut.inkHeight / 9 < 2 ? 2 : cut.inkHeight / 9;  // stroke
  auto bar = [&](const int y) { screen.target().fill(rect(cx - half, y - t / 2, 2 * half, t), paint); };
  switch (op) {
    case '+':
      bar(cy);
      screen.target().fill(rect(cx - t / 2, cy - half, t, 2 * half), paint);
      break;
    case '-':
      bar(cy);
      break;
    case '=':
      bar(cy - half / 2 - t / 2);
      bar(cy + half / 2 + t / 2);
      break;
    case '*': {
      const int d = half * 4 / 5;
      screen.target().line(fui::Point{static_cast<int16_t>(cx - d), static_cast<int16_t>(cy - d)},
                           fui::Point{static_cast<int16_t>(cx + d), static_cast<int16_t>(cy + d)},
                           static_cast<uint8_t>(t + 1), paint);
      screen.target().line(fui::Point{static_cast<int16_t>(cx - d), static_cast<int16_t>(cy + d)},
                           fui::Point{static_cast<int16_t>(cx + d), static_cast<int16_t>(cy - d)},
                           static_cast<uint8_t>(t + 1), paint);
      break;
    }
    case '/': {
      bar(cy);
      const int dot = t + 2;
      screen.target().fill(rect(cx - dot / 2, cy - half, dot, dot), paint, static_cast<uint8_t>(dot / 2));
      screen.target().fill(rect(cx - dot / 2, cy + half - dot, dot, dot), paint, static_cast<uint8_t>(dot / 2));
      break;
    }
    default:
      break;
  }
}

// A sum such as "12 * 3 = ?", centred in `box` on one line.
void drawSum(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
             const fui::Color colour = fui::Color::Black) {
  constexpr int kMaxTokens = 9;
  char copy[mathquiz::kTextBytes];
  std::snprintf(copy, sizeof(copy), "%s", text);
  const char* token[kMaxTokens] = {};
  int count = 0;
  for (char* p = std::strtok(copy, " "); p != nullptr && count < kMaxTokens; p = std::strtok(nullptr, " ")) {
    token[count++] = p;
  }

  const toybox::CutMetrics& cut = cutOf(font);
  const fui::Paint paint = fui::Paint::solid(colour);
  const int16_t opW = operatorWidth(cut);
  const int16_t minusW = static_cast<int16_t>(opW * 2 / 3);
  int16_t width[kMaxTokens] = {};
  int total = 0;
  for (int i = 0; i < count; ++i) {
    if (isOperator(token[i])) {
      width[i] = opW;
    } else if (token[i][0] == '-') {
      // A negative number: a short drawn minus, then the digits.
      width[i] = static_cast<int16_t>(minusW + cut.inkHeight / 8 + textWidth(screen, font, token[i] + 1));
    } else {
      width[i] = textWidth(screen, font, token[i]);
    }
    total += width[i];
  }
  int gap = cut.inkHeight / 3;
  if (count > 1 && total + gap * (count - 1) > box.width) gap = (box.width - total) / (count - 1);
  if (gap < 2) gap = 2;
  total += gap * (count - 1);

  int x = box.x + (box.width - total) / 2;
  const int cy = box.y + box.height / 2;
  for (int i = 0; i < count; ++i) {
    if (isOperator(token[i])) {
      drawOperator(screen, token[i][0], x + width[i] / 2, cy, cut, paint);
    } else if (token[i][0] == '-') {
      const int t = cut.inkHeight / 9 < 2 ? 2 : cut.inkHeight / 9;
      screen.target().fill(rect(x, cy - t / 2, minusW, t), paint);
      const int16_t digits = static_cast<int16_t>(width[i] - minusW - cut.inkHeight / 8);
      label(screen, rect(x + width[i] - digits, box.y, digits, box.height), token[i] + 1, font, fui::TextAlign::Left,
            colour);
    } else {
      label(screen, rect(x, box.y, width[i], box.height), token[i], font, fui::TextAlign::Left, colour);
    }
    x += width[i] + gap;
  }
}

// --- Charts --------------------------------------------------------------

struct Series {
  const char* title = "";
  int value[mathquiz::kWindowDays] = {};
  bool has[mathquiz::kWindowDays] = {};
  int highlight = -1;  // index into value, -1 for none
  bool hasAverage = false;
  int average = 0;
  int top = 100;          // the value at the top of the plot
  bool tenths = false;    // values are tenths of a second
  const char* unit = "";  // after a value in the legend: "%" or " S"
};

void formatAmount(const Series& s, const int v, char* out, const size_t capacity) {
  char number[12];
  mathquiz::formatValue(v, s.tenths, number, sizeof(number));
  std::snprintf(out, capacity, "%s%s", number, s.unit);
}

void dashed(toybox::Screen& screen, const int x0, const int x1, const int y) {
  for (int x = x0; x < x1; x += 12) {
    const int w = x + 7 > x1 ? x1 - x : 7;
    screen.target().fill(rect(x, y - 1, w, 3), kInk);
  }
}

void weekdayLabels(toybox::Screen& screen, const fui::Rect& plot, const int firstDay, const int highlight) {
  static constexpr const char* kInitial[] = {"M", "T", "W", "T", "F", "S", "S"};
  const int slot = plot.width / mathquiz::kWindowDays;
  for (int i = 0; i < mathquiz::kWindowDays; ++i) {
    const fui::Rect box = rect(plot.x + i * slot, plot.bottom() + 4, slot, 26);
    const bool chip = i == highlight;
    if (chip) screen.target().fill(rect(box.x + 3, box.y, box.width - 6, box.height), kInk, 4);
    label(screen, box, kInitial[mathquiz::weekdayOf(firstDay + i)], toybox::kSmallFont, fui::TextAlign::Center,
          chip ? fui::Color::White : fui::Color::Black);
  }
}

// Title on the left, the comparison on the right: today's figure beside a
// solid swatch, the fortnight's beside a dash, so the legend is the key to
// the plot under it.
void legend(toybox::Screen& screen, const fui::Rect& row, const Series& s) {
  char text[24];
  int right = row.right();
  if (s.hasAverage) {
    char amount[16];
    formatAmount(s, s.average, amount, sizeof(amount));
    std::snprintf(text, sizeof(text), "2 WEEKS %s", amount);
    const int16_t w = textWidth(screen, toybox::kSmallFont, text);
    label(screen, rect(right - w, row.y, w, row.height), text, toybox::kSmallFont, fui::TextAlign::Left);
    right -= w + 8;
    dashed(screen, right - 22, right, row.y + row.height / 2);
    right -= 22 + 18;
  }
  if (s.highlight >= 0 && s.has[s.highlight]) {
    char amount[16];
    formatAmount(s, s.value[s.highlight], amount, sizeof(amount));
    std::snprintf(text, sizeof(text), "TODAY %s", amount);
    const int16_t w = textWidth(screen, toybox::kSmallFont, text);
    label(screen, rect(right - w, row.y, w, row.height), text, toybox::kSmallFont, fui::TextAlign::Left);
    right -= w + 8;
    screen.target().fill(rect(right - 14, row.y + row.height / 2 - 7, 14, 14), kInk);
  }
}

int heightOf(const Series& s, const int v, const int plotH) {
  const int clamped = v > s.top ? s.top : v;
  const int h = s.top > 0 ? clamped * plotH / s.top : 0;
  return h < 3 ? 3 : h;
}

void bars(toybox::Screen& screen, const fui::Rect& plot, const Series& s) {
  const int slot = plot.width / mathquiz::kWindowDays;
  for (int i = 0; i < mathquiz::kWindowDays; ++i) {
    if (!s.has[i]) continue;
    const int h = heightOf(s, s.value[i], plot.height);
    const fui::Rect bar = rect(plot.x + i * slot + 5, plot.bottom() - h, slot - 10, h);
    if (i == s.highlight) {
      screen.target().fill(bar, kInk);
    } else {
      screen.target().fill(bar, fui::Paint::solid(fui::Color::DarkGray));
      screen.target().stroke(bar, kInk, 1);
    }
  }
}

// One chart: title, legend, plot, weekday row.
void chart(toybox::Screen& screen, const fui::Rect& area, const Series& s, const int firstDay) {
  label(screen, rect(area.x, area.y, area.width, 34), s.title, toybox::kDisplayFont, fui::TextAlign::Left);
  legend(screen, rect(area.x, area.y + 40, area.width, 28), s);

  constexpr int kHead = 40 + 28 + 22;
  const fui::Rect plot = rect(area.x, area.y + kHead, area.width, area.height - kHead - 32);

  // The scale: a hairline at the top with its value, and the baseline.
  char top[16];
  formatAmount(s, s.top, top, sizeof(top));
  for (int x = plot.x; x < plot.right(); x += 6) screen.target().fill(rect(x, plot.y, 2, 1), kInk);
  const int16_t topW = textWidth(screen, toybox::kSmallFont, top);
  screen.target().fill(rect(plot.right() - topW - 8, plot.y - 13, topW + 8, 26), kPaper);
  label(screen, rect(plot.right() - topW - 4, plot.y - 13, topW + 4, 26), top, toybox::kSmallFont,
        fui::TextAlign::Right);
  screen.target().fill(rect(plot.x, plot.bottom(), plot.width, 2), kInk);

  bool any = false;
  for (int i = 0; i < mathquiz::kWindowDays; ++i) any = any || s.has[i];
  if (!any) {
    label(screen, rect(plot.x, plot.y, plot.width, plot.height), "NO PRACTICE THESE TWO WEEKS", toybox::kSmallFont);
  } else {
    bars(screen, plot, s);
  }
  if (s.hasAverage) {
    const int y = plot.bottom() - heightOf(s, s.average, plot.height);
    dashed(screen, plot.x, plot.right(), y);
  }
  weekdayLabels(screen, plot, firstDay, s.highlight);
}

void dateRange(const int firstDay, const int lastDay, char* out, const size_t capacity) {
  static constexpr const char* kMonth[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  int y0 = 0, m0 = 1, d0 = 1, y1 = 0, m1 = 1, d1 = 1;
  mathquiz::civilFromDays(firstDay, y0, m0, d0);
  mathquiz::civilFromDays(lastDay, y1, m1, d1);
  std::snprintf(out, capacity, "%d %s - %d %s", d0, kMonth[m0 - 1], d1, kMonth[m1 - 1]);
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "MATH QUIZ", nullptr);
  const fui::Rect area = inner(screen);
  label(screen, rect(area.x, area.y + 24, area.width, 56), "PICK A CLASS", toybox::kDisplayFont);

  constexpr int kTileH = 150;
  const int tileW = (area.width - kGap) / 2;
  const int top = area.y + 104;
  for (int g = 1; g <= mathquiz::kGrades; ++g) {
    const int r = (g - 1) / 2;
    const int c = (g - 1) % 2;
    const fui::Rect tile = rect(area.x + c * (tileW + kGap), top + r * (kTileH + kGap), tileW, kTileH);
    const bool last = g == model.grade;
    const fui::Color ink = last ? fui::Color::White : fui::Color::Black;
    if (last) {
      screen.target().fill(tile, kInk, 10);
    } else {
      screen.target().stroke(tile, kInk, 2, 10);
    }
    char number[12];
    std::snprintf(number, sizeof(number), "%d", g);
    label(screen, rect(tile.x, tile.y + 12, tile.width, 28), "CLASS", toybox::kSmallFont, fui::TextAlign::Center, ink);
    label(screen, rect(tile.x, tile.y + 42, tile.width, 64), number, toybox::kBodyFont, fui::TextAlign::Center, ink);
    if (model.sample[g - 1] != nullptr) {
      drawSum(screen, rect(tile.x + 8, tile.y + 108, tile.width - 16, 30), model.sample[g - 1], toybox::kSmallFont,
              ink);
    }
    screen.frame().hit(tile, ActionGrade, static_cast<int16_t>(g));
  }

  button(screen, rect(area.x, footerTop(screen) + 16, area.width, 64), "MY SCORES", ActionScores, false);
}

void buildQuestion(toybox::Screen& screen, const QuestionModel& model) {
  char title[20];
  std::snprintf(title, sizeof(title), "CLASS %d", model.grade);
  char score[toybox::kSlashCounterChars] = {};
  if (model.asked > 0) std::snprintf(score, sizeof(score), "%d/%d", model.right, model.asked);
  chrome(screen, title, model.asked > 0 ? score : nullptr);
  const fui::Rect area = inner(screen);
  const mathquiz::Question* q = model.question;
  if (q == nullptr) return;

  // The sum, in a box of its own so it reads as the thing being asked.
  const fui::Rect sumBox = rect(area.x, area.y + 36, area.width, 150);
  screen.target().stroke(sumBox, kInk, 3, 12);
  drawSum(screen, rect(sumBox.x + 12, sumBox.y, sumBox.width - 24, sumBox.height), q->text, toybox::kBodyFont);

  const bool answered = model.chosen >= 0;
  if (answered) {
    const bool right = model.chosen == q->correct;
    label(screen, rect(area.x, sumBox.bottom() + 24, area.width, 56), right ? "RIGHT!" : "NOT QUITE",
          toybox::kDisplayFont);
    char line[40];
    char seconds[12];
    mathquiz::formatValue(model.tenthsSec, true, seconds, sizeof(seconds));
    if (right) {
      std::snprintf(line, sizeof(line), "%s SECONDS", seconds);
    } else {
      char answer[12];
      mathquiz::formatValue(q->answer, q->tenths, answer, sizeof(answer));
      std::snprintf(line, sizeof(line), "IT IS %s", answer);
    }
    label(screen, rect(area.x, sumBox.bottom() + 80, area.width, 30), line, toybox::kSmallFont);
  }

  // Four answers, two by two, sitting just above the footer where a thumb is.
  constexpr int kTileH = 120;
  const int tileW = (area.width - kGap) / 2;
  const int top = footerTop(screen) - 2 * kTileH - kGap - 8;
  for (int i = 0; i < mathquiz::kChoices; ++i) {
    const fui::Rect tile = rect(area.x + (i % 2) * (tileW + kGap), top + (i / 2) * (kTileH + kGap), tileW, kTileH);
    const bool isCorrect = answered && i == q->correct;
    const bool isWrongPick = answered && i == model.chosen && !isCorrect;
    if (isCorrect) {
      screen.target().fill(tile, kInk, 10);
    } else {
      screen.target().stroke(tile, kInk, static_cast<uint8_t>(isWrongPick ? 5 : (answered ? 1 : 3)), 10);
    }
    char value[16];
    mathquiz::formatValue(q->choice[i], q->tenths, value, sizeof(value));
    drawSum(screen, tile, value, toybox::kBodyFont, isCorrect ? fui::Color::White : fui::Color::Black);
    if (isWrongPick) {
      // Struck through corner to corner: the pick was seen, and it was wrong.
      screen.target().line(fui::Point{static_cast<int16_t>(tile.x + 14), static_cast<int16_t>(tile.bottom() - 14)},
                           fui::Point{static_cast<int16_t>(tile.right() - 14), static_cast<int16_t>(tile.y + 14)}, 4,
                           kInk);
    }
    if (model.chosen == i) {
      // A tab on the child's own pick, so a right answer still says "you".
      screen.target().fill(rect(tile.x + 8, tile.y + 14, 7, tile.height - 28), isCorrect ? kPaper : kInk);
    }
    if (!answered) screen.frame().hit(tile, ActionChoice, static_cast<int16_t>(i));
  }

  const int y = footerTop(screen) + 16;
  const fui::Rect end = rect(area.right() - kAsideWidth, y, kAsideWidth, 64);
  if (answered) button(screen, rect(area.x, y, area.width - kAsideWidth - kGap, 64), "NEXT", ActionNext, true);
  button(screen, end, "END", ActionEnd, false);
}

void buildResults(toybox::Screen& screen, const ResultsModel& model) {
  const mathquiz::Window* w = model.window;
  char range[32] = {};
  if (w != nullptr) dateRange(w->firstDay, w->lastDay, range, sizeof(range));
  chrome(screen, "MY SCORES", nullptr);
  const fui::Rect area = inner(screen);

  int y = area.y + 12;
  if (w != nullptr) {
    label(screen, rect(area.x, y, area.width, 28), range, toybox::kSmallFont, fui::TextAlign::Left);
    y += 34;
  }
  if (model.asked > 0) {
    char seconds[12];
    mathquiz::formatValue(model.tenthsSec, true, seconds, sizeof(seconds));
    char line[72];
    std::snprintf(line, sizeof(line), "THIS ROUND: %d OF %d RIGHT, %s S EACH", model.right, model.asked, seconds);
    label(screen, rect(area.x, y, area.width, 28), line, toybox::kSmallFont, fui::TextAlign::Left);
    y += 34;
  }
  y += 10;

  if (w == nullptr) {
    fui::TextStyle prose = textStyle(toybox::kSmallFont, fui::TextAlign::Center);
    prose.maxLines = 4;
    screen.target().text(rect(area.x, y + 120, area.width, 120),
                         "THE CLOCK IS NOT SET, SO SCORES CANNOT BE KEPT BY DAY. CONNECT TO WIFI ONCE TO SET IT.",
                         prose);
  } else {
    Series score;
    score.title = "RIGHT ANSWERS";
    score.unit = "%";
    Series speed;
    speed.title = "SECONDS EACH";
    speed.unit = " S";
    speed.tenths = true;
    int slowest = 0;
    for (int i = 0; i < mathquiz::kWindowDays; ++i) {
      const mathquiz::ChartDay& d = w->days[i];
      score.has[i] = speed.has[i] = d.has;
      score.value[i] = d.percent;
      speed.value[i] = d.tenthsSec;
      if (d.has && d.tenthsSec > slowest) slowest = d.tenthsSec;
      if (d.day == w->highlight) score.highlight = speed.highlight = i;
    }
    score.hasAverage = speed.hasAverage = w->hasAverage;
    score.average = w->avgPercent;
    speed.average = w->avgTenthsSec;
    if (w->hasAverage && w->avgTenthsSec > slowest) slowest = w->avgTenthsSec;
    // Five-second steps, so the top line reads as a round number.
    speed.top = ((slowest + 49) / 50) * 50;
    if (speed.top < 50) speed.top = 50;

    const int chartH = (footerTop(screen) - y - 24) / 2;
    chart(screen, rect(area.x, y, area.width, chartH), score, w->firstDay);
    chart(screen, rect(area.x, y + chartH + 24, area.width, chartH), speed, w->firstDay);
  }

  // Older and newer page by a fortnight; the primary goes back to practising.
  const int top = footerTop(screen) + 16;
  constexpr int kPagerW = 112;
  button(screen, rect(area.x, top, kPagerW, 64), "OLDER", ActionOlder, false, w != nullptr && w->hasOlder);
  button(screen, rect(area.x + kPagerW + 10, top, kPagerW, 64), "NEWER", ActionNewer, false,
         w != nullptr && model.page > 0);
  const int px = area.x + 2 * (kPagerW + 10);
  button(screen, rect(px, top, area.right() - px, 64), "PRACTISE", ActionPractise, true);
}

}  // namespace mathquizui
