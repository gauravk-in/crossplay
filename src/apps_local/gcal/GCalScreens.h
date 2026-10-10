#pragma once

// The Calendar screens. Freestanding builders in the Tasks mould: a model in,
// a drawn frame out, no renderer and no Activity, so host-tests/ui can assert
// what they drew and what they made tappable.
//
// The schedule is Google Calendar's Schedule view drawn in ink: the day in a
// column on the left (today's number on a black disc), each event a rounded
// card beside it with its time under its title, all-day events as solid black
// bars on top of their day, and a month's name across the page where a new
// month starts.
//
// Sign-in, the phone's QR and the notices are Tasks' builders (GTasksScreens.h)
// with Calendar's title on the band, because it is the same Google sign-in.

#include <cstdint>
#include <vector>

#include "../ui/ToyboxScreen.h"
#include "GCalCore.h"

namespace gcalui {

namespace fui = freeink::ui;

// Tasks takes the 360s; Calendar the 380s.
enum : fui::ActionId {
  ActionRefresh = 380,
  ActionToday = 381,
  ActionPagePrev = 382,
  ActionPageNext = 383,
  ActionSettings = 384,
  ActionSettingRow = 385,
  ActionCloseSettings = 386,
  ActionSignOut = 387,
  ActionKeepSignedIn = 388,
};

// --- The schedule ------------------------------------------------------------

struct ScheduleModel {
  // The month of the first row on the page: "OCTOBER 2026".
  const char* title = "CALENDAR";
  // On the band, right-aligned: "14:32", "AUTO SYNC", "OFFLINE".
  const char* status = nullptr;
  // This page's items, and the events they point into.
  const gcal::Item* items = nullptr;
  // itemHeights() for the same items: the drawing and the paging share them.
  const int* heights = nullptr;
  int count = 0;
  const gcal::Event* events = nullptr;
  int eventCount = 0;
  int64_t today = 0;
  bool canPagePrev = false;
  bool canPageNext = false;
  // TODAY is dimmed when today is already on the page.
  bool canGoToday = false;
  const freeink::Icon* settingsIcon = nullptr;
  // Drawn for the sleep screen: no buttons, no tap targets, and the rows run
  // to the bottom of the panel where the footer would be.
  bool asleep = false;
  // Shown when there is nothing at all to draw.
  const char* emptyHeadline = "NOTHING PLANNED";
  const char* emptyMessage = "No events in the next three months.";
};

void buildSchedule(toybox::Screen& screen, const ScheduleModel& model);

// Every item's height, in the order given, as buildSchedule lays them out, so
// the Activity can page with gcal::fitFrom and gcal::pageBefore.
std::vector<int> itemHeights(const fui::DrawTarget& target, const std::vector<gcal::Item>& items);

// The height a page of items gets: what fitFrom and pageBefore are given.
int pageHeight(const fui::DeviceContext& device, bool asleep);

// --- Settings --------------------------------------------------------------

struct SettingsModel {
  const char* pollLabel = "";   // "EVERY 5 MIN"
  const char* sleepLabel = "";  // "ON", "OFF"
  const char* account = "";     // the signed-in address, or ""
  bool signedIn = false;
};

// The rows. ActionSettingRow carries the value, not the position.
enum class SettingRow : uint8_t { Poll, Sleep, SignOut };

void buildSettings(toybox::Screen& screen, const SettingsModel& model);

// --- Sign out confirm ------------------------------------------------------

// KEEP on the left and filled, SIGN OUT outlined on the right, as in Tasks.
void buildSignOutConfirm(toybox::Screen& screen);

}  // namespace gcalui
