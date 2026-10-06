#pragma once

// The reader's half of the conversation with Google, and with the sign-in
// service (server/tasks-bridge) that stands in for Google's device sign-in.
//
// The service pairs this reader to a Google account (start / poll / on-device
// confirm, like the Instapaper bridge) and afterwards trades this reader's
// device token for a short-lived access token. Everything else goes straight
// to tasks.googleapis.com: read the default list's name, read its open tasks,
// mark one completed. The reader never holds Google's refresh token or the
// client secret.
//
// Transport is bridge::request (verified TLS on the device, curl in the
// simulator), not HttpDownloader, which calls setInsecure(). The baked root
// bundle holds GTS Root R1 and R4, which googleapis.com chains to;
// /.crosspoint/gtasks/.roots.pem overrides it.
//
// Every failure fills `message` with a sentence the screen shows verbatim.

#include <cstdint>
#include <string>
#include <vector>

#include "GTasksCore.h"

namespace gtasks {

struct AccessToken {
  std::string value;
  // millis() after which it is not worth sending. Google grants an hour; this
  // is set a minute short of that so a token never expires mid-sync.
  uint32_t goodUntilMs = 0;
  bool usable(uint32_t nowMs) const { return !value.empty() && static_cast<int32_t>(goodUntilMs - nowMs) > 0; }
};

class Api {
 public:
  // The sign-in service's host: bridge.cfg's, or the one compiled in when that
  // is "".
  void setBridgeHost(const std::string& host);
  // The address the reader's QR points at, the code in its fragment so it
  // never reaches a server log.
  std::string pairUrl(const std::string& code) const;
  // What the sign-in screen tells a person to type: host and path, no scheme.
  std::string pairAddress() const;

  struct PairStart {
    std::string code;
    std::string pollToken;
  };
  bool pairStart(PairStart& out, std::string& message);
  // 1 delivered (account+token filled), 0 still pending, -1 failed/expired.
  int pairPoll(const std::string& pollToken, std::string& account, std::string& token, std::string& message);
  // Best effort on any walk-away: a pollToken kills the pending code, a
  // deviceToken revokes a pairing the confirm screen declined.
  void pairAbandon(const std::string& pollToken, const std::string& deviceToken);
  // Best effort on sign-out. The service revokes the Google grant when this was
  // the account's last reader.
  void unpair(const std::string& deviceToken);

  // True after the service refused the device token: unpaired, or Google
  // revoked the grant. The card's auth.cfg is then useless and the screen has
  // to say to sign in again rather than "try later".
  bool signedOut = false;

  bool refresh(const Credentials& creds, uint32_t nowMs, AccessToken& out, std::string& message);

  // The default list's title, for the header. "" when Google did not say.
  bool listTitle(const AccessToken& token, std::string& title, std::string& message);

  // Every OPEN task in the default list, in no particular order (sort with
  // sortForDisplay). Pages through Google's 100-per-response limit.
  bool openTasks(const AccessToken& token, std::vector<Task>& out, std::string& message);

  // Marks a task completed. A task Google no longer has (deleted on the phone)
  // counts as done: there is nothing left to complete.
  bool complete(const AccessToken& token, const std::string& id, std::string& message);

  // True when the last call failed because the access token was refused, so
  // the caller can refresh once and retry.
  bool tokenRefused = false;

 private:
  int callBridge(const char* method, const std::string& path, const std::string& token, const std::string& body,
                 std::string& response, std::string& message) const;

  std::string bridgeHost_;
};

}  // namespace gtasks
