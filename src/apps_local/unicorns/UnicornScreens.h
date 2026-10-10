#pragma once

// Unicorns on screen. Freestanding builders over plain models, so host-tests/ui
// can build every screen against a fake draw target.

#include "../ui/ToyboxScreen.h"
#include "UnicornCore.h"

namespace unicornui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionHowToNext = 2,
  ActionAgain = 3,
  ActionDone = 4,
  // PEEK and HEART under the board; the value is 1 for HEART.
  ActionTool = 5,
  // The capsule under a finished board: the door to the result screen.
  ActionSeeResult = 6,
};

enum class MenuRow : int { Play = 0, HowTo, Count };

struct MenuModel {
  int selected = -1;
  int stars = 0;
};

struct HowToModel {
  int page = 0;
};

struct BoardModel {
  unicorns::Game game{};
  // The square a finger is resting on, or -1, drawn as a thick frame so a hold
  // shows it is being felt before it places a heart.
  int holdColumn = -1;
  int holdRow = -1;
  // What a tap on a square does: open it, or put a heart on it.
  bool heartMode = false;
};

struct ResultModel {
  bool won = false;
  int stars = 0;
};

// The rect of one square, and its exact inverse. The grid is hit-tested from
// the geometry that drew it rather than registered as 36 buttons.
fui::Rect cellRect(const fui::DeviceContext& device, int column, int row);
bool cellAt(const fui::DeviceContext& device, int x, int y, int& column, int& row);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

}  // namespace unicornui
