#pragma once

// Google Tasks on the reader: the parts that need no device.
//
// Freestanding C++17 -- no Arduino, no storage, no JSON library -- so
// host-tests/gtasks can ask it what a cache file written by a later firmware
// does, what a half-written credentials file does, and when a poll is due.
//
// ---------------------------------------------------------------------------
// The shape of it.
//
// The card holds the list as Google last described it, plus one fact Google
// does not have yet: which of those tasks were ticked HERE. A tick is never
// sent from the tap. It is written to the card as `pending` and goes up with
// the next sync, whichever starts it -- the charger's poll or REFRESH -- so a
// tick made with no Wi-Fi is not lost and a tick made on a train is not a
// failure.
//
// Sign-in happens on the reader too (see "Signing in" below). The card holds
// the refresh token in auth.cfg; every access token after that the reader gets
// for itself.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace gtasks {

struct Task {
  std::string id;
  std::string title;
  // Google's `position`: zero-padded digits that sort as strings. Kept so a
  // reload of the cache draws the order Google drew, not the order it arrived.
  std::string position;
  // Set on a subtask. Children are drawn indented under their parent.
  std::string parent;
  // "YYYY-MM-DD", or empty. Google stores only the date part of `due`.
  std::string due;
  // Ticked on this reader and not yet confirmed by Google. Drawn ticked, and
  // the only reason a completed task is ever on the card at all.
  bool pending = false;
};

// What auth.cfg carries, one `key=value` per line: Google's refresh token for
// this reader, and the address it belongs to (shown on the sign-out confirm).
// Written by the reader itself when a sign-in finishes.
struct Credentials {
  std::string refreshToken;
  std::string account;
  bool complete() const { return !refreshToken.empty(); }
};

// Lines that are blank, `#` comments or unknown keys are skipped; a CR from a
// file saved on Windows is not part of a value. Fails OPEN: whatever is
// missing simply leaves complete() false, and the app asks to sign in.
Credentials parseCredentials(const std::string& text);
std::string serializeCredentials(const Credentials& creds);

// The Google Cloud OAuth client the reader signs in as: a "Desktop app" client
// with the Tasks API enabled. Google treats a desktop client's secret as
// public, which is why it may sit on a card or in a build; what is private is
// each person's refresh token, and that never leaves their card.
struct Client {
  std::string id;
  std::string secret;
  bool complete() const { return !id.empty() && !secret.empty(); }
};

// client.cfg: `client_id=` and `client_secret=`, the same shape as auth.cfg.
Client parseClient(const std::string& text);

// --- Signing in --------------------------------------------------------------
//
// Google's device flow refuses the Tasks scope, so the reader runs the
// installed-app flow itself. It makes a PKCE verifier and a state, a phone
// opens the consent URL from the reader's own Wi-Fi page, and Google sends the
// phone's browser to http://127.0.0.1 -- which fails to load on the phone, with
// the one-time code in its address. The person pastes that address back into
// the reader's page, and the reader trades the code for a refresh token.

constexpr const char* kRedirectUri = "http://127.0.0.1:1";
constexpr const char* kScopes = "openid email https://www.googleapis.com/auth/tasks";

// base64url(sha256(verifier)), unpadded: PKCE's S256 challenge.
std::string pkceChallenge(const std::string& verifier);

// The consent URL a phone opens.
std::string authUrl(const std::string& clientId, const std::string& challenge, const std::string& state);

// What a pasted address said. `code` is set on success; otherwise `message` is
// a sentence for the page. The address is whatever the person copied: the
// whole URL, or just its query, with or without stray spaces.
struct Pasted {
  std::string code;
  std::string message;
  bool ok() const { return !code.empty(); }
};
Pasted parsePasted(const std::string& pasted, const std::string& expectedState);

// The `email` claim of an id_token, read without verifying its signature.
// That is allowed only because it arrives straight from Google's token
// endpoint over TLS in answer to our own code exchange (OpenID Connect Core
// 3.1.3.7); "" when it cannot be read.
std::string idTokenEmail(const std::string& idToken);

// --- The cache -------------------------------------------------------------

// One task per line, tab-separated, with a version line first:
//   gtasks 1
//   <id>\t<pending 0|1>\t<position>\t<parent>\t<due>\t<title>
// Tabs and newlines in a title become spaces on the way in. A line that does
// not parse is dropped rather than failing the file, and a file without the
// version line is not ours and parses as empty.
std::string serializeTasks(const std::vector<Task>& tasks);
std::vector<Task> parseTasks(const std::string& text);

