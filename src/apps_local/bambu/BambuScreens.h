#pragma once

// The Bambu monitor's screens.
//
// Freestanding builders in the TrmnlScreens mould: a model in, a drawn frame
// out, no renderer and no Activity, so host-tests/bambu can assert what they
// drew and what they made tappable.

#include <cstdint>

#include "../ui/ToyboxScreen.h"
#include "BambuCore.h"

namespace bambuui {

namespace fui = freeink::ui;

// TRMNL has the 700s and Prompter the 760s; Bambu takes the 880s.
enum : fui::ActionId {
  ActionUsePhone = 880,
  ActionDismiss = 881,
  ActionRefresh = 882,
};

// --- The monitor ---------------------------------------------------------------

struct MonitorModel {
  const bambu::PrinterState* state = nullptr;
  // One line along the bottom: "LIVE  /  192.168.1.42", "CONNECTING", "NOT LIVE".
  const char* link = "";
  bool live = false;
  // Why it is not live, in a sentence; drawn over the line while not live.
  const char* reason = "";
  // "ENDS 14:35" when the reader knows the time of day, else empty.
  const char* endsAt = "";
};

// What the activity draws after rebinding the title slot to a big cut, because
// a freestanding builder only has the three slots it was given.
struct Hero {
  fui::Rect rect{};
  char text[8] = {};
};

// Draws everything but the big percentage, which goes in the returned hero.
Hero buildMonitor(toybox::Screen& screen, const MonitorModel& model);
// The big number, in whatever face the caller bound to the title slot.
void drawHero(toybox::Screen& screen, const Hero& hero);

// --- Setting up ---------------------------------------------------------------------

// Nothing configured yet: what the app needs and where on the printer it is.
void buildSetup(toybox::Screen& screen);

struct PhoneModel {
  const char* url = "";       // what the QR carries
  const char* readable = "";  // what a person reads
  bool saved = false;
  bool busy = false;  // the reader is waiting on Bambu for the phone
};
// Returns the square the caller draws the code into.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

// A sentence and a BACK button.
void buildNotice(toybox::Screen& screen, const char* prose);

}  // namespace bambuui
