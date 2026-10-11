#pragma once

// The app store on screen. Freestanding builders over plain models, like every
// other toybox app: no renderer, no storage, no network. The activity binds
// proseMenuFaces and fills the models; everything a finger can hit is
// registered here, from the rects the pixels were drawn from.

#include <string>
#include <vector>

#include "../ui/ToyboxScreen.h"

namespace appstoreui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionTab = 1,    // value: 0 installed, 1 get apps
  ActionRow = 2,    // value: index into the model's rows
  ActionOlder = 3,  // previous page
  ActionNewer = 4,  // next page
  ActionPrimary = 5,
  ActionSecondary = 6,
  ActionBack = 7,
};

struct Row {
  std::string title;   // the app's name
  std::string detail;  // one line under it
  std::string badge;   // the right-hand label: OPEN, INSTALL, UPDATE, ...
  bool dim = false;    // drawn with a dotted badge: there for information, not for tapping
};

struct ListModel {
  int tab = 0;
  const std::vector<Row>* rows = nullptr;
  int page = 0;
  // Shown instead of rows when there are none.
  const char* empty = "";
  // One line under the tabs: where the catalog came from, or why it did not.
  std::string status;
};

// Draws the list and returns how many pages it has, so the activity can clamp.
int buildList(toybox::Screen& screen, const ListModel& model);

struct DetailModel {
  std::string title;
  std::string summary;
  // Label and value pairs: VERSION 1.2.0, APP API 1.0, SIZE 126 KB...
  std::vector<std::pair<std::string, std::string>> facts;
  // A sentence that matters more than the facts: why it will not run here.
  std::string warning;
  const char* primary = nullptr;    // nullptr hides the button
  const char* secondary = nullptr;  // nullptr hides it
};

void buildDetail(toybox::Screen& screen, const DetailModel& model);

struct NoticeModel {
  std::string title;
  std::string message;
  const char* button = nullptr;  // nullptr: no button (busy)
  int progress = -1;             // 0..100 draws a bar; -1 none
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace appstoreui
