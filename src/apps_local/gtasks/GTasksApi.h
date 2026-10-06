#pragma once

// The reader's half of the conversation with Google.
//
// Sign-in: trade the code a phone brought back for a refresh token, and
// revoke it on sign-out. Every sync: trade the refresh token for an access
// token, read the default list's name, read its open tasks, mark one
// completed. Nothing goes anywhere but Google.
//
// Transport is bridge::request (verified TLS on the device, curl in the
// simulator), not HttpDownloader, which calls setInsecure() -- and a request
// carrying a refresh token to whoever answers is a request handing them the
// account. The baked root bundle holds GTS Root R1 and R4, which googleapis.com
// chains to; /.crosspoint/gtasks/.roots.pem overrides it.
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
  // The end of a sign-in: the code from the pasted address, and the PKCE
  // verifier it was asked for with. Fills the refresh token and the address.
  bool exchange(const Client& client, const std::string& code, const std::string& verifier, Credentials& out,
                std::string& message);
  // Best effort on sign-out: tells Google to drop the grant, so the token on
  // a card that is copied later is worth nothing.
  void revoke(const std::string& refreshToken);

  // True after Google refused the refresh token itself (revoked, expired, or a
  // client in Testing mode a week later). The card's auth.cfg is then useless
  // and the screen has to say to sign in again rather than "try later".
  bool signedOut = false;

  bool refresh(const Client& client, const Credentials& creds, uint32_t nowMs, AccessToken& out, std::string& message);

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
};

}  // namespace gtasks
