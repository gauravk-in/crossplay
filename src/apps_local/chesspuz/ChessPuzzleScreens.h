#pragma once

// Checkmate on screen. Freestanding builders over plain models: no renderer,
// no Activity, no storage. Everything a finger can hit is registered here, from
// the same rects the pixels were drawn from. The board itself is app-drawn into
// the rect buildPuzzle() returns, the split FreeInkUI documents for a play
// surface.

#include "../ui/ToyboxScreen.h"
#include "ChessPuzzleCore.h"

namespace chesspuzui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionLevel = 1,  // value: 1..3
  ActionHints = 2,  // flips the switch
  ActionScores = 3,
  ActionUndo = 4,
  ActionHint = 5,
  ActionSkip = 6,
  ActionNext = 7,
  ActionOlder = 8,
  ActionNewer = 9,
  ActionPlay = 10,
  ActionBoard = 11,  // the board's rect; the activity resolves the square
};

struct MenuModel {
  bool hintsOn = true;
  int next[chesspuz::kLevels] = {};   // 1-based puzzle number each level is on
  int count[chesspuz::kLevels] = {};  // puzzles in each level
  int todayPlayed = 0;
  int todayClean = 0;
  bool haveToday = false;  // false when the clock is not set
};

struct PuzzleModel {
  int mateIn = 1;
  int number = 1;  // 1-based, within the level
  int count = 1;
  bool playerWhite = true;
  chesspuz::State state = chesspuz::State::Playing;
  int movesMade = 0;
  bool hintsOn = true;
  int hintsUsed = 0;
  int undosUsed = 0;
  bool canUndo = false;
  bool canHint = false;
  bool thinking = false;  // the opponent is about to answer
};

struct ResultsModel {
  const chesspuz::Window* window = nullptr;  // null when the clock is not set
  int page = 0;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
// Returns the board's square area: 8 x 8 squares, its size a multiple of 8.
fui::Rect buildPuzzle(toybox::Screen& screen, const PuzzleModel& model);
void buildResults(toybox::Screen& screen, const ResultsModel& model);

}  // namespace chesspuzui
