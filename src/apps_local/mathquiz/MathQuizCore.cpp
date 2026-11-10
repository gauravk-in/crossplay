#include "MathQuizCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace mathquiz {

namespace {

// Up to this many near misses are offered per question; three are kept.
constexpr int kMaxCandidates = 10;

struct Build {
  Question q;
  int candidate[kMaxCandidates] = {};
  int candidates = 0;
  bool allowNegative = false;

  void miss(const int value) {
    if (candidates < kMaxCandidates) candidate[candidates++] = value;
  }
};

bool usable(const Build& b, const int value, const int* taken, const int count) {
  if (value == b.q.answer) return false;
  if (!b.allowNegative && value < 0) return false;
  for (int i = 0; i < count; ++i) {
    if (taken[i] == value) return false;
  }
  return true;
}

// Three wrong choices, preferring the near misses in the order they were
// offered after a shuffle, then plain neighbours of the answer. The answer
// lands in a random slot.
Question finish(Build& b, Rng& rng) {
  for (int i = b.candidates - 1; i > 0; --i) std::swap(b.candidate[i], b.candidate[rng.range(0, i)]);

  int wrong[kChoices - 1] = {};
  int count = 0;
  for (int i = 0; i < b.candidates && count < kChoices - 1; ++i) {
    if (usable(b, b.candidate[i], wrong, count)) wrong[count++] = b.candidate[i];
  }
  for (int step = 1; count < kChoices - 1; ++step) {
    const int up = b.q.answer + step;
    const int down = b.q.answer - step;
    if (usable(b, up, wrong, count)) wrong[count++] = up;
    if (count < kChoices - 1 && usable(b, down, wrong, count)) wrong[count++] = down;
  }

  b.q.correct = rng.range(0, kChoices - 1);
  int w = 0;
  for (int i = 0; i < kChoices; ++i) b.q.choice[i] = i == b.q.correct ? b.q.answer : wrong[w++];
  return b.q;
}

void setText(Build& b, const char* fmt, const int x, const int y) {
  std::snprintf(b.q.text, sizeof(b.q.text), fmt, x, y);
}

void addition(Build& b, const int x, const int y) {
  setText(b, "%d + %d = ?", x, y);
  b.q.answer = x + y;
  b.miss(b.q.answer + 1);
  b.miss(b.q.answer - 1);
  b.miss(b.q.answer + 10);
  b.miss(b.q.answer - 10);
  b.miss(x > y ? x - y : y - x);  // subtracted instead
  b.miss(b.q.answer + 2);
}

void subtraction(Build& b, const int x, const int y) {
  setText(b, "%d - %d = ?", x, y);
  b.q.answer = x - y;
  b.miss(b.q.answer + 1);
  b.miss(b.q.answer - 1);
  b.miss(b.q.answer + 10);
  b.miss(b.q.answer - 10);
  b.miss(x + y);  // added instead
  b.miss(b.q.answer + 2);
}

void multiplication(Build& b, const int x, const int y) {
  setText(b, "%d * %d = ?", x, y);
  b.q.answer = x * y;
  b.miss(x * (y + 1));  // one row too many
  b.miss(x * (y - 1));
  b.miss((x + 1) * y);
  b.miss(x + y);  // added instead
  b.miss(b.q.answer + 10);
  b.miss(b.q.answer - 10);
}

void division(Build& b, const int divisor, const int quotient) {
  setText(b, "%d / %d = ?", divisor * quotient, divisor);
  b.q.answer = quotient;
  b.miss(quotient + 1);
  b.miss(quotient - 1);
  b.miss(quotient + 2);
  b.miss(quotient + 10);
  b.miss(quotient - 10);
  if (divisor != quotient) b.miss(divisor);
}

// One decimal place, values in tenths.
void decimals(Build& b, Rng& rng) {
  b.q.tenths = true;
  char x[12];
  char y[12];
  if (rng.range(0, 2) == 0) {
    // A decimal times a whole number: 1.5 x 4.
    const int a = rng.range(11, 99);
    const int n = rng.range(2, 9);
    formatValue(a, true, x, sizeof(x));
    std::snprintf(b.q.text, sizeof(b.q.text), "%s * %d = ?", x, n);
    b.q.answer = a * n;
    b.miss(a * n / 10);  // the point dropped one place
    b.miss(a * n + 10);
    b.miss(a * n - 10);
    b.miss(a * (n + 1));
    b.miss(a * n + 1);
    return;
  }
  const int a = rng.range(15, 199);
  const int c = rng.range(11, 99);
  const bool add = rng.range(0, 1) == 0;
  const int big = add ? a : std::max(a, c);
  const int small = add ? c : std::min(a, c);
  formatValue(big, true, x, sizeof(x));
  formatValue(small, true, y, sizeof(y));
  std::snprintf(b.q.text, sizeof(b.q.text), add ? "%s + %s = ?" : "%s - %s = ?", x, y);
  b.q.answer = add ? big + small : big - small;
  b.miss(b.q.answer + 10);  // a carried one in the wrong column
  b.miss(b.q.answer - 10);
  b.miss(b.q.answer + 1);
  b.miss(b.q.answer - 1);
  b.miss(add ? big - small : big + small);
}

void orderOfOperations(Build& b, Rng& rng) {
  const int x = rng.range(2, 12);
  const int y = rng.range(2, 9);
  const int z = rng.range(2, 9);
  if (rng.range(0, 1) == 0) {
    std::snprintf(b.q.text, sizeof(b.q.text), "%d + %d * %d = ?", x, y, z);
    b.q.answer = x + y * z;
    b.miss((x + y) * z);  // worked left to right
  } else {
    const int big = y * z + x;
    std::snprintf(b.q.text, sizeof(b.q.text), "%d - %d * %d = ?", big, y, z);
    b.q.answer = big - y * z;
    b.miss((big - y) * z);
  }
  b.miss(b.q.answer + 1);
  b.miss(b.q.answer - 1);
  b.miss(b.q.answer + y);
  b.miss(b.q.answer + 10);
}

void negatives(Build& b, Rng& rng) {
  b.allowNegative = true;
  const int x = rng.range(2, 15);
  const int y = rng.range(2, 15);
  switch (rng.range(0, 2)) {
    case 0:
      setText(b, "-%d + %d = ?", x, y);
      b.q.answer = -x + y;
      b.miss(x + y);
      b.miss(-(x + y));
      break;
    case 1:
      setText(b, "%d - %d = ?", x, x + y);
      b.q.answer = -y;
      b.miss(y);
      b.miss(x + x + y);
      break;
    default:
      setText(b, "-%d - %d = ?", x, y);
      b.q.answer = -x - y;
      b.miss(x + y);
      b.miss(y - x);
      break;
  }
  b.miss(-b.q.answer);  // the sign lost
  b.miss(b.q.answer + 1);
  b.miss(b.q.answer - 1);
}

void percentage(Build& b, Rng& rng) {
  static constexpr int kPercents[] = {10, 20, 25, 50, 75};
  const int p = kPercents[rng.range(0, 4)];
  // A multiple of twenty makes every percent above a whole number.
  const int n = 20 * rng.range(1, 10);
  setText(b, "%d%% OF %d = ?", p, n);
  b.q.answer = p * n / 100;
  b.miss(n - b.q.answer);
  b.miss(b.q.answer * 2);
  b.miss(b.q.answer + 5);
  b.miss(b.q.answer - 5);
  b.miss(b.q.answer + 10);
  b.miss(p);
}

}  // namespace

