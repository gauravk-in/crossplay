#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MathQuizCore.h"

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// Reads a number token in the question's own units: tenths when `tenths`.
long readNumber(const std::string& token, const bool tenths) {
  const bool negative = token[0] == '-';
  const std::string digits = negative ? token.substr(1) : token;
  long whole = 0;
  long frac = 0;
  bool point = false;
  for (const char c : digits) {
    if (c == '.') {
      point = true;
    } else if (c >= '0' && c <= '9') {
      if (point) {
        frac = frac * 10 + (c - '0');
      } else {
        whole = whole * 10 + (c - '0');
      }
    }
  }
  const long v = tenths ? whole * 10 + frac : whole;
  return negative ? -v : v;
}

// Evaluates the question text independently of the generator, with * and /
// binding tighter than + and -. Decimal products come out in tenths because
// the question has only one decimal factor.
bool evaluate(const mathquiz::Question& q, long& out) {
  std::vector<std::string> tokens;
  const char* p = q.text;
  while (*p) {
    while (*p == ' ') ++p;
    const char* start = p;
    while (*p && *p != ' ') ++p;
    if (p > start) tokens.emplace_back(start, p);
  }
  if (tokens.size() < 4 || tokens[tokens.size() - 2] != "=" || tokens.back() != "?") return false;
  tokens.resize(tokens.size() - 2);
  if (tokens.size() == 3 && tokens[1] == "OF") {
    const long percent = std::atol(tokens[0].c_str());
    out = percent * std::atol(tokens[2].c_str()) / 100;
    return tokens[0].back() == '%';
  }
  // Terms joined by + and -, each a product or quotient.
  long total = 0;
  long term = 0;
  int sign = 1;
  char pending = 0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string& t = tokens[i];
    if (i % 2 == 1) {
      if (t == "+" || t == "-") {
        total += sign * term;
        sign = t == "+" ? 1 : -1;
        pending = 0;
      } else if (t == "*" || t == "/") {
        pending = t[0];
      } else {
        return false;
      }
      continue;
    }
    const bool decimal = t.find('.') != std::string::npos;
    const long v = readNumber(t, q.tenths && (decimal || pending == 0));
    if (pending == '*') {
      term *= v;
    } else if (pending == '/') {
      if (v == 0 || term % v != 0) return false;
      term /= v;
    } else {
      term = v;
    }
  }
  out = total + sign * term;
  return true;
}

void testQuestions() {
  for (int grade = 1; grade <= mathquiz::kGrades; ++grade) {
    mathquiz::Rng rng(static_cast<uint32_t>(grade * 7919));
    int slot[mathquiz::kChoices] = {};
    for (int n = 0; n < 5000; ++n) {
      const mathquiz::Question q = mathquiz::makeQuestion(grade, rng);
      char what[160];
      std::snprintf(what, sizeof(what), "grade %d: %s", grade, q.text);
      long value = 0;
      if (!evaluate(q, value)) {
        check(false, (std::string("question reads as arithmetic, ") + what).c_str());
        continue;
      }
      check(value == q.answer, (std::string("answer matches the question, ") + what).c_str());
      check(q.correct >= 0 && q.correct < mathquiz::kChoices, "correct slot in range");
      ++slot[q.correct];
      check(q.choice[q.correct] == q.answer, "correct slot holds the answer");
      for (int i = 0; i < mathquiz::kChoices; ++i) {
        for (int j = i + 1; j < mathquiz::kChoices; ++j) check(q.choice[i] != q.choice[j], "choices are distinct");
        if (grade < 6) check(q.choice[i] >= 0, (std::string("no negative choices below class 6, ") + what).c_str());
      }
      check(std::strlen(q.text) < mathquiz::kTextBytes - 1, "question text fits its buffer");
      if (grade < 6) check(q.answer >= 0, "no negative answers below class 6");
    }
    for (int i = 0; i < mathquiz::kChoices; ++i) check(slot[i] > 1000, "the answer lands in every slot");
  }
  // Class 1 stays within twenty.
  mathquiz::Rng rng(5);
  for (int n = 0; n < 2000; ++n) {
    const mathquiz::Question q = mathquiz::makeQuestion(1, rng);
    check(q.answer <= 20 && q.answer >= 0, "class 1 answers within 0..20");
  }
}

