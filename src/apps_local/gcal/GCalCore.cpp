#include "GCalCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>

#include "../gtasks/GTasksCore.h"

namespace gcal {
namespace {

constexpr const char* kEventsHeader = "gcal 1";

constexpr const char* kMonths[] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                   "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
constexpr const char* kMonthsShort[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr const char* kWeekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

std::string trimCr(const std::string& line) {
  if (!line.empty() && line.back() == '\r') return line.substr(0, line.size() - 1);
  return line;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t nl = text.find('\n', start);
    const size_t end = nl == std::string::npos ? text.size() : nl;
    lines.push_back(trimCr(text.substr(start, end - start)));
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return lines;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) break;
    start = tab + 1;
  }
  return fields;
}

std::string flatten(const std::string& value) {
  std::string out = value;
  for (char& c : out) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return out;
}

bool parseInt64(const std::string& text, int64_t& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const long long v = std::strtoll(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') return false;
  out = static_cast<int64_t>(v);
  return true;
}

// `digits` decimal digits at text[pos], or -1.
int digitsAt(const std::string& text, const size_t pos, const int digits) {
  if (pos + static_cast<size_t>(digits) > text.size()) return -1;
  int v = 0;
  for (int i = 0; i < digits; ++i) {
    const char c = text[pos + static_cast<size_t>(i)];
    if (c < '0' || c > '9') return -1;
    v = v * 10 + (c - '0');
  }
  return v;
}

int daysInMonth(const int year, const int month) {
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
  return kDays[month - 1];
}

bool validDate(const int y, const int m, const int d) {
  return y >= 1900 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}

std::string hhmm(const int minute) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", (minute / 60) % 24, minute % 60);
  return buf;
}

// Where an event sits on the reader's days: the first and last local day it
// touches (inclusive), and its start and end as local times.
struct Span {
  int64_t firstDay = 0;
  int64_t lastDay = 0;
  LocalTime start;
  LocalTime end;
};

Span spanOf(const Event& e) {
  Span s;
  if (e.allDay) {
    s.firstDay = e.start;
    s.lastDay = e.end > e.start ? e.end - 1 : e.start;
    return s;
  }
  s.start = toLocal(e.start);
  s.end = toLocal(e.end);
  s.firstDay = s.start.day;
  // An event that ends at midnight ends on the day before: 22:00 to 00:00 is
  // one evening, not two days.
  s.lastDay = e.end > e.start ? toLocal(e.end - 1).day : s.firstDay;
  if (s.lastDay < s.firstDay) s.lastDay = s.firstDay;
  return s;
}

struct Occurrence {
  int event = 0;
  bool allDayRow = false;
  int sortMinute = 0;
  std::string when;
};

}  // namespace

// --- Days and times --------------------------------------------------------

// Howard Hinnant's days_from_civil and civil_from_days.
int64_t daysFromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const int64_t yoe = year - era * 400;
  const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

int weekdayOf(const int64_t days) {
  const int64_t w = (days + 4) % 7;  // 1970-01-01 was a Thursday
  return static_cast<int>(w < 0 ? w + 7 : w);
}

bool parseDate(const std::string& text, int64_t& out) {
  if (text.size() < 10 || text[4] != '-' || text[7] != '-') return false;
  const int y = digitsAt(text, 0, 4);
  const int m = digitsAt(text, 5, 2);
  const int d = digitsAt(text, 8, 2);
  if (y < 0 || m < 0 || d < 0 || !validDate(y, m, d)) return false;
  if (text.size() > 10) return false;
  out = daysFromCivil(y, m, d);
  return true;
}

bool parseDateTime(const std::string& text, int64_t& out) {
  // YYYY-MM-DDTHH:MM:SS, then optional .fff, then Z or +HH:MM / -HH:MM.
  if (text.size() < 20 || text[10] != 'T' || text[13] != ':' || text[16] != ':') return false;
  int64_t day = 0;
  if (!parseDate(text.substr(0, 10), day)) return false;
  const int h = digitsAt(text, 11, 2);
  const int mi = digitsAt(text, 14, 2);
  const int s = digitsAt(text, 17, 2);
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) return false;
  size_t pos = 19;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
  }
  if (pos >= text.size()) return false;
  int offsetSeconds = 0;
  if (text[pos] == 'Z' || text[pos] == 'z') {
    if (pos + 1 != text.size()) return false;
  } else if (text[pos] == '+' || text[pos] == '-') {
    if (pos + 6 != text.size() || text[pos + 3] != ':') return false;
    const int oh = digitsAt(text, pos + 1, 2);
    const int om = digitsAt(text, pos + 4, 2);
    if (oh < 0 || oh > 23 || om < 0 || om > 59) return false;
    offsetSeconds = (oh * 60 + om) * 60 * (text[pos] == '-' ? -1 : 1);
  } else {
    return false;
  }
  out = day * 86400 + h * 3600 + mi * 60 + s - offsetSeconds;
  return true;
}

