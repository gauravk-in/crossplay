#pragma once

// Unicorns, the rules. Minesweeper for a five year old: the mines are unicorns
// hiding under the squares, and finding one by tapping it is a peekaboo rather
// than an explosion. Freestanding: no renderer, no Activity, no storage.
//
// Six by six with five unicorns. Each square is 80px, the size of a small
// child's fingertip with room to miss, and the whole field fits on the panel
// without paging or scrolling. Five in thirty-six is about 14%, the density of
// the classic beginner board, and with the first tap's nine squares kept clear
// the opening flood usually uncovers a third of the field at once.
//
// Kept from Minesweeper because they are what make it fair for a beginner:
//
//   * **The first tap is always safe.** Unicorns are placed AFTER it, away from
//     that square and its neighbours, so the first move always opens a space.
//   * **A revealed zero opens its neighbours.** Nobody should have to tap every
//     empty square by hand.
//
// Left out on purpose: chording. It is a shortcut for players who already
// think in counts, and it is the one move that can end the round on a square
// the child never pointed at.

#include <cstdint>

namespace unicorns {

constexpr int kColumns = 6;
constexpr int kRows = 6;
constexpr int kUnicorns = 5;
constexpr int kCells = kColumns * kRows;

constexpr uint8_t kUnicorn = 1 << 0;
constexpr uint8_t kOpen = 1 << 1;
constexpr uint8_t kHeart = 1 << 2;

enum class Status : uint8_t {
  // Nothing tapped yet. Unicorns are placed by the first tap.
  Fresh,
  Playing,
  // Every square without a unicorn is open.
  Won,
  // A unicorn was tapped. Not a loss in the wording anywhere: the round just
  // ends with everybody saying hello.
  Found,
};

struct Game {
  uint8_t cell[kColumns][kRows];
  uint32_t rng;
  Status status;
};

inline uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

inline bool inside(const int column, const int row) {
  return column >= 0 && column < kColumns && row >= 0 && row < kRows;
}

inline bool has(const Game& game, const int column, const int row, const uint8_t flag) {
  return inside(column, row) && (game.cell[column][row] & flag) != 0;
}

inline void start(Game& game, const uint32_t seed) {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) game.cell[column][row] = 0;
  }
  game.rng = seed;
  game.status = Status::Fresh;
}

inline bool over(const Game& game) { return game.status == Status::Won || game.status == Status::Found; }

// How many unicorns touch a square, counting the eight neighbours.
inline int neighbouringUnicorns(const Game& game, const int column, const int row) {
  int count = 0;
  for (int dc = -1; dc <= 1; ++dc) {
    for (int dr = -1; dr <= 1; ++dr) {
      if (dc == 0 && dr == 0) continue;
      if (has(game, column + dc, row + dr, kUnicorn)) ++count;
    }
  }
  return count;
}

// Hide the unicorns away from `safeColumn`/`safeRow` and everything touching
// it. At most nine squares are kept clear, which leaves 27 for five unicorns.
inline void hideUnicorns(Game& game, const int safeColumn, const int safeRow) {
  int placed = 0;
  while (placed < kUnicorns) {
    const uint32_t roll = nextRandom(game.rng) % static_cast<uint32_t>(kCells);
    const int column = static_cast<int>(roll) % kColumns;
    const int row = static_cast<int>(roll) / kColumns;
    if (game.cell[column][row] & kUnicorn) continue;
    const int dc = column - safeColumn;
    const int dr = row - safeRow;
    if (dc >= -1 && dc <= 1 && dr >= -1 && dr <= 1) continue;
    game.cell[column][row] |= kUnicorn;
    ++placed;
  }
}

inline int heartCount(const Game& game) {
  int count = 0;
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] & kHeart) ++count;
    }
  }
  return count;
}

inline bool allSafeSquaresOpen(const Game& game) {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      const uint8_t cell = game.cell[column][row];
      if ((cell & kUnicorn) == 0 && (cell & kOpen) == 0) return false;
    }
  }
  return true;
}

// Open a square, flooding through everything a zero touches. Iterative, and a
// square is marked open when it is queued, so each square is queued at most
// once and the queue cannot outgrow the board (see MinesweeperCore.h's
// floodFrom for the bug that rule came from).
inline void floodFrom(Game& game, const int startColumn, const int startRow) {
  uint8_t queue[kCells][2];
  int head = 0;
  int tail = 0;

  // A heart is the child saying "a unicorn is here". The flood goes round it.
  if (game.cell[startColumn][startRow] & (kOpen | kHeart)) return;
  game.cell[startColumn][startRow] |= kOpen;
  queue[tail][0] = static_cast<uint8_t>(startColumn);
  queue[tail][1] = static_cast<uint8_t>(startRow);
  ++tail;

  while (head < tail) {
    const int column = queue[head][0];
    const int row = queue[head][1];
    ++head;

    if (neighbouringUnicorns(game, column, row) != 0) continue;
    for (int dc = -1; dc <= 1; ++dc) {
      for (int dr = -1; dr <= 1; ++dr) {
        if (dc == 0 && dr == 0) continue;
        const int nc = column + dc;
        const int nr = row + dr;
        if (!inside(nc, nr)) continue;
        if (game.cell[nc][nr] & (kOpen | kHeart)) continue;
        game.cell[nc][nr] |= kOpen;
        queue[tail][0] = static_cast<uint8_t>(nc);
        queue[tail][1] = static_cast<uint8_t>(nr);
        ++tail;
      }
    }
  }
}

inline bool canOpen(const Game& game, const int column, const int row) {
  if (!inside(column, row) || over(game)) return false;
  return (game.cell[column][row] & (kOpen | kHeart)) == 0;
}

// Open a square and settle the outcome. Returns false and changes nothing when
// the tap would do nothing, so a caller can route a tap straight here.
inline bool open(Game& game, const int column, const int row) {
  if (!canOpen(game, column, row)) return false;

  if (game.status == Status::Fresh) {
    hideUnicorns(game, column, row);
    game.status = Status::Playing;
  }

  if (game.cell[column][row] & kUnicorn) {
    game.cell[column][row] |= kOpen;
    game.status = Status::Found;
    return true;
  }

  floodFrom(game, column, row);
  if (allSafeSquaresOpen(game)) game.status = Status::Won;
  return true;
}

// Put a heart on a covered square, or take it off again. Before the first tap
// there are no unicorns yet, so a heart then is allowed and simply means
// nothing; refusing it would be a rule a five year old cannot see.
inline bool toggleHeart(Game& game, const int column, const int row) {
  if (!inside(column, row) || over(game)) return false;
  if (game.cell[column][row] & kOpen) return false;
  game.cell[column][row] ^= kHeart;
  return true;
}

// The unicorns still hiding, as the child counts them: total minus hearts
// placed. Never below zero on screen, because "-1 unicorns" is not a number a
// child can do anything with.
inline int unicornsLeft(const Game& game) {
  const int left = kUnicorns - heartCount(game);
  return left < 0 ? 0 : left;
}

}  // namespace unicorns
