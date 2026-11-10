#pragma once

// The phone page's server. GET /bambu is the settings page, /bambu/config
// reads and writes the settings, and /bambu/cloud/* signs the reader in to a
// Bambu account and out again. Nothing else is routed, so the address in the
// QR code reaches the settings and not the card.
//
// A sign-in is made from inside the request: the phone waits the few seconds
// the reader spends talking to Bambu, and gets the answer as the reply.
//
// Its own small WebServer rather than another surface on CrossPointWebServer,
// so adding an app costs no upstream file.

#include <WebServer.h>

#include <memory>
#include <string>

#include "BambuCloud.h"

class BambuServer {
 public:
  ~BambuServer() { stop(); }

  // False when the server could not be made or there is no network to put it
  // on. The simulator has neither, and the screen is still drawn there.
  bool begin();
  void stop();
  bool isRunning() const { return running_; }
  void handleClient();

  // Read-only key=value lines the page shows under the settings: the
  // connection's last word.
  void setFacts(const std::string& facts) { facts_ = facts; }

  // True once a phone has saved. Cleared by the reader of it.
  bool takeChanged() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
  }

 private:
  void handlePage();
  void handleRead();
  void handleSave();
  void handleSignIn();
  void handleCode();
  void handleSignOut();
  // Saves a fresh account, fills the serial (and the access code) from it when
  // the settings have none, and answers with the printers on the account.
  void finishSignIn(const bambu::cloud::Account& account);
  void answer(const char* result, const std::string& message, const std::string& extra = std::string());
  std::string accountFacts() const;

  std::unique_ptr<WebServer> server_;
  std::string facts_;
  bool running_ = false;
  bool changed_ = false;
};