Question makeQuestion(const int grade, Rng& rng) {
  Build b;
  const int roll = rng.range(0, 99);
  switch (grade < 1 ? 1 : grade > kGrades ? kGrades : grade) {
    case 1:
      // Adding and taking away within 20.
      if (roll < 50) {
        const int x = rng.range(1, 10);
        addition(b, x, rng.range(0, 10));
      } else {
        const int x = rng.range(2, 20);
        subtraction(b, x, rng.range(0, x));
      }
      break;
    case 2:
      // Within 100, with and without carrying.
      if (roll < 50) {
        const int x = rng.range(10, 89);
        addition(b, x, rng.range(2, 99 - x));
      } else {
        const int x = rng.range(20, 99);
        subtraction(b, x, rng.range(2, x - 1));
      }
      break;
    case 3:
      // Times tables and their division facts; three-digit sums.
      if (roll < 40) {
        multiplication(b, rng.range(2, 10), rng.range(2, 10));
      } else if (roll < 60) {
        division(b, rng.range(2, 10), rng.range(1, 10));
      } else if (roll < 80) {
        const int x = rng.range(100, 899);
        addition(b, x, rng.range(10, 999 - x));
      } else {
        const int x = rng.range(150, 999);
        subtraction(b, x, rng.range(10, x - 50));
      }
      break;
    case 4:
      // Long multiplication by one digit, short division, four-digit sums.
      if (roll < 35) {
        multiplication(b, rng.range(12, 99), rng.range(2, 9));
      } else if (roll < 65) {
        division(b, rng.range(2, 9), rng.range(11, 99));
      } else if (roll < 85) {
        const int x = rng.range(1000, 8999);
        addition(b, x, rng.range(100, 9999 - x));
      } else {
        const int x = rng.range(1500, 9999);
        subtraction(b, x, rng.range(100, x - 500));
      }
      break;
    case 5:
      // Two-digit by two-digit, dividing by two digits, tenths.
      if (roll < 30) {
        multiplication(b, rng.range(11, 49), rng.range(11, 29));
      } else if (roll < 55) {
        division(b, rng.range(11, 25), rng.range(2, 30));
      } else {
        decimals(b, rng);
      }
      break;
    default:
      // Order of operations, negative numbers, percentages.
      if (roll < 34) {
        orderOfOperations(b, rng);
      } else if (roll < 67) {
        negatives(b, rng);
      } else {
        percentage(b, rng);
      }
      break;
  }
  return finish(b, rng);
}