void testFormat() {
  char b[16];
  mathquiz::formatValue(42, false, b, sizeof(b));
  check(std::strcmp(b, "42") == 0, "42");
  mathquiz::formatValue(-7, false, b, sizeof(b));
  check(std::strcmp(b, "-7") == 0, "-7");
  mathquiz::formatValue(35, true, b, sizeof(b));
  check(std::strcmp(b, "3.5") == 0, "3.5");
  mathquiz::formatValue(4, true, b, sizeof(b));
  check(std::strcmp(b, "0.4") == 0, "0.4");
  mathquiz::formatValue(-12, true, b, sizeof(b));
  check(std::strcmp(b, "-1.2") == 0, "-1.2");
}

void testHistory() {
  std::vector<mathquiz::DayRecord> days =
      mathquiz::parseHistory("20005|10|8|52000\njunk\n20003|4|4|20000\n20005|2|1|8000\n20004|3|5|1000\n");
  check(days.size() == 2, "junk and impossible lines dropped, repeats merged");
  check(days[0].day == 20003 && days[1].day == 20005, "sorted by day");
  check(days[1].asked == 12 && days[1].right == 9 && days[1].totalMs == 60000, "a repeated day merges");
  check(mathquiz::parseHistory(mathquiz::formatHistory(days)).size() == 2, "round trips");
  check(mathquiz::formatHistory(days) == "20003|4|4|20000\n20005|12|9|60000\n", "writes one line per day");

  mathquiz::addAnswer(days, 20004, true, 3000);
  mathquiz::addAnswer(days, 20004, false, 999999);
  check(days.size() == 3 && days[1].day == 20004, "a new day is inserted in order");
  check(days[1].asked == 2 && days[1].right == 1, "counts the answers");
  check(days[1].totalMs == 3000 + mathquiz::kMaxAnswerMs, "a question left open counts as the cap");
}

void testWindow() {
  const int today = 20100;
  std::vector<mathquiz::DayRecord> days;
  for (int i = 0; i < 4; ++i) mathquiz::addAnswer(days, today, i < 3, 4000);  // 75%, 4.0s
  mathquiz::addAnswer(days, today - 1, true, 6000);
  mathquiz::addAnswer(days, today - 1, false, 6000);  // 50%, 6.0s
  mathquiz::addAnswer(days, today - 13, true, 9000);  // 100%, 9.0s
  mathquiz::addAnswer(days, today - 14, true, 1000);  // previous window

  const mathquiz::Window w = mathquiz::windowFor(days, today, 0);
  check(w.lastDay == today && w.firstDay == today - 13, "page 0 ends today");
  check(w.highlight == today, "page 0 highlights today");
  check(w.days[13].has && w.days[13].percent == 75 && w.days[13].tenthsSec == 40, "today's bar");
  check(w.days[12].has && w.days[12].percent == 50 && w.days[12].tenthsSec == 60, "yesterday's bar");
  check(!w.days[5].has, "an empty day has no bar");
  check(w.days[0].has && w.days[0].percent == 100, "the window's first day");
  check(w.hasAverage && w.avgPercent == 67 && w.avgTenthsSec == 70, "the average leaves today out, per question");
  check(w.hasOlder, "something before the window");

  const mathquiz::Window older = mathquiz::windowFor(days, today, 1);
  check(older.lastDay == today - 14 && older.highlight == -1, "page 1 is the fortnight before");
  check(older.days[13].has && !older.hasOlder, "page 1 holds the older day and nothing is older");
  check(older.hasAverage && older.avgPercent == 100 && older.avgTenthsSec == 10, "an older page averages all of it");

  const mathquiz::Window empty = mathquiz::windowFor({}, today, 0);
  check(!empty.hasAverage && !empty.hasOlder, "an empty history");
  check(mathquiz::percentOf(2, 3) == 67 && mathquiz::percentOf(1, 3) == 33, "percent rounds");
  check(mathquiz::tenthsOf(12345, 2) == 62, "average seconds round to tenths");
}

void testCalendar() {
  check(mathquiz::daysFromCivil(1970, 1, 1) == 0, "epoch");
  check(mathquiz::weekdayOf(0) == 3, "1970-01-01 was a Thursday");
  const int d = mathquiz::daysFromCivil(2026, 10, 10);
  int y = 0, m = 0, dd = 0;
  mathquiz::civilFromDays(d, y, m, dd);
  check(y == 2026 && m == 10 && dd == 10, "civil round trip");
  check(mathquiz::weekdayOf(d) == 5, "2026-10-10 is a Saturday");
}

}  // namespace

int main() {
  testQuestions();
  testFormat();
  testHistory();
  testWindow();
  testCalendar();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("mathquiz: all checks passed\n");
  return 0;
}
