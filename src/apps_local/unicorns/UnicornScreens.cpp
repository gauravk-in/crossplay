#include "UnicornScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxIcons.h"
#include "UnicornArt.h"

namespace unicornui {

namespace {

namespace uc = unicorns;

// 80px: six of them are exactly the panel's 480, edge to edge.
constexpr int16_t kCell = 80;
constexpr int16_t kBoardWidth = kCell * uc::kColumns;
constexpr int16_t kBoardHeight = kCell * uc::kRows;

int16_t boardTop() { return static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter + toybox::kRule); }

// An icon centred in `where`, scaled to `side`. Whole multiples of the art's
// own size keep the pixel art crisp.
void drawIcon(toybox::Screen& screen, const fui::Rect& where, const freeink::Icon& icon, const int16_t side,
              const bool paper) {
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(where.x + (where.width - side) / 2),
                                      static_cast<int16_t>(where.y + (where.height - side) / 2), side, side);
  screen.target().bitmap(box, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain,
                         fui::Paint::solid(paper ? fui::Color::White : fui::Color::Black));
}

void drawUnicorn(toybox::Screen& screen, const fui::Rect& where, const int16_t side, const bool paper) {
  drawIcon(screen, where, art_unicorn, side, paper);
}

void drawHeart(toybox::Screen& screen, const fui::Rect& where, const int16_t side, const bool paper) {
  drawIcon(screen, where, icon_hearts_32, side, paper);
}

// The count on an open square, as dice dots rather than a numeral: a child
// who cannot read numbers yet can still count dots, and most five year olds
// already know dice faces by sight. Seven and eight extend the six with the
// centre and the middle column.
void drawClue(toybox::Screen& screen, const fui::Rect& box, const int count) {
  static const uint8_t kPips[9][8] = {
      {0},
      {4},
      {0, 8},
      {0, 4, 8},
      {0, 2, 6, 8},
      {0, 2, 4, 6, 8},
      {0, 2, 3, 5, 6, 8},
      {0, 2, 3, 4, 5, 6, 8},
      {0, 1, 2, 3, 5, 6, 7, 8},
  };
  if (count < 1 || count > 8) return;
  const int16_t step = static_cast<int16_t>(box.width / 4);
  const int16_t radius = static_cast<int16_t>(box.width / 9);
  for (int i = 0; i < count; ++i) {
    const int slot = kPips[count][i];
    const int16_t cx = static_cast<int16_t>(box.x + step * (1 + slot % 3));
    const int16_t cy = static_cast<int16_t>(box.y + step * (1 + slot / 3));
    toybox::disc(screen, cx, cy, radius, fui::Color::Black);
  }
}

void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, whose default is black on the black
  // band; see jaipur's toyboxChrome.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A row of stars earned, centred on `line`. Past ten it is ten stars and the
// count, because a row of thirty stars no longer fits and no longer counts.
void starRow(toybox::Screen& screen, const fui::Rect& line, const int stars) {
  constexpr int kMaxDrawn = 10;
  constexpr int16_t kStar = 32;
  constexpr int16_t kStep = 40;
  const int drawn = stars > kMaxDrawn ? kMaxDrawn : stars;
  char more[16] = "";
  if (stars > kMaxDrawn) std::snprintf(more, sizeof(more), "%d", stars);
  const int16_t moreWidth = more[0] != '\0' ? 64 : 0;
  const int16_t width = static_cast<int16_t>(drawn * kStep - (kStep - kStar) + moreWidth);
  int16_t x = static_cast<int16_t>(line.x + (line.width - width) / 2);
  for (int i = 0; i < drawn; ++i) {
    drawIcon(screen, fui::makeRect(x, line.y, kStar, line.height), art_star, kStar, false);
    x = static_cast<int16_t>(x + kStep);
  }
  if (more[0] != '\0') {
    fui::TextStyle count;
    count.font = toybox::kDisplayFont;
    count.align = fui::TextAlign::Center;
    screen.target().text(toybox::inkCentred(fui::makeRect(x, line.y, moreWidth, line.height), toybox::kDisplayCut),
                         more, count);
  }
}

