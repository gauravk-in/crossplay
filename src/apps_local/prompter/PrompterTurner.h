#pragma once

// The Bluetooth page turner: the reader as a BLE HID host.
//
// A page turner is a little Bluetooth keyboard. It sends arrow keys, Page
// Up/Down or the volume keys, and PrompterCore decides what each one means.
// This file is the radio: scanning for remotes, pairing, staying connected,
// and handing the presses to loop() through a queue.
//
// The radio is up only while Prompter is open. A worker task does the slow,
// blocking parts (connecting can take seconds), so the screen never waits on
// it; the remote's key reports arrive on the Bluetooth host's own task and are
// decoded there into a queue that loop() drains.
//
// Compiled in only where the build says the board has Bluetooth (PROMPTER_BLE).
// Elsewhere, and in the simulator, available() is false and the app says so.

#include <cstdint>
#include <string>
#include <vector>

#include "PrompterCore.h"

namespace prompter {

class TurnerLink {
 public:
  enum class State : uint8_t { Off, Idle, Scanning, Connecting, Connected };

  struct Found {
    std::string address;
    int type = 0;
    std::string name;
    int rssi = 0;
  };

  TurnerLink() = default;
  ~TurnerLink() { end(); }
  TurnerLink(const TurnerLink&) = delete;
  TurnerLink& operator=(const TurnerLink&) = delete;

  static bool available();

  // Brings the radio up. False when there is no Bluetooth or no memory for it.
  bool begin();
  // Disconnects and puts the radio down. Safe to call twice.
  void end();
  bool running() const { return running_; }

  // Looks for remotes for `seconds`; results build up in found().
  void startScan(int seconds);
  bool scanning() const;
  std::vector<Found> found() const;

  // Keeps a connection to this remote, reconnecting when it drops or sleeps.
  void follow(const std::string& address, int type, bool swap);
  void forget();
  void setSwap(bool swap) { swap_ = swap; }

  State state() const;
  // The remote's name once connected, or why the last attempt failed.
  std::string detail() const;
  // Why begin() last failed, as the radio stack said it; empty once it starts.
  std::string failure() const;
  // The bytes of the last key press that maps to no turn, empty if none.
  std::string unknownKey() const;
  // The next page turn pressed on the remote, or None.
  Turn takeTurn();
  // Counts up on every connect and drop, so a screen can tell it changed.
  uint32_t generation() const;

 private:
  bool running_ = false;
  bool swap_ = false;
};

}  // namespace prompter