// Every list on the account, in Google's order, with how many open tasks it had
// at the last sync (for the switcher). One per line after a version line:
//   gtasks-lists 1
//   <id>\t<open>\t<title>
// A list whose id fails safeId() is dropped: the id names a file on the card
// and goes into a URL path.
struct TaskList {
  std::string id;
  std::string title;
  int open = 0;
};
std::string serializeLists(const std::vector<TaskList>& lists);
std::vector<TaskList> parseLists(const std::string& text);

// The list shown while the reader sleeps (Settings > Sleep screen > Tasks), and
// the sleep settings it replaced so turning it off puts them back. -1 is "not
// recorded".
//   list=<id>
//   previous_mode=<n>
//   previous_quick=<n>
struct Asleep {
  std::string listId;
  int previousMode = -1;
  int previousQuick = -1;
};
bool parseAsleep(const std::string& text, Asleep& out);
std::string serializeAsleep(const Asleep& asleep);

// Parents with no due date first, then by due date, then Google's position;
// each followed by its own children in the same order. A child whose parent is
// not in the list is drawn as a parent.
void sortForDisplay(std::vector<Task>& tasks);

// Indices into `tasks` (already sorted) that the list draws. `today` is
// "YYYY-MM-DD", or empty for every task. With a date: undated tasks, tasks due
// that day or earlier, and the parent of any child that is shown.
std::vector<int> visibleRows(const std::vector<Task>& tasks, const std::string& today);

// Below this, time() is not a date but a clock that was never set (2023-11-14,
// the floor Study and Instapaper use).
constexpr int64_t kClockFloor = 1700000000;

// "YYYY-MM-DD" in local time, or "" when the clock was never set.
std::string localDate(int64_t epoch);

// True for a task drawn indented.
bool isChild(const Task& task, const std::vector<Task>& all);

// The list after a sync. `fresh` is every OPEN task Google returned; `local`
// is the card. A tick still pending survives (the push failed, so the task is
// still open on Google and must stay ticked here until it goes up). A tick on
// a task Google no longer lists is dropped: it was completed or deleted
// somewhere else, and either way there is nothing left to send.
std::vector<Task> merge(const std::vector<Task>& local, const std::vector<Task>& fresh);

std::vector<std::string> pendingIds(const std::vector<Task>& tasks);
int pendingCount(const std::vector<Task>& tasks);

// "DUE 7 OCT" from "2026-10-07", "" for anything else. Upper case because the
// Toybox cuts are drawn in capitals everywhere else on these screens.
std::string dueLabel(const std::string& due);

// Google's `due` is RFC 3339 ("2026-10-07T00:00:00.000Z"); the date is all it
// keeps, so the date is all this keeps.
std::string dueDate(const std::string& rfc3339);

// --- Settings --------------------------------------------------------------

// Minutes between polls while the reader is on the charger. 0 is OFF.
constexpr uint16_t kPollChoices[] = {1, 2, 5, 10, 15, 30, 60, 0};
constexpr uint16_t kDefaultPollMinutes = 1;

struct Settings {
  uint16_t pollMinutes = kDefaultPollMinutes;
  // Show only undated tasks and those due today or earlier.
  bool todayOnly = false;
};

// `poll_minutes=N` and `today_only=0|1`. A value that is not one of kPollChoices falls back to the
// default rather than to whatever the file said, so a typo on the card cannot
// make the reader poll every second.
Settings parseSettings(const std::string& text);
std::string serializeSettings(const Settings& settings);

// The choice after `minutes` in kPollChoices, wrapping. Unknown values step to
// the first choice.
uint16_t nextPollMinutes(uint16_t minutes);

// "EVERY MIN", "EVERY 5 MIN", "EVERY HOUR", "OFF".
std::string pollLabel(uint16_t minutes);

// Is a background poll due? Only while charging, only when polling is on, and
// only once the interval has passed since the last ATTEMPT -- not the last
// success, or a dead network would be retried every loop pass. `everAttempted`
// false means "now".
bool pollDue(bool charging, uint16_t pollMinutes, bool everAttempted, uint32_t nowMs, uint32_t lastAttemptMs);

// --- Wire helpers ----------------------------------------------------------

// application/x-www-form-urlencoded, for Google's token endpoint.
std::string formEncode(const std::string& value);

// A task id goes into a URL path. Google's are base64url, but nothing here
// relies on that: anything outside [A-Za-z0-9_-] is refused, so an id can
// never spell a different path.
bool safeId(const std::string& id);

}  // namespace gtasks
