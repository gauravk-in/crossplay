#pragma once

// BAMBU: a Bambu Lab printer's progress and AMS on the reader.
//
// The three-way split every app in this fork uses. BambuCore holds the rules
// (the settings file, the report merge, the MQTT packets) and has a host
// suite. BambuScreens lays out the monitor. This file keeps what needs
// hardware: the radio, the card, the phone page, and the connection.
//
// The settings live on the phone, because an IP address, a fifteen character
// serial and an access code are a lot of taps on this keyboard. With a Bambu
// account signed in as well, a printer the LAN cannot reach is followed
// through Bambu's cloud instead. The monitor
// stays connected while it is open, so the screen follows the print; it is
// repainted when something worth seeing changed, and no more often than the
// panel likes.

#include <cstdint>
#include <memory>
#include <string>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "BambuCloud.h"
#include "BambuCore.h"
#include "BambuLink.h"
#include "BambuScreens.h"
#include "BambuServer.h"

class BambuActivity final : public Activity {
 public:
  BambuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Bambu", renderer, mappedInput) {}
  ~BambuActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Awake with the phone page up, and with the monitor up unless the setting
  // lets the reader sleep.
  bool preventAutoSleep() override {
    return (server_ && server_->isRunning()) || (view_ == View::Monitor && config_.keepAwake);
  }
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class View : uint8_t { Setup, Monitor, Phone, Notice };

  // render() runs on its own task and reads everything below, so loop()
  // changes it only under a RenderLock. The connection merges into a copy of
  // the state and swaps it in, which keeps the lock off the radio's time.
  void loadConfig();
  void saveSnapshot(bool force);

  void setView(View view);
  void openHome();
  void showNotice(const std::string& text);

  void queueConnect(bool fromTap);
  void runConnect();
  void pollLink();
  void dropLink(const std::string& why);
  // Repaints the monitor when a change is worth a refresh of the panel.
  void maybeRepaint();
  void pumpInput();
  bool joinWifi(std::string& why);
  // Signed in with no serial yet: takes the account's printer. False, with
  // `why`, when the account lists none or Bambu cannot be asked.
  bool pickCloudPrinter(std::string& why);
  void releaseWifi();

  void startPhone();
  void stopPhone();

  std::string linkText() const;
  std::string endsAt() const;

  bambu::Config config_;
  bambu::cloud::Account account_;
  bambu::PrinterState state_;    // what render() draws
  bambu::PrinterState working_;  // what the connection merges into
  bool configured_ = false;

  View view_ = View::Setup;
  std::string notice_;

  // The connection.
  bambu::Link link_;
  bool connectQueued_ = false;
  bool connectFromTap_ = false;
  bool connecting_ = false;
  bool cancel_ = false;
  bool live_ = false;
  bool viaCloud_ = false;            // the live link goes through Bambu's broker
  bool tryingCloud_ = false;         // what the CONNECTING line names
  unsigned long localFailedAt_ = 0;  // millis(); 0 when the LAN last worked
  bool broughtRadioUp_ = false;
  bool yieldedDevMode_ = false;
  int failures_ = 0;
  unsigned long nextConnectAt_ = 0;  // millis(); 0 means nothing is scheduled
  std::string lastWhy_;
  std::string clientId_;

  // Repainting.
  bool dirty_ = false;
  bool urgent_ = false;
  unsigned long lastPaintAt_ = 0;
  unsigned long lastSavedAt_ = 0;
  bool unsaved_ = false;
  unsigned long liveSince_ = 0;
  int paints_ = 0;

  // The phone.
  std::unique_ptr<BambuServer> server_;
  bool devPaused_ = false;
  bool phoneSaved_ = false;
  bool phoneBusy_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
