#pragma once

// The connection to the printer: MQTT 3.1.1 over TLS, read-only.
//
// It connects, subscribes to the printer's report topic, asks once for
// everything ("pushall"), and from then on streams each report straight into
// a ReportParser as the bytes arrive. A 30KB report never sits in memory
// whole: the largest buffer here is one read chunk.
//
// The printer's certificate is signed by Bambu's own CA and names the serial,
// not the address, so it is not verified. That is the same trust Bambu Studio
// gives a printer in LAN mode, and the access code is what keeps strangers off.
//
// Device only: the simulator has no TLS stack, and connect() says so.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "BambuCore.h"

namespace bambu {

class Link {
 public:
  using AbortCallback = std::function<bool()>;

  Link();
  ~Link();
  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;

  // TCP, TLS, CONNECT and CONNACK, SUBSCRIBE, pushall. Blocks for at most a
  // few seconds per step; `shouldAbort` is polled throughout. False with `why`
  // set in a sentence a person can act on.
  bool connect(const Config& config, const std::string& clientId, const AbortCallback& shouldAbort, std::string& why);

  enum class Poll : uint8_t { Idle, Changed, Dropped };
  // Reads what has arrived, merging any report into `state`. Keeps the
  // connection alive with a ping. Dropped means connect() again.
  Poll poll(PrinterState& state, std::string& why);

  // Asks the printer to send everything again.
  bool requestPushAll();
  void stop();
  bool connected() const;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace bambu
