#pragma once

// Weather: today with an hourly strip, and the ten days after it.
//
// The three-way split every app here uses. WeatherCore holds the rules (the two
// files on the card, the once-a-day decision, units, WMO codes) and has a host
// suite. WeatherScreens builds the chrome screens. This file keeps what needs
// hardware: the radio, the card, the clock and the forecast drawn on the panel.
//
// The first time the app is opened on a new local day it fetches a fresh
// report; every other time it shows the one on the card until refresh is
// tapped. Nothing slow happens on the render path: a fetch is requested with
// the screen that announces it and runs on the following loop pass.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WeatherCore.h"

class WeatherActivity final : public Activity {
 public:
  WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}
  ~WeatherActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Forecast, Settings, Places, Notice };
  enum class Pending : uint8_t { None, Forecast, Search };
  // What the forecast footer says about the report on screen.
  enum class Status : uint8_t { Idle, Updating, Failed, NoWifi };

  void ensureConnected(Pending what);
  void request(Pending what);
  bool fetchForecast();
  bool searchPlaces();
  void askForPlace();
  void pickPlace(int index);
  void saveConfig();
  void showNotice(const char* headline, const char* message, const char* actionLabel, freeink::ui::ActionId action);
  void openForecast();

  void drawForecast(toybox::Screen& screen, const freeink::ui::Rect& body);

  weather::Config config_;
  // Heap-held: two days of hours and eleven days is a couple of KB.
  std::unique_ptr<weather::Forecast> forecast_;
  bool haveForecast_ = false;

  std::vector<weather::Place> places_;
  std::vector<freeink::ui::ListItem> placeItems_;
  std::string query_;
  std::string settingsLocation_;

  View view_ = View::Forecast;
  Status status_ = Status::Idle;
  Pending pending_ = Pending::None;
  Pending afterConnect_ = Pending::None;
  bool wifiUsed_ = false;
  bool backPressSeen_ = false;

  std::string noticeHeadline_;
  std::string noticeMessage_;
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
