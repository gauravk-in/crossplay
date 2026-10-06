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
// Sign-in is not here. Google's device flow refuses the Tasks scope, so a
// small service (server/tasks-bridge) holds the Google grant; the card holds
// only that service's device token, in auth.cfg, and trades it for an hour's
// access token whenever a sync needs one.
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

// What auth.cfg carries, one `key=value` per line: the sign-in service's
// device token for this reader, and the Google address it was paired to (shown
// on the sign-out confirm). The reader never holds Google's refresh token.
struct Credentials {
  std::string deviceToken;
  std::string account;
  bool complete() const { return !deviceToken.empty(); }
};

// Lines that are blank, `#` comments or unknown keys are skipped; a CR from a
// file saved on Windows is not part of a value. Fails OPEN: whatever is
// missing simply leaves complete() false, and the app asks to sign in.
Credentials parseCredentials(const std::string& text);
std::string serializeCredentials(const Credentials& creds);

// bridge.cfg's `host=`: where the sign-in service lives, overriding the one
// compiled in. "" when absent or when the value is not a plain host name, so a
// stray scheme, path or space falls back rather than building a broken URL.
std::string parseBridgeHost(const std::string& text);

// --- The cache -------------------------------------------------------------

// One task per line, tab-separated, with a version line first:
//   gtasks 1
//   <id>\t<pending 0|1>\t<position>\t<parent>\t<due>\t<title>
// Tabs and newlines in a title become spaces on the way in. A line that does
// not parse is dropped rather than failing the file, and a file without the
// version line is not ours and parses as empty.
std::string serializeTasks(const std::vector<Task>& tasks);
std::vector<Task> parseTasks(const std::string& text);

// Google's order: parents by position, each followed by its own children by
// position. A child whose parent is not in the list is drawn as a parent.
void sortForDisplay(std::vector<Task>& tasks);

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
};

// `poll_minutes=N`. A value that is not one of kPollChoices falls back to the
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
