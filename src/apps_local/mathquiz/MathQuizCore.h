#pragma once

// Math Quiz: arithmetic drills for school kids, levelled by school class.
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card -- so that
// host-tests/mathquiz can drive every rule on a laptop. The activity reads and
// writes the bytes these functions produce, and nothing else.
//
// Questions are made on the device from a seeded generator, so there is no
// pack to fetch and a class never runs out. Each class has its own mix (see
// makeQuestion); the four choices are the answer plus three near misses of the
// kind a child actually makes, so guessing the odd one out does not work.
//
// History is one line per day, `day|asked|right|ms`, in
// /.crosspoint/mathquiz/history.txt. `ms` is the total answering time that day,
// so the day's average is ms / asked and adding a question is two additions.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mathquiz {

constexpr int kGrades = 6;
constexpr int kChoices = 4;
// The charts show this many days: today and the thirteen before it.
constexpr int kWindowDays = 14;
// A question left on screen while the kid wanders off would swamp the day's
// average; past this an answer counts as this long.
constexpr uint32_t kMaxAnswerMs = 60000;
// Years of daily practice at about twenty bytes a line.
constexpr size_t kMaxHistoryBytes = 32 * 1024;
constexpr size_t kMaxHistoryDays = 1200;

// xorshift32: small, deterministic, and good enough to pick sums.
class Rng {
 public:
  explicit Rng(uint32_t seed = 1) : s_(seed ? seed : 1) {}
  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }
  // Inclusive both ends.
  int range(int lo, int hi) {
    return hi <= lo ? lo : lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
  }

 private:
  uint32_t s_;
};

// The question is a row of space-separated tokens. Numbers and words are drawn
// as text; the operator tokens "+", "-", "*", "/" and "=" are drawn as shapes,
// because the UI faces carry ASCII only and a child should see x and the
// division sign, not a star and a slash.
constexpr size_t kTextBytes = 40;

struct Question {
  char text[kTextBytes] = {};
  int answer = 0;
  int choice[kChoices] = {};
  int correct = 0;      // index into choice
  bool tenths = false;  // every value is in tenths and prints with one decimal
};

Question makeQuestion(int grade, Rng& rng);

// A value as the kid reads it: "42", "-7", "3.5".
void formatValue(int value, bool tenths, char* out, size_t capacity);

// --- History -------------------------------------------------------------

struct DayRecord {
  int day = 0;  // days since 1970-01-01, local
  int asked = 0;
  int right = 0;
  uint32_t totalMs = 0;
};

// Lenient: a malformed line is skipped, days come back sorted and unique (a
// repeated day is merged), and at most kMaxHistoryDays of the newest are kept.
std::vector<DayRecord> parseHistory(const std::string& text);
std::string formatHistory(const std::vector<DayRecord>& days);

// One answered question on `day`. Keeps `days` sorted.
void addAnswer(std::vector<DayRecord>& days, int day, bool right, uint32_t ms);

// --- The charts ----------------------------------------------------------

struct ChartDay {
  int day = 0;
  bool has = false;   // any questions answered that day
  int percent = 0;    // 0..100, rounded
  int tenthsSec = 0;  // average seconds per question, in tenths
};

// Fourteen days ending on `lastDay`. `highlight` is the day drawn solid and
// compared against the rest -- today on the newest page, nothing (-1) on an
// older one. The averages are over every question in the window except the
// highlighted day's, weighted by question rather than by day, so a day of two
// questions does not count as much as a day of forty.
struct Window {
  int firstDay = 0;
  int lastDay = 0;
  int highlight = -1;
  ChartDay days[kWindowDays];
  bool hasAverage = false;
  int avgPercent = 0;
  int avgTenthsSec = 0;
  bool hasOlder = false;  // something was answered before firstDay
};

// Page 0 ends today; page n ends 14 * n days earlier.
Window windowFor(const std::vector<DayRecord>& days, int today, int page);

// Rounded halves away from zero: right * 100 / asked, and ms / asked in tenths.
int percentOf(int right, int asked);
int tenthsOf(uint32_t totalMs, int asked);

// Civil calendar arithmetic (Howard Hinnant's algorithms), so a day number
// needs no libc and no timezone conversation.
int daysFromCivil(int year, int month, int day);
void civilFromDays(int days, int& year, int& month, int& day);
int weekdayOf(int days);  // 0 Monday .. 6 Sunday

}  // namespace mathquiz