std::string formatDateTimeUtc(const int64_t epoch) {
  int64_t day = epoch / 86400;
  int64_t rest = epoch % 86400;
  if (rest < 0) {
    rest += 86400;
    --day;
  }
  int y = 0;
  int m = 0;
  int d = 0;
  civilFromDays(day, y, m, d);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, static_cast<int>(rest / 3600),
                static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60));
  return buf;
}

LocalTime toLocal(const int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm parts{};
  localtime_r(&t, &parts);
  LocalTime out;
  out.day = daysFromCivil(parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
  out.minute = parts.tm_hour * 60 + parts.tm_min;
  return out;
}

// --- Events ----------------------------------------------------------------

std::string serializeEvents(const std::vector<Event>& events) {
  std::string out = kEventsHeader;
  out += '\n';
  for (const Event& e : events) {
    char head[64];
    std::snprintf(head, sizeof(head), "%d\t%lld\t%lld\t", e.allDay ? 1 : 0, static_cast<long long>(e.start),
                  static_cast<long long>(e.end));
    out += head;
    out += flatten(e.title);
    out += '\t';
    out += flatten(e.location);
    out += '\n';
  }
  return out;
}

std::vector<Event> parseEvents(const std::string& text) {
  std::vector<Event> out;
  const std::vector<std::string> lines = splitLines(text);
  if (lines.empty() || lines.front() != kEventsHeader) return out;
  out.reserve(lines.size());
  for (size_t i = 1; i < lines.size(); ++i) {
    if (lines[i].empty()) continue;
    const std::vector<std::string> f = splitTabs(lines[i]);
    if (f.size() < 5) continue;
    Event e;
    if (f[0] != "0" && f[0] != "1") continue;
    e.allDay = f[0] == "1";
    if (!parseInt64(f[1], e.start) || !parseInt64(f[2], e.end) || e.end < e.start) continue;
    e.title = f[3];
    e.location = f[4];
    out.push_back(std::move(e));
  }
  return out;
}

void sortEvents(std::vector<Event>& events) {
  // An all-day event's start is a day number and a timed one's is seconds, so
  // they are compared on the day they begin, then all-day first.
  const auto key = [](const Event& e) { return e.allDay ? e.start : toLocal(e.start).day; };
  std::stable_sort(events.begin(), events.end(), [&key](const Event& a, const Event& b) {
    const int64_t da = key(a);
    const int64_t db = key(b);
    if (da != db) return da < db;
    if (a.allDay != b.allDay) return a.allDay;
    if (a.start != b.start) return a.start < b.start;
    if (a.end != b.end) return a.end < b.end;
    return a.title < b.title;
  });
}

// --- The schedule ----------------------------------------------------------

std::vector<Item> buildSchedule(const std::vector<Event>& events, const int64_t firstDay, const int64_t lastDay,
                                const int64_t today) {
  std::map<int64_t, std::vector<Occurrence>> days;
  for (size_t i = 0; i < events.size(); ++i) {
    const Event& e = events[i];
    const Span span = spanOf(e);
    const int64_t from = std::max(span.firstDay, firstDay);
    const int64_t to = std::min(span.lastDay, lastDay);
    const int64_t length = span.lastDay - span.firstDay + 1;
    for (int64_t day = from; day <= to; ++day) {
      Occurrence o;
      o.event = static_cast<int>(i);
      if (e.allDay) {
        o.allDayRow = true;
        if (length > 1) {
          char buf[48];
          std::snprintf(buf, sizeof(buf), "DAY %lld / %lld", static_cast<long long>(day - span.firstDay + 1),
                        static_cast<long long>(length));
          o.when = buf;
        }
      } else if (span.firstDay == span.lastDay) {
        o.sortMinute = span.start.minute;
        o.when = e.end > e.start ? hhmm(span.start.minute) + " - " + hhmm(span.end.minute) : hhmm(span.start.minute);
      } else if (day == span.firstDay) {
        o.sortMinute = span.start.minute;
        o.when = "FROM " + hhmm(span.start.minute);
      } else if (day == span.lastDay) {
        o.when = "UNTIL " + hhmm(span.end.minute);
      } else {
        o.allDayRow = true;
        o.when = "ALL DAY";
      }
      days[day].push_back(std::move(o));
    }
  }
  if (today >= firstDay && today <= lastDay) days[today];

  std::vector<Item> items;
  items.reserve(events.size() + days.size() + 8);
  int lastMonth = -1;
  int lastYear = -1;
  for (auto& entry : days) {
    const int64_t day = entry.first;
    std::vector<Occurrence>& occ = entry.second;
    std::stable_sort(occ.begin(), occ.end(), [](const Occurrence& a, const Occurrence& b) {
      if (a.allDayRow != b.allDayRow) return a.allDayRow;
      return a.sortMinute < b.sortMinute;
    });
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(day, y, m, d);
    // The header names the first month on the page; a banner marks every
    // month after it, as Google's Schedule does.
    if (lastMonth >= 0 && (m != lastMonth || y != lastYear)) {
      Item month;
      month.kind = Item::Kind::Month;
      month.day = daysFromCivil(y, m, 1);
      items.push_back(month);
    }
    lastMonth = m;
    lastYear = y;
    if (occ.empty()) {
      Item nothing;
      nothing.kind = Item::Kind::Nothing;
      nothing.day = day;
      nothing.firstOfDay = true;
      items.push_back(nothing);
      continue;
    }
    for (size_t i = 0; i < occ.size(); ++i) {
      Item item;
      item.kind = Item::Kind::Event;
      item.day = day;
      item.firstOfDay = i == 0;
      item.event = occ[i].event;
      item.allDayRow = occ[i].allDayRow;
      item.when = std::move(occ[i].when);
      items.push_back(std::move(item));
    }
  }
  return items;
}

int indexOfDay(const std::vector<Item>& items, const int64_t day) {
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].kind != Item::Kind::Month && items[i].day >= day) {
      // A month banner right before it belongs to the same page.
      if (i > 0 && items[i - 1].kind == Item::Kind::Month) return static_cast<int>(i - 1);
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(items.size());
}

