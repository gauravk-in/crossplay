#pragma once

// Google Calendar on the reader: the parts that need no device.
//
// Freestanding C++17 -- no Arduino, no storage, no JSON library -- so
// host-tests/gcal can ask it what Google's timestamps mean, what a cache file
// written by a later firmware does, and what the schedule looks like on a day
// with a three-day trip, a birthday and nothing else.
//
// ---------------------------------------------------------------------------
// The shape of it.
//
// The card holds every event from two weeks back to three months ahead, from
// every calendar the account shows in Google Calendar, as last synced. The
// screen is Google Calendar's Schedule view: one row per event, grouped under
// its day, days with nothing on them left out, a month's name where a month
// starts. It opens on today and pages forward and back from there.
//
// Calendar is read only. Nothing the reader does goes up to Google, so a sync
// is simply "read the window again and replace the card".
//
// The Google account is the one Tasks signed in (see GTasksCore.h, "Signing
// in"): the same client, the same auth.cfg, one sign-in that asks for both.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace gcal {

// --- Days and times --------------------------------------------------------

// Days since 1970-01-01 in the proleptic Gregorian calendar, the one every
// date here is counted in. Negative before 1970.
int64_t daysFromCivil(int year, int month, int day);
void civilFromDays(int64_t days, int& year, int& month, int& day);
// 0 is Sunday, as in struct tm.
int weekdayOf(int64_t days);

// "2026-10-07" as a day number; false for anything else.
bool parseDate(const std::string& text, int64_t& out);
// RFC 3339 as Google writes it ("2026-10-07T09:30:00+02:00", "...Z", with or
// without fractional seconds) as epoch seconds; false for anything else.
bool parseDateTime(const std::string& text, int64_t& out);
// The other way, in UTC with a Z, for timeMin and timeMax.
std::string formatDateTimeUtc(int64_t epoch);

// Where an instant falls on the reader's own clock: its local day and the
// minute of that day. Uses the process time zone (localtime_r), which the
// device sets from the Clock settings.
struct LocalTime {
  int64_t day = 0;
  int minute = 0;
};
LocalTime toLocal(int64_t epoch);

// --- Events ----------------------------------------------------------------

struct Event {
  // A whole-day event ("all day" in Google) counts in days; anything else in
  // epoch seconds. Both ends are exclusive, as Google's are: an all-day event
  // on the 7th runs from day(7th) to day(8th).
  bool allDay = false;
  int64_t start = 0;
  int64_t end = 0;
  std::string title;
  std::string location;
};

// One per line, tab-separated, with a version line first:
//   gcal 1
//   <allDay 0|1>\t<start>\t<end>\t<title>\t<location>
// Tabs and newlines in a title or a place become spaces on the way in. A line
// that does not parse is dropped rather than failing the file, and a file
// without the version line is not ours and parses as empty.
std::string serializeEvents(const std::vector<Event>& events);
std::vector<Event> parseEvents(const std::string& text);

// Google's order for one day: all-day events first, then by start, then by
// end, then by title, so the same calendar always draws the same way.
void sortEvents(std::vector<Event>& events);

// --- The schedule ----------------------------------------------------------

// One line of the Schedule view. The screen draws them top to bottom; paging
// is a matter of which item is first.
struct Item {
  enum class Kind : uint8_t {
    Month,    // "NOVEMBER 2026", where a new month starts
    Event,    // one event on one day
    Nothing,  // today, when nothing is on it
  };
  Kind kind = Kind::Event;
  int64_t day = 0;
  // The first row of its day, which carries the date in the left column.
  bool firstOfDay = false;
  // Kind::Event: the event (an index into the list passed in) and how it sits
  // on this day.
  int event = -1;
  bool allDayRow = false;  // drawn as a filled bar: all day, or a middle day
  // "09:30 - 10:15", "ALL DAY", "UNTIL 10:00", "FROM 22:00"; "" for an all-day
  // row that needs nothing under it.
  std::string when;
};

// Every item from `firstDay` to `lastDay` inclusive. Days with no events are
// left out, except `today`, which is always there so opening the app always
// lands somewhere. An event that spans days appears on each of them.
// `events` must be sorted with sortEvents.
std::vector<Item> buildSchedule(const std::vector<Event>& events, int64_t firstDay, int64_t lastDay, int64_t today);

// The first item on `day`, or the first after it; items.size() if none.
int indexOfDay(const std::vector<Item>& items, int64_t day);

// "OCTOBER 2026".
std::string monthTitle(int64_t day);
// "TUE".
const char* weekdayShort(int64_t day);
// "6".
std::string dayNumber(int64_t day);
// "TUE 6 OCT".
std::string dayLabel(int64_t day);

// --- Paging ------------------------------------------------------------------
//
// Items differ in height (a month banner is taller than an all-day bar), so a
// page is "as many as fit from here". `heights[i]` is item i's height, in the
// screen's units, laid out with no gaps.

// How many items fit starting at `first` (at least one, so an item taller than
// the page still shows), 0 when first is past the end.
int fitFrom(const std::vector<int>& heights, int first, int pageHeight);
// The first item of the page before the one starting at `first`: the earliest
// index whose items up to `first` fit. 0 at the top.
int pageBefore(const std::vector<int>& heights, int first, int pageHeight);

// --- The window --------------------------------------------------------------

// What a sync reads, in days around today. Back far enough to look up last
// week; forward far enough to plan a trip.
constexpr int kDaysBack = 14;
constexpr int kDaysAhead = 90;

// --- Settings --------------------------------------------------------------

// Minutes between polls on the charger. Tasks' choices; Calendar changes less
// often than a to-do list, so its default is longer.
constexpr uint16_t kDefaultPollMinutes = 5;

struct Settings {
  uint16_t pollMinutes = kDefaultPollMinutes;
};

// `poll_minutes=N`. A value that is not one of gtasks::kPollChoices falls back
// to the default.
Settings parseSettings(const std::string& text);
std::string serializeSettings(const Settings& settings);

// When the card last synced, and how many calendars it read.
//   last_sync=<epoch>
//   calendars=<n>
struct Meta {
  int64_t lastSyncAt = 0;
  int calendars = 0;
};
Meta parseMeta(const std::string& text);
std::string serializeMeta(const Meta& meta);

// The sleep settings the Calendar sleep screen replaced, so turning it off
// puts them back. -1 is "not recorded".
//   previous_mode=<n>
//   previous_quick=<n>
struct Asleep {
  int previousMode = -1;
  int previousQuick = -1;
};
bool parseAsleep(const std::string& text, Asleep& out);
std::string serializeAsleep(const Asleep& asleep);

// --- Wire helpers ----------------------------------------------------------

// A calendar id goes into a URL path. Ids are e-mail-like ("x@group.calendar.
// google.com", "en.usa#holiday@group.v.calendar.google.com"); everything but
// [A-Za-z0-9._-] is percent-encoded so an id can never spell a different path.
std::string pathEncode(const std::string& value);

}  // namespace gcal
