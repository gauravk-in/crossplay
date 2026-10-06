#pragma once

// The reader's half of the conversation with Google Calendar: which calendars
// the account shows, and the events on them in a window. Read only.
//
// Sign-in and access tokens are Tasks' (gtasks::Api): one Google account per
// reader, one refresh token in /.crosspoint/gtasks/auth.cfg, granted both
// scopes. The transport is the same bridge::request, with the same verified
// roots.
//
// Every failure fills `message` with a sentence the screen shows verbatim.

#include <string>
#include <vector>

#include "../gtasks/GTasksApi.h"
#include "GCalCore.h"

namespace gcal {

struct Calendar {
  std::string id;
  std::string title;
};

class Api {
 public:
  // The calendars ticked in the account's own Google Calendar, primary first.
  // Hidden and unticked ones are left out, as the phone leaves them out.
  bool calendars(const gtasks::AccessToken& token, std::vector<Calendar>& out, std::string& message);

  // Every event on one calendar between two instants, recurring ones expanded,
  // cancelled ones and ones the account declined left out. Appended to `out`.
  bool events(const gtasks::AccessToken& token, const std::string& calendarId, int64_t timeMin, int64_t timeMax,
              std::vector<Event>& out, std::string& message);

  // True when the last call failed because the access token was refused, so
  // the caller can refresh once and retry.
  bool tokenRefused = false;
  // True when Google answered that the sign-in does not include Calendar: a
  // reader signed in before Calendar existed, or a box left unticked on the
  // consent screen. Only signing in again fixes it.
  bool needsConsent = false;
};

}  // namespace gcal