std::string monthTitle(const int64_t day) {
  int y = 0;
  int m = 0;
  int d = 0;
  civilFromDays(day, y, m, d);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s %d", kMonths[m - 1], y);
  return buf;
}

const char* weekdayShort(const int64_t day) { return kWeekdays[weekdayOf(day)]; }

std::string dayNumber(const int64_t day) {
  int y = 0;
  int m = 0;
  int d = 0;
  civilFromDays(day, y, m, d);
  return std::to_string(d);
}

std::string dayLabel(const int64_t day) {
  int y = 0;
  int m = 0;
  int d = 0;
  civilFromDays(day, y, m, d);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s %d %s", weekdayShort(day), d, kMonthsShort[m - 1]);
  return buf;
}

// --- Paging ------------------------------------------------------------------

int fitFrom(const std::vector<int>& heights, const int first, const int pageHeight) {
  const int n = static_cast<int>(heights.size());
  if (first < 0 || first >= n) return 0;
  int used = 0;
  int count = 0;
  for (int i = first; i < n; ++i) {
    if (count > 0 && used + heights[static_cast<size_t>(i)] > pageHeight) break;
    used += heights[static_cast<size_t>(i)];
    ++count;
  }
  return count;
}

int pageBefore(const std::vector<int>& heights, const int first, const int pageHeight) {
  if (first <= 0) return 0;
  const int n = static_cast<int>(heights.size());
  int start = first > n ? n : first;
  int used = 0;
  while (start > 0) {
    const int h = heights[static_cast<size_t>(start - 1)];
    if (start < first && used + h > pageHeight) break;
    used += h;
    --start;
  }
  return start;
}

// --- Settings --------------------------------------------------------------

namespace {

// key=value lines; blanks, comments and unknown keys are skipped.
template <typename Fn>
void eachPair(const std::string& text, Fn fn) {
  for (const std::string& line : splitLines(text)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    fn(line.substr(0, eq), line.substr(eq + 1));
  }
}

}  // namespace

Settings parseSettings(const std::string& text) {
  Settings out;
  eachPair(text, [&out](const std::string& key, const std::string& value) {
    if (key != "poll_minutes") return;
    int64_t v = 0;
    if (!parseInt64(value, v)) return;
    for (const uint16_t choice : gtasks::kPollChoices) {
      if (v == choice) out.pollMinutes = choice;
    }
  });
  return out;
}

std::string serializeSettings(const Settings& settings) {
  return "poll_minutes=" + std::to_string(settings.pollMinutes) + "\n";
}

Meta parseMeta(const std::string& text) {
  Meta out;
  eachPair(text, [&out](const std::string& key, const std::string& value) {
    int64_t v = 0;
    if (!parseInt64(value, v)) return;
    if (key == "last_sync") out.lastSyncAt = v;
    if (key == "calendars") out.calendars = static_cast<int>(v);
  });
  return out;
}

std::string serializeMeta(const Meta& meta) {
  return "last_sync=" + std::to_string(meta.lastSyncAt) + "\ncalendars=" + std::to_string(meta.calendars) + "\n";
}

bool parseAsleep(const std::string& text, Asleep& out) {
  out = Asleep{};
  bool any = false;
  eachPair(text, [&out, &any](const std::string& key, const std::string& value) {
    int64_t v = 0;
    if (!parseInt64(value, v)) return;
    if (key == "previous_mode") {
      out.previousMode = static_cast<int>(v);
      any = true;
    }
    if (key == "previous_quick") {
      out.previousQuick = static_cast<int>(v);
      any = true;
    }
  });
  return any;
}

std::string serializeAsleep(const Asleep& asleep) {
  return "previous_mode=" + std::to_string(asleep.previousMode) +
         "\nprevious_quick=" + std::to_string(asleep.previousQuick) + "\n";
}

// --- Wire helpers ----------------------------------------------------------

std::string pathEncode(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (const char ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
        c == '-') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0f];
    }
  }
  return out;
}

}  // namespace gcal