void formatValue(const int value, const bool tenths, char* out, const size_t capacity) {
  if (!tenths) {
    std::snprintf(out, capacity, "%d", value);
    return;
  }
  const int whole = std::abs(value) / 10;
  const int frac = std::abs(value) % 10;
  std::snprintf(out, capacity, "%s%d.%d", value < 0 ? "-" : "", whole, frac);
}

// --- History ---------------------------------------------------------------

std::vector<DayRecord> parseHistory(const std::string& text) {
  std::vector<DayRecord> out;
  out.reserve(64);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    int day = 0;
    int asked = 0;
    int right = 0;
    unsigned long ms = 0;
    if (std::sscanf(line.c_str(), "%d|%d|%d|%lu", &day, &asked, &right, &ms) != 4) continue;
    if (day < 0 || asked <= 0 || right < 0 || right > asked) continue;
    DayRecord r;
    r.day = day;
    r.asked = asked;
    r.right = right;
    r.totalMs = static_cast<uint32_t>(ms);
    out.push_back(r);
  }
  std::stable_sort(out.begin(), out.end(), [](const DayRecord& a, const DayRecord& b) { return a.day < b.day; });

  std::vector<DayRecord> merged;
  merged.reserve(out.size());
  for (const DayRecord& r : out) {
    if (!merged.empty() && merged.back().day == r.day) {
      merged.back().asked += r.asked;
      merged.back().right += r.right;
      merged.back().totalMs += r.totalMs;
    } else {
      merged.push_back(r);
    }
  }
  if (merged.size() > kMaxHistoryDays) merged.erase(merged.begin(), merged.end() - kMaxHistoryDays);
  return merged;
}

std::string formatHistory(const std::vector<DayRecord>& days) {
  std::string out;
  out.reserve(days.size() * 24);
  char line[48];
  for (const DayRecord& r : days) {
    std::snprintf(line, sizeof(line), "%d|%d|%d|%lu\n", r.day, r.asked, r.right, static_cast<unsigned long>(r.totalMs));
    out += line;
  }
  return out;
}

void addAnswer(std::vector<DayRecord>& days, const int day, const bool right, const uint32_t ms) {
  const uint32_t counted = ms > kMaxAnswerMs ? kMaxAnswerMs : ms;
  auto it = std::lower_bound(days.begin(), days.end(), day, [](const DayRecord& r, const int d) { return r.day < d; });
  if (it == days.end() || it->day != day) {
    DayRecord r;
    r.day = day;
    it = days.insert(it, r);
  }
  it->asked += 1;
  if (right) it->right += 1;
  it->totalMs += counted;
}

// --- The charts ------------------------------------------------------------

int percentOf(const int right, const int asked) {
  if (asked <= 0) return 0;
  return (right * 200 + asked) / (asked * 2);
}

int tenthsOf(const uint32_t totalMs, const int asked) {
  if (asked <= 0) return 0;
  const uint64_t per = static_cast<uint64_t>(asked) * 100;
  return static_cast<int>((static_cast<uint64_t>(totalMs) + per / 2) / per);
}

Window windowFor(const std::vector<DayRecord>& days, const int today, const int page) {
  Window w;
  w.lastDay = today - kWindowDays * (page < 0 ? 0 : page);
  w.firstDay = w.lastDay - (kWindowDays - 1);
  w.highlight = page <= 0 ? today : -1;
  for (int i = 0; i < kWindowDays; ++i) w.days[i].day = w.firstDay + i;

  int asked = 0;
  int right = 0;
  uint64_t ms = 0;
  for (const DayRecord& r : days) {
    if (r.day < w.firstDay) {
      w.hasOlder = true;
      continue;
    }
    if (r.day > w.lastDay) continue;
    ChartDay& c = w.days[r.day - w.firstDay];
    c.has = true;
    c.percent = percentOf(r.right, r.asked);
    c.tenthsSec = tenthsOf(r.totalMs, r.asked);
    if (r.day == w.highlight) continue;
    asked += r.asked;
    right += r.right;
    ms += r.totalMs;
  }
  if (asked > 0) {
    w.hasAverage = true;
    w.avgPercent = percentOf(right, asked);
    w.avgTenthsSec = static_cast<int>((ms + static_cast<uint64_t>(asked) * 50) / (static_cast<uint64_t>(asked) * 100));
  }
  return w;
}

// --- Calendar --------------------------------------------------------------

int daysFromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int yoe = year - era * 400;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int days, int& year, int& month, int& day) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const int doe = days - era * 146097;
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

int weekdayOf(const int days) {
  // 1970-01-01 was a Thursday.
  const int w = (days + 3) % 7;
  return w < 0 ? w + 7 : w;
}

}  // namespace mathquiz
