#pragma once

// Prompter's menus. Freestanding builders in the TRMNL mould: a model in, a
// drawn frame out, no renderer and no Activity. The script itself is not here:
// the activity draws it edge to edge in the size the settings say.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace prompterui {

namespace fui = freeink::ui;

// TRMNL has the 700s; Prompter takes the 760s.
enum : fui::ActionId {
  ActionOpenScript = 760,
  ActionPhone = 761,
  ActionSettings = 762,
  ActionDismiss = 763,
  ActionSizeDown = 764,
  ActionSizeUp = 765,
  ActionAutoDown = 766,
  ActionAutoUp = 767,
  ActionOrientation = 768,
  ActionColors = 769,
  ActionTurner = 770,
  ActionSwap = 771,
  ActionScan = 772,
  ActionPair = 773,
  ActionForget = 774,
  ActionListUp = 775,
  ActionListDown = 776,
};

// --- The scripts ----------------------------------------------------------------

struct LibraryModel {
  // Display names, without .txt.
  const char* const* names = nullptr;
  int count = 0;
  int top = 0;
  // The script open last, marked in the list.
  int current = -1;
  // One line under the header: the page turner's state.
  const char* turner = "";
};

// Returns how many rows fit, so the caller can page the list.
int buildLibrary(toybox::Screen& screen, const LibraryModel& model);

// --- Settings --------------------------------------------------------------------

struct SettingsModel {
  int size = 0;  // 0-based
  int sizeCount = 1;
  const char* timer = "OFF";
  bool canTimerDown = false;
  bool canTimerUp = true;
  bool landscape = false;
  bool dark = false;
  bool swap = false;
  const char* turner = "";
  bool bluetooth = true;
};

// Returns the box the caller draws the text-size sample into.
fui::Rect buildSettings(toybox::Screen& screen, const SettingsModel& model);

// --- The page turner -------------------------------------------------------------

struct TurnerModel {
  const char* status = "";
  const char* const* found = nullptr;
  int foundCount = 0;
  bool scanning = false;
  bool paired = false;
};

void buildTurner(toybox::Screen& screen, const TurnerModel& model);

// --- The phone -------------------------------------------------------------------

struct PhoneModel {
  const char* url = "";
  const char* readable = "";
  const char* saved = "";
};

// Returns the square the caller draws the code into.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

// A sentence and a BACK button.
void buildNotice(toybox::Screen& screen, const char* prose);

}  // namespace prompterui
