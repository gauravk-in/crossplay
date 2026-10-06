#pragma once

// The Cards screens.
//
// Freestanding builders in the NotesScreens mould: a model in, a drawn frame
// out, no renderer and no Activity, so host-tests/ui can assert what they drew
// and what they made tappable. The code itself is drawn by the Activity into
// the square buildCard() hands back, because drawing modules needs the
// renderer this layer does not have.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace walletui {

namespace fui = freeink::ui;

// Workouts has the 620s; Cards takes the 660s.
enum : fui::ActionId {
  ActionOpenCard = 660,
  ActionUsePhone = 661,
  ActionPrev = 662,
  ActionNext = 663,
  ActionDelete = 664,
  ActionDeleteConfirm = 665,
  ActionDeleteKeep = 666,
  ActionDismiss = 667,
};

// --- The list ------------------------------------------------------------

struct ListRow {
  const char* title = "";
  const char* caption = "";  // empty: the title has the row to itself
};

struct ListModel {
  const ListRow* rows = nullptr;
  int count = 0;
  int firstVisible = 0;
  // "1/2" on the band when the cards do not fit one page.
  const char* pageLabel = nullptr;
};

void buildList(toybox::Screen& screen, const ListModel& model);
// How many rows a page holds, asked of the layout the drawing uses.
int listCapacity(const fui::DeviceContext& device);

// --- One card ------------------------------------------------------------

struct CardModel {
  const char* title = "";
  const char* caption = "";
  // "2/5": where this card sits among them, on the band.
  const char* position = nullptr;
  // PREV and NEXT are drawn only when there is a card that way.
  bool hasPrev = false;
  bool hasNext = false;
};

// Returns the square the code goes in. Everything around it is left white, so
// the code always has the quiet margin a scanner looks for.
fui::Rect buildCard(toybox::Screen& screen, const CardModel& model);

// What goes in the square when the code cannot be drawn.
void buildCardFailure(toybox::Screen& screen, const fui::Rect& square, const char* prose);

// --- The delete confirm --------------------------------------------------

// KEEP IT sits on the pixels NEXT had, so a second jab at the band's bin
// during the repaint lands on nothing, and one at NEXT keeps the card.
void buildDeleteConfirm(toybox::Screen& screen, const char* title, const char* prose);

// --- The phone -----------------------------------------------------------

struct PhoneModel {
  // What the QR carries: the device's own address, from the live IP.
  const char* url = "";
  // What a person reads: the mDNS name when it started, else the address.
  const char* readable = "";
  // How many cards arrived from the phone while this screen was up.
  int added = 0;
};

// Returns the square the caller draws the address code into.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

// --- A refusal -----------------------------------------------------------

void buildNotice(toybox::Screen& screen, const char* prose);

}  // namespace walletui