// A small picture of squares from a string: '.' covered, 'o' open, 'U' a
// unicorn peeking out, 'H' a heart. Counts are computed from the picture's own
// unicorns ('U' and 'H' both hide one), so the lesson cannot disagree with the
// rule.
void lessonField(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell,
                 const char* const* rows, const int columns, const int rowCount) {
  const auto unicornAt = [&](const int c, const int r) {
    if (c < 0 || c >= columns || r < 0 || r >= rowCount) return false;
    return rows[r][c] == 'U' || rows[r][c] == 'H';
  };
  for (int r = 0; r < rowCount; ++r) {
    for (int c = 0; c < columns; ++c) {
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(left + c * cell), static_cast<int16_t>(top + r * cell), cell, cell);
      const char mark = rows[r][c];
      if (mark == '.' || mark == 'H') screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
      if (mark == 'H') {
        drawHeart(screen, box, static_cast<int16_t>(cell / 2), false);
      } else if (mark == 'U') {
        drawUnicorn(screen, box, static_cast<int16_t>(cell - 16), false);
      } else if (mark == 'o') {
        int touching = 0;
        for (int dc = -1; dc <= 1; ++dc) {
          for (int dr = -1; dr <= 1; ++dr) {
            if ((dc != 0 || dr != 0) && unicornAt(c + dc, r + dr)) ++touching;
          }
        }
        drawClue(screen, box, touching);
      }
    }
  }
}

const char* const kLessonLines[] = {
    "UNICORNS ARE HIDING UNDER THE SQUARES. TAP A SQUARE TO PEEK!",
    "THE DOTS SAY HOW MANY UNICORNS ARE HIDING NEXT TO IT.",
    "THINK ONE IS HIDING THERE? TAP HEART, THEN THE SQUARE.",
    "OPEN EVERY SQUARE WITHOUT A UNICORN TO WIN A STAR!",
};

void lessonDiagram(toybox::Screen& screen, const int page, const int16_t bandTop, const int16_t bandBottom) {
  static const char* const kPeek[] = {"...", ".o.", "..."};
  static const char* const kCount[] = {"oU", "oo"};
  static const char* const kHeart[] = {"oo", "oH"};
  static const char* const kWin[] = {"oooo", "oUoo", "oooo"};

  const char* const* rows = kPeek;
  int columns = 3;
  int rowCount = 3;
  int16_t cell = 88;
  if (page == 1) {
    rows = kCount;
    columns = 2;
    rowCount = 2;
    cell = 100;
  } else if (page == 2) {
    rows = kHeart;
    columns = 2;
    rowCount = 2;
    cell = 100;
  } else if (page == 3) {
    rows = kWin;
    columns = 4;
    rowCount = 3;
    cell = 80;
  }
  const fui::DeviceContext device = screen.device();
  const int16_t width = static_cast<int16_t>(cell * columns);
  const int16_t height = static_cast<int16_t>(cell * rowCount);
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  const int16_t room = static_cast<int16_t>(bandBottom - bandTop);
  const int16_t top = static_cast<int16_t>(bandTop + (room > height ? (room - height) / 2 : 0));
  lessonField(screen, left, top, cell, rows, columns, rowCount);
}

