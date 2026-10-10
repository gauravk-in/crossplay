// Host tests for Google Calendar: what Google's timestamps mean, the card's
// cache file, the Schedule view built from it, and paging through it.
//
// Every failure here is silent on the device. A misread offset moves a meeting
// by an hour; a schedule that drops the last day of a trip hides it; a page
// that does not move backwards strands the reader on today.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/apps_local/gcal/GCalCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

void checkInt(const long long actual, const long long expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n  expected [%lld]\n  actual   [%lld]\n", line, what, expected, actual);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)
#define CHECK_INT(actual, expected) checkInt((actual), (expected), #actual, __LINE__)

using gcal::Event;
using gcal::Item;

int64_t day(const int y, const int m, const int d) { return gcal::daysFromCivil(y, m, d); }

int64_t at(const char* rfc3339) {
  int64_t out = 0;
  if (!gcal::parseDateTime(rfc3339, out)) std::printf("bad fixture %s\n", rfc3339);
  return out;
}

Event timed(const char* title, const char* start, const char* end) {
  Event e;
  e.title = title;
  e.start = at(start);
  e.end = at(end);
  return e;
}

Event allDay(const char* title, const int64_t from, const int64_t toExclusive) {
  Event e;
  e.allDay = true;
  e.title = title;
  e.start = from;
  e.end = toExclusive;
  return e;
}

void setZone(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

void testCivilDays() {
  CHECK_INT(day(1970, 1, 1), 0);
  CHECK_INT(day(2000, 3, 1), 11017);
  int y = 0;
  int m = 0;
  int d = 0;
  gcal::civilFromDays(day(2028, 2, 29), y, m, d);
  CHECK_INT(y, 2028);
  CHECK_INT(m, 2);
  CHECK_INT(d, 29);
  gcal::civilFromDays(-1, y, m, d);
  CHECK_INT(y, 1969);
  CHECK_INT(d, 31);
  CHECK_INT(gcal::weekdayOf(0), 4);                 // a Thursday
  CHECK_INT(gcal::weekdayOf(day(2026, 10, 6)), 2);  // a Tuesday
  CHECK_EQ(std::string(gcal::weekdayShort(day(2026, 10, 6))), "TUE");
}

void testParseDate() {
  int64_t out = 0;
  CHECK(gcal::parseDate("2026-10-07", out));
  CHECK_INT(out, day(2026, 10, 7));
  CHECK(!gcal::parseDate("2026-02-30", out));
  CHECK(!gcal::parseDate("2026-1-07", out));
  CHECK(!gcal::parseDate("2026-10-07T00:00:00Z", out));
  CHECK(!gcal::parseDate("", out));
}

void testParseDateTime() {
  int64_t out = 0;
  CHECK(gcal::parseDateTime("1970-01-01T00:00:00Z", out));
  CHECK_INT(out, 0);
  CHECK(gcal::parseDateTime("2026-10-07T09:30:00+02:00", out));
  CHECK_INT(out, day(2026, 10, 7) * 86400 + 7 * 3600 + 30 * 60);
  CHECK(gcal::parseDateTime("2026-10-07T09:30:00-05:30", out));
  CHECK_INT(out, day(2026, 10, 7) * 86400 + 15 * 3600);
  // Fractional seconds are dropped, not refused.
  CHECK(gcal::parseDateTime("2026-10-07T09:30:00.123Z", out));
  CHECK_INT(out, day(2026, 10, 7) * 86400 + 9 * 3600 + 30 * 60);
  CHECK(!gcal::parseDateTime("2026-10-07T09:30:00", out));  // no zone: ambiguous
  CHECK(!gcal::parseDateTime("2026-10-07T25:00:00Z", out));
  CHECK(!gcal::parseDateTime("2026-10-07", out));
  CHECK(!gcal::parseDateTime("2026-10-07T09:30:00+0200", out));
  CHECK_EQ(gcal::formatDateTimeUtc(at("2026-10-07T09:30:05+02:00")), "2026-10-07T07:30:05Z");
}

void testToLocal() {
  setZone("UTC0");
  gcal::LocalTime t = gcal::toLocal(at("2026-10-07T23:30:00Z"));
  CHECK_INT(t.day, day(2026, 10, 7));
  CHECK_INT(t.minute, 23 * 60 + 30);
  // The same instant in Berlin (summer time) is the next morning.
  setZone("CET-1CEST,M3.5.0,M10.5.0/3");
  t = gcal::toLocal(at("2026-10-07T23:30:00Z"));
  CHECK_INT(t.day, day(2026, 10, 8));
  CHECK_INT(t.minute, 1 * 60 + 30);
  setZone("UTC0");
}

void testCacheRoundTrip() {
  std::vector<Event> events;
  events.push_back(timed("Stand-up\twith\nthe team", "2026-10-07T09:00:00Z", "2026-10-07T09:15:00Z"));
  events.back().location = "Room 4";
  events.push_back(allDay("Holiday", day(2026, 10, 8), day(2026, 10, 9)));
  const std::vector<Event> back = gcal::parseEvents(gcal::serializeEvents(events));
  CHECK_INT(static_cast<long long>(back.size()), 2);
  CHECK_EQ(back[0].title, "Stand-up with the team");
  CHECK_EQ(back[0].location, "Room 4");
  CHECK_INT(back[0].start, events[0].start);
  CHECK(!back[0].allDay);
  CHECK(back[1].allDay);
  CHECK_INT(back[1].end, day(2026, 10, 9));
}

void testCacheRejects() {
  CHECK(gcal::parseEvents("").empty());
  CHECK(gcal::parseEvents("gtasks 1\n0\t1\t2\tx\t\n").empty());
  CHECK(gcal::parseEvents("gcal 2\n0\t1\t2\tx\t\n").empty());
  // Bad lines drop; good ones around them survive, CRLF included.
  const std::vector<Event> e = gcal::parseEvents(
      "gcal 1\r\n0\t10\t20\tGood\t\r\nx\t1\t2\ta\tb\n0\t30\t20\tBackwards\t\n1\t5\t6\tShort\n0\t1\t2\tLast\tHere\n");
  CHECK_INT(static_cast<long long>(e.size()), 2);
  CHECK_EQ(e[0].title, "Good");
  CHECK_EQ(e[1].location, "Here");
}

void testSort() {
  std::vector<Event> events;
  events.push_back(timed("Late", "2026-10-07T15:00:00Z", "2026-10-07T16:00:00Z"));
  events.push_back(timed("Early", "2026-10-07T08:00:00Z", "2026-10-07T09:00:00Z"));
  events.push_back(allDay("Birthday", day(2026, 10, 7), day(2026, 10, 8)));
  events.push_back(timed("Yesterday", "2026-10-06T20:00:00Z", "2026-10-06T21:00:00Z"));
  gcal::sortEvents(events);
  CHECK_EQ(events[0].title, "Yesterday");
  CHECK_EQ(events[1].title, "Birthday");
  CHECK_EQ(events[2].title, "Early");
  CHECK_EQ(events[3].title, "Late");
}

std::string describe(const std::vector<Item>& items, const std::vector<Event>& events) {
  std::string out;
  for (const Item& it : items) {
    if (it.kind == Item::Kind::Month) {
      out += "[" + gcal::monthTitle(it.day) + "]";
    } else if (it.kind == Item::Kind::Nothing) {
      out += "<" + gcal::dayLabel(it.day) + ": nothing>";
    } else {
      out += it.firstOfDay ? "<" + gcal::dayLabel(it.day) + ">" : std::string();
      out += events[static_cast<size_t>(it.event)].title;
      if (!it.when.empty()) out += "@" + it.when;
      if (it.allDayRow) out += "*";
    }
    out += " ";
  }
  return out;
}

void testSchedule() {
  setZone("UTC0");
  std::vector<Event> events;
  events.push_back(timed("Dentist", "2026-10-07T09:30:00Z", "2026-10-07T10:15:00Z"));
  events.push_back(allDay("Trip", day(2026, 10, 9), day(2026, 10, 12)));  // 9th, 10th, 11th
  events.push_back(timed("Night train", "2026-10-30T22:00:00Z", "2026-11-01T07:00:00Z"));
  events.push_back(timed("Reminder", "2026-10-07T08:00:00Z", "2026-10-07T08:00:00Z"));
  events.push_back(timed("Evening", "2026-10-08T22:00:00Z", "2026-10-09T00:00:00Z"));
  gcal::sortEvents(events);
  const std::vector<Item> items = gcal::buildSchedule(events, day(2026, 10, 1), day(2026, 11, 30), day(2026, 10, 6));
  CHECK_EQ(describe(items, events),
           "<TUE 6 OCT: nothing> <WED 7 OCT>Reminder@08:00 Dentist@09:30 - 10:15 <THU 8 OCT>Evening@22:00 - 00:00 "
           "<FRI 9 OCT>Trip@DAY 1 / 3* <SAT 10 OCT>Trip@DAY 2 / 3* <SUN 11 OCT>Trip@DAY 3 / 3* "
           "<FRI 30 OCT>Night train@FROM 22:00 <SAT 31 OCT>Night train@ALL DAY* [NOVEMBER 2026] "
           "<SUN 1 NOV>Night train@UNTIL 07:00 ");
  CHECK_INT(gcal::indexOfDay(items, day(2026, 10, 7)), 1);
  // A day with nothing lands on the next day that has something.
  CHECK_INT(gcal::indexOfDay(items, day(2026, 10, 12)), 7);
  // The first day of a month brings its banner with it.
  CHECK_INT(gcal::indexOfDay(items, day(2026, 11, 1)), 9);
  CHECK_INT(gcal::indexOfDay(items, day(2027, 1, 1)), static_cast<long long>(items.size()));
}

void testScheduleClipsAndOrders() {
  setZone("UTC0");
  std::vector<Event> events;
  events.push_back(allDay("Long", day(2026, 9, 1), day(2026, 12, 1)));
  events.push_back(timed("Lunch", "2026-10-07T12:00:00Z", "2026-10-07T13:00:00Z"));
  events.push_back(timed("Overnight", "2026-10-06T23:00:00Z", "2026-10-07T01:00:00Z"));
  gcal::sortEvents(events);
  const std::vector<Item> items = gcal::buildSchedule(events, day(2026, 10, 7), day(2026, 10, 7), day(2026, 10, 7));
  // All-day first, then what started yesterday, then the day's own events.
  CHECK_EQ(describe(items, events), "<WED 7 OCT>Long@DAY 37 / 91* Overnight@UNTIL 01:00 Lunch@12:00 - 13:00 ");
  // Today outside the window is not invented.
  CHECK(gcal::buildSchedule({}, day(2026, 10, 7), day(2026, 10, 8), day(2026, 11, 1)).empty());
}

void testScheduleFollowsZone() {
  // 23:30 UTC is tomorrow morning in Berlin, and the schedule says so.
  setZone("CET-1CEST,M3.5.0,M10.5.0/3");
  std::vector<Event> events;
  events.push_back(timed("Call", "2026-10-07T23:30:00Z", "2026-10-08T00:00:00Z"));
  const std::vector<Item> items = gcal::buildSchedule(events, day(2026, 10, 1), day(2026, 10, 31), day(2026, 10, 1));
  CHECK_EQ(describe(items, events), "<THU 1 OCT: nothing> <THU 8 OCT>Call@01:30 - 02:00 ");
  setZone("UTC0");
}

void testPaging() {
  const std::vector<int> h = {30, 30, 50, 30, 30, 30, 200, 30};
  CHECK_INT(gcal::fitFrom(h, 0, 100), 2);
  CHECK_INT(gcal::fitFrom(h, 2, 100), 2);
  CHECK_INT(gcal::fitFrom(h, 6, 100), 1);  // taller than a page still shows
  CHECK_INT(gcal::fitFrom(h, 8, 100), 0);
  CHECK_INT(gcal::pageBefore(h, 0, 100), 0);
  CHECK_INT(gcal::pageBefore(h, 2, 100), 0);
  CHECK_INT(gcal::pageBefore(h, 4, 100), 2);
  CHECK_INT(gcal::pageBefore(h, 6, 100), 3);
  CHECK_INT(gcal::pageBefore(h, 7, 100), 6);
  // Forward then back returns to where it started when pages are full.
  int first = 3;
  first += gcal::fitFrom(h, first, 100);
  CHECK_INT(gcal::pageBefore(h, first, 100), 3);
}

void testSettingsAndMeta() {
  CHECK_INT(gcal::parseSettings("").pollMinutes, gcal::kDefaultPollMinutes);
  CHECK_INT(gcal::parseSettings("poll_minutes=15\n").pollMinutes, 15);
  CHECK_INT(gcal::parseSettings("poll_minutes=0\n").pollMinutes, 0);
  CHECK_INT(gcal::parseSettings("poll_minutes=3\n").pollMinutes, gcal::kDefaultPollMinutes);
  CHECK_INT(gcal::parseSettings(gcal::serializeSettings(gcal::Settings{30})).pollMinutes, 30);

  gcal::Meta m;
  m.lastSyncAt = 1791300000;
  m.calendars = 3;
  const gcal::Meta back = gcal::parseMeta(gcal::serializeMeta(m));
  CHECK_INT(back.lastSyncAt, m.lastSyncAt);
  CHECK_INT(back.calendars, 3);

  gcal::Asleep a;
  CHECK(!gcal::parseAsleep("", a));
  a.previousMode = 9;
  a.previousQuick = 1;
  gcal::Asleep b;
  CHECK(gcal::parseAsleep(gcal::serializeAsleep(a), b));
  CHECK_INT(b.previousMode, 9);
  CHECK_INT(b.previousQuick, 1);
}

void testLabels() {
  CHECK_EQ(gcal::monthTitle(day(2026, 10, 6)), "OCTOBER 2026");
  CHECK_EQ(gcal::dayNumber(day(2026, 10, 6)), "6");
  CHECK_EQ(gcal::dayLabel(day(2026, 12, 25)), "FRI 25 DEC");
  CHECK_EQ(gcal::pathEncode("en.usa#holiday@group.v.calendar.google.com"),
           "en.usa%23holiday%40group.v.calendar.google.com");
  CHECK_EQ(gcal::pathEncode("a/b c"), "a%2Fb%20c");
}

std::string slurp(const char* path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// Every Google zone the reader knows lands on a zone the Clock settings list,
// by its exact name, so adopting it can never pick nothing.
void testAccountZones() {
  CHECK(gcal::clockZoneForGoogle("Europe/Berlin") != nullptr);
  checkEqual(gcal::clockZoneForGoogle("Europe/Berlin"), "Berlin / Paris / Madrid / Rome", "Berlin", __LINE__);
  checkEqual(gcal::clockZoneForGoogle("Asia/Kolkata"), "India / Colombo", "Kolkata", __LINE__);
  checkEqual(gcal::clockZoneForGoogle("America/New_York"), "New York / Toronto", "New York", __LINE__);
  CHECK(gcal::clockZoneForGoogle("Mars/Olympus_Mons") == nullptr);
  CHECK(gcal::clockZoneForGoogle("") == nullptr);

  const std::string core = slurp("../../src/apps_local/gcal/GCalCore.cpp");
  const std::string clock = slurp("../../src/util/Timezones.cpp");
  CHECK(!core.empty() && !clock.empty());
  const std::regex alias("\\{\"([A-Za-z_/]+)\", \"([^\"]+)\"\\}");
  int aliases = 0;
  for (std::sregex_iterator it(core.begin(), core.end(), alias), end; it != end; ++it) {
    const std::string iana = (*it)[1];
    const std::string zone = (*it)[2];
    ++aliases;
    const char* found = gcal::clockZoneForGoogle(iana);
    check(found != nullptr && zone == found, iana.c_str(), __LINE__);
    check(clock.find("{\"" + zone + "\", ") != std::string::npos, zone.c_str(), __LINE__);
  }
  CHECK(aliases > 100);
}

}  // namespace

int main() {
  testCivilDays();
  testParseDate();
  testParseDateTime();
  testToLocal();
  testCacheRoundTrip();
  testCacheRejects();
  testSort();
  testSchedule();
  testScheduleClipsAndOrders();
  testScheduleFollowsZone();
  testPaging();
  testSettingsAndMeta();
  testLabels();
  testAccountZones();
  std::printf("gcal: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
