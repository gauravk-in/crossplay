#pragma once

// Memory on screen. Freestanding builders over plain models: no renderer, no
// Activity, no storage. The activity binds the faces and fills the models;
// everything a finger can hit is registered here, from the same rects the
// pixels were drawn from.
//
// Faces (toybox::proseMenuFaces): SMALL is the button cut for labels, BODY the
// 20px UI cut, TITLE the display cut.

#include "../ui/ToyboxScreen.h"
#include "MemoryCore.h"

namespace memoryui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionSize = 1,     // value: the memory::Size
  ActionMenuRow = 2,  // value: a MenuRow
  ActionBoard = 3,    // the whole board; the card comes from BoardLayout::cardAt
  ActionAgain = 4,
  ActionMenu = 5,
};

enum class MenuRow : int { Solo = 0, Pass, Nearby, HowTo, Count };

// Where the cards are. Drawing and hit-testing both read this one value, so a
// tap cannot land on a card other than the one drawn under it.
struct BoardLayout {
  fui::Rect area{};
  int columns = 0;
  int rows = 0;
  int16_t cardW = 0;
  int16_t cardH = 0;
  int16_t gap = 0;
  int16_t left = 0;
  int16_t top = 0;

  fui::Rect cardRect(int index) const;
  // -1 for a tap between cards or off the board.
  int cardAt(int x, int y) const;
};

BoardLayout layoutBoard(const fui::Rect& area, memory::Size size);

struct MenuModel {
  int size = 0;
  const memory::Window* window = nullptr;  // null when the clock is not set
  const char* nearbyName = nullptr;
  // False hides PLAY NEARBY, for a build with no radio (the downloadable app).
  bool nearby = true;
};

struct BoardModel {
  const memory::Game* game = nullptr;
  // The seat whose recall this device keeps: 0 alone, the coin toss's seat in
  // a match, -1 when two people share the device.
  int seat = 0;
  const char* names[memory::kSeats] = {"P1", "P2"};
  // One character on each taken card, saying whose it is.
  const char* badges[memory::kSeats] = {"1", "2"};
  bool waiting = false;   // a match, and the other device is playing
  const char* news = "";  // one line under the board
};

struct ResultModel {
  const memory::Game* game = nullptr;
  int seat = 0;
  const char* names[memory::kSeats] = {"P1", "P2"};
  const memory::Window* window = nullptr;  // the chart, when this game is charted
  bool charted = false;                    // whether this game went on the chart
  bool tooFew = false;                     // finished, but with too few recall chances to chart
};

// The board it drew, for the activity's hit test.
BoardLayout buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);
void buildHowTo(toybox::Screen& screen);

}  // namespace memoryui
