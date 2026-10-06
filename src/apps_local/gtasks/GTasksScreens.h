#pragma once

// The Google Tasks screens. Freestanding builders in the Notes and Instapaper
// mould: a model in, a drawn frame out, no renderer and no Activity, so
// host-tests/ui can assert what they drew and what they made tappable.
//
// The list borrows Notes' rows on purpose -- the same tick box, the same
// strike through a done line, the same footer band -- so a list on this
// device looks like a list whichever app it came from.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace gtasksui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer the 200s, Hacker News the 300s, Instapaper the
// 320s, Notes the 340s. Google Tasks takes the 360s.
enum : fui::ActionId {
  ActionToggle = 360,
  ActionRefresh = 361,
  ActionSettings = 362,
  ActionSettingRow = 363,
  ActionCloseSettings = 364,
  ActionSignOut = 365,
  ActionKeepSignedIn = 366,
  ActionNotice = 367,
  ActionPagePrev = 368,
  ActionPageNext = 369,
  ActionStartSignIn = 370,
  ActionCancelSignIn = 371,
  ActionOpenLists = 372,
  ActionPickList = 373,
  ActionCloseLists = 374,
};

// --- The list --------------------------------------------------------------

struct Row {
  const char* title = "";
  const char* due = nullptr;  // "DUE 7 OCT", or null
  bool checked = false;       // ticked here, waiting to go up
  bool child = false;         // a subtask, drawn indented
};

struct ListModel {
  // The list's own name from Google, or "TASKS" before the first sync.
  const char* title = "TASKS";
  // On the band, right-aligned: "14:32", "2 TO SEND", "OFFLINE".
  const char* status = nullptr;
  const Row* rows = nullptr;  // this page's rows, not the whole list
  int count = 0;
  // Row index of rows[0] in the whole list, carried back as the toggle's value
  // so a tap names a task rather than a slot on the glass.
  int firstIndex = 0;
  // "2 / 3" when the list does not fit; null when it does.
  const char* pageLabel = nullptr;
  bool canPagePrev = false;
  bool canPageNext = false;
  // Owned by the Activity, which knows the glyphs. The menu sits left of the
  // title and opens the list switcher; the gear sits right.
  const freeink::Icon* settingsIcon = nullptr;
  const freeink::Icon* menuIcon = nullptr;
  // Drawn for the sleep screen: no buttons, no tap targets, and the rows run
  // to the bottom of the panel where the footer would be.
  bool asleep = false;
  // Shown on an empty list. Different before the first sync than after it.
  const char* emptyHeadline = "ALL DONE";
  const char* emptyMessage = "Nothing open in this list.";
};

void buildList(toybox::Screen& screen, const ListModel& model);

// How many rows one page holds. Asked of the same layout the drawing uses, so
// the page label, the side keys and the drawn rows cannot disagree.
int listCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, bool paged, bool asleep = false);

// --- The list switcher -----------------------------------------------------

struct ListChoice {
  const char* title = "";
  const char* detail = "";  // "4 OPEN", "ON SLEEP SCREEN"
};

struct ListsModel {
  const ListChoice* lists = nullptr;
  int count = 0;
  int current = -1;  // marked as selected
};

// Every list, one row each; a tap carries the row's index in ActionPickList.
void buildLists(toybox::Screen& screen, const ListsModel& model);

// --- Settings --------------------------------------------------------------

struct SettingsModel {
  const char* showLabel = "ALL";  // "ALL" or "DUE TODAY"
  const char* pollLabel = "";     // "EVERY MIN"
  const char* sleepLabel = "";    // "OFF", or the list on the sleep screen
  bool signedIn = false;
};

// The rows. ActionSettingRow carries the value, not the position.
enum class SettingRow : uint8_t { Poll, Sleep, SignOut, Show };

void buildSettings(toybox::Screen& screen, const SettingsModel& model);

// --- Sign out confirm ------------------------------------------------------

// KEEP takes the left and is filled, on the pixels the primary action has on
// every other screen; SIGN OUT is outlined on the right, the side of the bar
// that takes things away. So a second jab at the row that opened this cancels.
void buildSignOutConfirm(toybox::Screen& screen, int pendingCount);

// --- Signing in ------------------------------------------------------------

// Why sign-in is needed (or why it stopped working) and the one button that
// starts it. `reason` replaces the default sentence when Google said why.
void buildSignIn(toybox::Screen& screen, const char* reason);

// The phone's way in: a QR of the reader's own sign-in page, and the same
// address in characters for typing. Returns the QR's rect for the encoder.
fui::Rect buildPhone(toybox::Screen& screen, const char* address);

// --- Notices ---------------------------------------------------------------

struct NoticeModel {
  const char* headline = "";
  const char* message = "";
  // nullptr draws no button: a busy notice has nothing to decide yet.
  const char* actionLabel = nullptr;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace gtasksui