// Under the board: a unicorn for each one still hiding, a heart for each heart
// placed, so the count reads without a numeral. Then the two tools, side by
// side, the one in use filled black.
void boardStrip(toybox::Screen& screen, const BoardModel& model) {
  const fui::Rect tools = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t half = static_cast<int16_t>((tools.width - toybox::kGutter) / 2);
  const fui::Rect peekRect = fui::makeRect(tools.x, tools.y, half, tools.height);
  const fui::Rect heartRect = fui::makeRect(static_cast<int16_t>(tools.right() - half), tools.y, half, tools.height);

  // One action, and the value names the tool, so tapping the one already in use
  // changes nothing rather than switching to the other by surprise.
  fui::ButtonProps peek;
  peek.label = "PEEK";
  peek.action = ActionTool;
  peek.value = 0;
  peek.styles = toybox::rowStyles();
  peek.state = model.heartMode ? fui::StateNormal : fui::StateSelected;
  screen.button(peek, peekRect);

  fui::ButtonProps heart;
  heart.label = "HEART";
  heart.action = ActionTool;
  heart.value = 1;
  heart.styles = toybox::rowStyles();
  heart.state = model.heartMode ? fui::StateSelected : fui::StateNormal;
  heart.icon = fui::bitmapFromIcon(icon_hearts_32);
  heart.iconSize = 28;
  screen.button(heart, heartRect);

  const fui::Rect counter = screen.takeBottom(64, toybox::kGutter);
  const int hearts = uc::heartCount(model.game);
  constexpr int16_t kIcon = 64;
  constexpr int16_t kStep = 76;
  const int16_t width = static_cast<int16_t>(uc::kUnicorns * kStep - (kStep - kIcon));
  int16_t x = static_cast<int16_t>(counter.x + (counter.width - width) / 2);
  for (int i = 0; i < uc::kUnicorns; ++i) {
    const fui::Rect slot = fui::makeRect(x, counter.y, kIcon, counter.height);
    if (i < hearts) {
      drawHeart(screen, slot, 48, false);
    } else {
      drawUnicorn(screen, slot, kIcon, false);
    }
    x = static_cast<int16_t>(x + kStep);
  }
}

}  // namespace

fui::Rect cellRect(const fui::DeviceContext& device, const int column, const int row) {
  const int16_t left = static_cast<int16_t>((device.width - kBoardWidth) / 2);
  return fui::makeRect(static_cast<int16_t>(left + column * kCell), static_cast<int16_t>(boardTop() + row * kCell),
                       kCell, kCell);
}

bool cellAt(const fui::DeviceContext& device, const int x, const int y, int& column, int& row) {
  const int16_t left = static_cast<int16_t>((device.width - kBoardWidth) / 2);
  const int dx = x - left;
  const int dy = y - boardTop();
  if (dx < 0 || dy < 0 || dx >= kBoardWidth || dy >= kBoardHeight) return false;
  column = dx / kCell;
  row = dy / kCell;
  return true;
}

