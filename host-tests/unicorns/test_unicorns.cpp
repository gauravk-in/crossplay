// The Unicorns rulebook, checked without a panel.

#include <cstdio>

#include "UnicornCore.h"

using namespace unicorns;

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

int count(const Game& game, const uint8_t flag) {
  int total = 0;
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] & flag) ++total;
    }
  }
  return total;
}

// No open square without a unicorn may sit next to a covered, non-unicorn
// square while showing zero: that would mean the flood stopped early.
bool floodComplete(const Game& game) {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      const uint8_t cell = game.cell[column][row];
      if (!(cell & kOpen) || (cell & kUnicorn)) continue;
      if (neighbouringUnicorns(game, column, row) != 0) continue;
      for (int dc = -1; dc <= 1; ++dc) {
        for (int dr = -1; dr <= 1; ++dr) {
          const int nc = column + dc;
          const int nr = row + dr;
          if (!inside(nc, nr)) continue;
          const uint8_t next = game.cell[nc][nr];
          if (!(next & kOpen) && !(next & kHeart)) return false;
        }
      }
    }
  }
  return true;
}

void firstTapIsAlwaysSafeAndFloods() {
  for (uint32_t seed = 1; seed <= 3000; ++seed) {
    for (int column = 0; column < kColumns; column += 5) {
      for (int row = 0; row < kRows; row += 5) {
        Game game{};
        start(game, seed);
        CHECK(open(game, column, row));
        CHECK(game.status == Status::Playing || game.status == Status::Won);
        CHECK(count(game, kUnicorn) == kUnicorns);
        CHECK(neighbouringUnicorns(game, column, row) == 0);
        CHECK(count(game, kOpen) > 1);
        CHECK(floodComplete(game));
      }
    }
  }
}

void tappingAUnicornEndsTheRoundKindly() {
  Game game{};
  start(game, 7);
  open(game, 0, 0);
  int uc = -1;
  int ur = -1;
  for (int column = 0; column < kColumns && uc < 0; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] & kUnicorn) {
        uc = column;
        ur = row;
        break;
      }
    }
  }
  CHECK(uc >= 0);
  CHECK(open(game, uc, ur));
  CHECK(game.status == Status::Found);
  CHECK(over(game));
  // Nothing else moves once it is over.
  CHECK(!open(game, 0, 0));
  CHECK(!toggleHeart(game, uc, ur));
}

void openingEverySafeSquareWins() {
  Game game{};
  start(game, 42);
  open(game, 2, 2);
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (!(game.cell[column][row] & kUnicorn)) open(game, column, row);
    }
  }
  CHECK(game.status == Status::Won);
  CHECK(count(game, kOpen) == kCells - kUnicorns);
}

void heartsBlockOpeningAndCount() {
  Game game{};
  start(game, 9);
  CHECK(toggleHeart(game, 5, 5));
  CHECK(!open(game, 5, 5));
  CHECK(unicornsLeft(game) == kUnicorns - 1);
  CHECK(toggleHeart(game, 5, 5));
  CHECK(unicornsLeft(game) == kUnicorns);
  // More hearts than unicorns never shows a negative count.
  for (int column = 0; column < kColumns; ++column) toggleHeart(game, column, 0);
  CHECK(unicornsLeft(game) == 0);
}

}  // namespace

int main() {
  firstTapIsAlwaysSafeAndFloods();
  tappingAUnicornEndsTheRoundKindly();
  openingEverySafeSquareWins();
  heartsBlockOpeningAndCount();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
