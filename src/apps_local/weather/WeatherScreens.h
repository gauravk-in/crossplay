#pragma once

// Weather's chrome screens: settings, the place picker and the notice. Free
// functions over a model, so they touch no renderer and no Activity. The
// forecast itself is the app's own surface and is drawn by WeatherActivity.

#include "../ui/ToyboxScreen.h"

namespace weatherui {

namespace fui = freeink::ui;

// Pomodoro and the games keep to the low hundreds; these stay in the 900s.
enum : fui::ActionId {
  ActionSettings = 901,
  ActionRefresh = 902,
  ActionSettingsRow = 903,
  ActionSettingsDone = 904,
  ActionPickPlace = 905,
  ActionSearchAgain = 906,
  ActionNotice = 907,
};

// The settings rows, in the order they are drawn.
enum class SettingsRow : int16_t { Location, Temperature, Wind, Rain, Count };

struct SettingsModel {
  const char* location = nullptr;  // "Berlin, Germany", or nullptr when unset
  const char* temperature = "";
  const char* wind = "";
  const char* rain = "";
};

void buildSettings(toybox::Screen& screen, const SettingsModel& model);

struct PlacesModel {
  const fui::ListItem* items = nullptr;
  int count = 0;
};

void buildPlaces(toybox::Screen& screen, const PlacesModel& model);

struct NoticeModel {
  const char* headline = "";
  const char* message = nullptr;
  const char* actionLabel = nullptr;
  fui::ActionId action = fui::NO_ACTION;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

// The forecast's band: the place's name, settings on the left, refresh on the
// right. Returns the body below the chrome.
fui::Rect buildForecastChrome(toybox::Screen& screen, const char* title);

}  // namespace weatherui