int howToPages() { return static_cast<int>(sizeof(kLessonLines) / sizeof(kLessonLines[0])); }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "UNICORNS");

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);
  const int16_t listTop = static_cast<int16_t>(content.bottom() - listHeight);

  // The unicorn herself, as big as the room allows in whole multiples of her
  // 32px art, with the stars won so far underneath.
  constexpr int16_t kArt = 256;
  constexpr int16_t kStars = 40;
  const int16_t stackHeight = static_cast<int16_t>(kArt + toybox::kGutter * 2 + kStars);
  const int16_t room = static_cast<int16_t>(listTop - content.y);
  const int16_t top = static_cast<int16_t>(content.y + (room > stackHeight ? (room - stackHeight) / 2 : 0));
  drawUnicorn(screen, fui::makeRect(content.x, top, content.width, kArt), kArt, false);

  const fui::Rect starLine =
      fui::makeRect(content.x, static_cast<int16_t>(top + kArt + toybox::kGutter * 2), content.width, kStars);
  if (model.stars > 0) {
    starRow(screen, starLine, model.stars);
  } else {
    fui::TextStyle caption;
    caption.font = toybox::kBodyFont;
    caption.align = fui::TextAlign::Center;
    screen.target().text(starLine, "FIND THE HIDING UNICORNS", caption);
  }
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);

  fui::ButtonProps next;
  next.label = page + 1 < pages ? "NEXT" : "LET'S PLAY";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 4;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 150), kLessonLines[page], body);

  lessonDiagram(screen, page, static_cast<int16_t>(area.y + 160), area.bottom());
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "UNICORNS";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{0, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();
  const fui::Rect first = cellRect(device, 0, 0);
  screen.target().fill(fui::makeRect(0, static_cast<int16_t>(first.y - toybox::kRule), device.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.target().fill(fui::makeRect(0, static_cast<int16_t>(first.y + kBoardHeight), device.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  const bool over = uc::over(model.game);
  for (int column = 0; column < uc::kColumns; ++column) {
    for (int row = 0; row < uc::kRows; ++row) {
      const fui::Rect box = cellRect(device, column, row);
      const uint8_t cell = model.game.cell[column][row];
      const bool open = (cell & uc::kOpen) != 0;
      const bool unicorn = (cell & uc::kUnicorn) != 0;

      if (open && unicorn) {
        // The one that was found: drawn loudest, white on black.
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
        drawUnicorn(screen, box, 64, true);
        continue;
      }
      if (over && unicorn) {
        // Once the round ends every unicorn comes out to say hello, hearts or
        // not: where they were is the fun part to look at.
        screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
        drawUnicorn(screen, box, 64, false);
        continue;
      }
      if (!open) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
      if (cell & uc::kHeart) {
        drawHeart(screen, box, 48, false);
      } else if (open) {
        drawClue(screen, box, uc::neighbouringUnicorns(model.game, column, row));
      }
    }
  }

  if (model.holdColumn >= 0 && model.holdRow >= 0) {
    const fui::Rect box = cellRect(device, model.holdColumn, model.holdRow);
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 5);
  }

  if (over) {
    fui::ButtonProps verdict;
    verdict.label = model.game.status == uc::Status::Won ? "HOORAY! YOU DID IT" : "PEEKABOO! A UNICORN";
    verdict.action = ActionSeeResult;
    screen.button(verdict, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
    return;
  }

  boardStrip(screen, model);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  fui::HeaderProps header;
  header.title = model.won ? "HOORAY!" : "PEEKABOO!";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps done;
  done.label = "ALL DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  const fui::DeviceContext device = screen.device();

  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 2;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 70),
                       model.won ? "YOU FOUND EVERY SAFE SQUARE!" : "YOU FOUND A UNICORN. SHE SAYS HELLO!", body);

  // The picture: a big unicorn, framed by stars when she was never found.
  constexpr int16_t kArt = 224;
  const int16_t artTop = static_cast<int16_t>(area.y + 90);
  const fui::Rect art = fui::makeRect(area.x, artTop, area.width, kArt);
  drawUnicorn(screen, art, kArt, false);
  if (model.won) {
    const int16_t cx = static_cast<int16_t>(device.width / 2);
    drawIcon(screen, fui::makeRect(static_cast<int16_t>(cx - 190), artTop, 48, 48), art_star, 48, false);
    drawIcon(screen, fui::makeRect(static_cast<int16_t>(cx + 142), static_cast<int16_t>(artTop + 24), 48, 48), art_star,
             48, false);
    drawIcon(screen, fui::makeRect(static_cast<int16_t>(cx - 170), static_cast<int16_t>(artTop + 150), 32, 32),
             art_star, 32, false);
    drawIcon(screen, fui::makeRect(static_cast<int16_t>(cx + 150), static_cast<int16_t>(artTop + 170), 32, 32),
             art_star, 32, false);
  }

  const fui::Rect line =
      fui::makeRect(area.x, static_cast<int16_t>(artTop + kArt + toybox::kGutter * 2), area.width, 70);
  screen.target().text(line, model.won ? "YOU WON A STAR!" : "LET'S PLAY AGAIN!", body);
  if (model.won && model.stars > 0) {
    starRow(screen, fui::makeRect(area.x, static_cast<int16_t>(line.y + 64), area.width, 40), model.stars);
  }
}

}  // namespace unicornui
