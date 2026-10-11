// Memory's rules and its recall score, driven without a panel.

#include <cstdio>
#include <cstring>
#include <type_traits>

#include "MemoryCore.h"

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

using namespace memory;

// The other card with the same picture.
int partner(const Game& g, int i) {
  for (int j = 0; j < cardsOf(static_cast<Size>(g.size)); ++j)
    if (j != i && g.face[j] == g.face[i]) return j;
  return -1;
}

// A card that is not `i`'s partner and is still in play.
int stranger(const Game& g, int i) {
  for (int j = 0; j < cardsOf(static_cast<Size>(g.size)); ++j)
    if (j != i && g.face[j] != g.face[i] && g.owner[j] == 0) return j;
  return -1;
}

void testDeal() {
  for (int s = 0; s < kSizes; ++s) {
    Game g;
    deal(g, static_cast<Size>(s), 1, 1234);
    const int n = cardsOf(static_cast<Size>(s));
    int count[kMaxPairs] = {};
    for (int i = 0; i < n; ++i) {
      CHECK(g.face[i] < kMaxPairs);
      ++count[g.face[i]];
    }
    int pictures = 0;
    for (int f = 0; f < kMaxPairs; ++f) {
      CHECK(count[f] == 0 || count[f] == 2);
      pictures += count[f] == 2;
    }
    CHECK(pictures == n / 2);
    CHECK(pairsLeft(g) == n / 2);
  }
  Game a, b;
  deal(a, Size::Large, 1, 7);
  deal(b, Size::Large, 1, 7);
  CHECK(std::memcmp(&a, &b, sizeof(Game)) == 0);
  deal(b, Size::Large, 1, 8);
  CHECK(std::memcmp(a.face, b.face, sizeof(a.face)) != 0);
}

void testBlindTurnsScoreNothing() {
  Game g;
  deal(g, Size::Small, 1, 99);
  // A blind miss: neither card had been seen.
  const int a = 0;
  const int b = stranger(g, a);
  CHECK(flip(g, a) == Flip::First);
  CHECK(flip(g, b) == Flip::Miss);
  CHECK(g.hits[0] == 0 && g.misses[0] == 0);
  CHECK(recallPercent(g, 0) == -1);
  // A lucky blind pair: also not a hit.
  Game h;
  deal(h, Size::Small, 1, 99);
  CHECK(flip(h, 0) == Flip::First);
  CHECK(flip(h, partner(h, 0)) == Flip::Pair);
  CHECK(h.hits[0] == 0 && h.misses[0] == 0);
  CHECK(h.pairs[0] == 1);
}

void testHitAndMiss() {
  Game g;
  deal(g, Size::Small, 1, 5);
  const int a = 0;
  const int b = stranger(g, a);
  flip(g, a);
  flip(g, b);  // a and b now seen
  // Turn a's partner: its pair has been seen, so this is a chance...
  const int pa = partner(g, a);
  CHECK(flip(g, pa) == Flip::First);
  CHECK(g.upCount == 1);  // the pending miss went back down
  CHECK(g.chance == 1);
  // ...and turning a is a hit.
  Game before = g;
  CHECK(flip(g, a) == Flip::Pair);
  CHECK(lastRecall(before, g, 0) == Recall::Hit);
  CHECK(g.hits[0] == 1 && g.misses[0] == 0);
  // b's partner, then the wrong card: a miss.
  const int pb = partner(g, b);
  CHECK(flip(g, pb) == Flip::First);
  int wrong = -1;
  for (int i = 0; i < 16; ++i)
    if (i != b && i != pb && g.owner[i] == 0) wrong = i;
  before = g;
  CHECK(flip(g, wrong) == Flip::Miss);
  CHECK(lastRecall(before, g, 0) == Recall::Miss);
  CHECK(g.hits[0] == 1 && g.misses[0] == 1);
  CHECK(recallPercent(g, 0) == 50);
}

void testSecondCardSeenIsNotAChance() {
  // The rule is about the FIRST card's partner. Turning a blind first card and
  // then a seen card that does not match it scores nothing.
  Game g;
  deal(g, Size::Small, 1, 11);
  const int a = 0;
  const int b = stranger(g, a);
  flip(g, a);
  flip(g, b);
  int fresh = -1;
  for (int i = 0; i < 16; ++i)
    if (i != a && i != b && partner(g, i) != a && partner(g, i) != b) {
      fresh = i;
      break;
    }
  CHECK(flip(g, fresh) == Flip::First);
  CHECK(g.chance == 0);
  CHECK(flip(g, a) == Flip::Miss);
  CHECK(g.hits[0] == 0 && g.misses[0] == 0);
}

void testRefusals() {
  Game g;
  deal(g, Size::Small, 1, 3);
  CHECK(flip(g, -1) == Flip::Refused);
  CHECK(flip(g, 16) == Flip::Refused);
  CHECK(flip(g, 0) == Flip::First);
  CHECK(flip(g, 0) == Flip::Refused);  // the card already up
  CHECK(flip(g, partner(g, 0)) == Flip::Pair);
  CHECK(flip(g, 0) == Flip::Refused);  // taken
}

void testTwoPlayers() {
  Game g;
  deal(g, Size::Small, 2, 21);
  CHECK(g.turn == 0);
  flip(g, 0);
  CHECK(flip(g, partner(g, 0)) == Flip::Pair);
  CHECK(g.turn == 0);  // a pair keeps the turn
  CHECK(isFresh(g, 0));
  int a = -1;
  for (int i = 0; i < 16; ++i)
    if (g.owner[i] == 0) {
      a = i;
      break;
    }
  flip(g, a);
  CHECK(flip(g, stranger(g, a)) == Flip::Miss);
  CHECK(g.turn == 1);    // a miss passes it
  CHECK(isFresh(g, 0));  // still news for the next player
  // Player two remembers a's partner.
  CHECK(flip(g, partner(g, a)) == Flip::First);
  CHECK(!isFresh(g, 0));
  CHECK(flip(g, a) == Flip::Pair);
  CHECK(g.hits[1] == 1 && g.hits[0] == 0);
  CHECK(g.owner[a] == 2);
}

void testSoloKeepsTurn() {
  Game g;
  deal(g, Size::Small, 1, 2);
  flip(g, 0);
  flip(g, stranger(g, 0));
  CHECK(g.turn == 0);
}

void testPlayToTheEnd() {
  for (int s = 0; s < kSizes; ++s) {
    Game g;
    deal(g, static_cast<Size>(s), 2, 77 + s);
    const int n = cardsOf(static_cast<Size>(s));
    int guard = 0;
    while (!g.over && guard++ < 1000) {
      int i = -1;
      for (int k = 0; k < n; ++k)
        if (g.owner[k] == 0 && !(g.upCount == 1 && g.up[0] == k)) {
          i = k;
          break;
        }
      CHECK(flip(g, i) != Flip::Refused);
      if (g.upCount == 1) CHECK(flip(g, partner(g, i)) == Flip::Pair);
    }
    CHECK(g.over);
    CHECK(pairsLeft(g) == 0);
    CHECK(g.pairs[0] + g.pairs[1] == n / 2);
    CHECK(flip(g, 0) == Flip::Refused);
  }
}

void testWireState() {
  static_assert(std::is_trivially_copyable<Game>::value, "Game travels as bytes");
  static_assert(sizeof(Game) <= 192, "Game must fit one link packet");
}

void testHistory() {
  const std::string text = "20000|0|3|1\nbad line\n20001|2|5|5\n20002|9|1|1\n20003|1|-1|0\n20004|1|2|2junk\n";
  const auto games = parseHistory(text);
  CHECK(games.size() == 2);
  CHECK(formatHistory(games) == "20000|0|3|1\n20001|2|5|5\n");
  CHECK(percentOf(3, 1) == 75);
  CHECK(percentOf(2, 1) == 67);
  CHECK(percentOf(0, 0) == 0);
  GameRecord tiny{1, 0, 2, 0};
  CHECK(!counts(tiny));
  GameRecord enough{1, 0, 2, 1};
  CHECK(counts(enough));
}

void testWindow() {
  const int today = daysFromCivil(2026, 10, 11);
  std::vector<GameRecord> games = {
      {today, 0, 3, 1},       // 75
      {today, 0, 4, 0},       // 100, the day's best
      {today, 0, 1, 0},       // too few chances
      {today, 1, 6, 0},       // another size
      {today - 1, 0, 1, 3},   // 25
      {today - 3, 0, 3, 3},   // 50
      {today - 14, 0, 9, 0},  // before the window
  };
  const Window w = windowFor(games, today, Size::Small);
  CHECK(w.firstDay == today - 13);
  CHECK(w.days[13].has && w.days[13].best == 100 && w.days[13].games == 2);
  CHECK(w.days[12].has && w.days[12].best == 25);
  CHECK(w.days[10].has && w.days[10].best == 50);
  CHECK(!w.days[0].has);
  CHECK(w.hasAverage && w.average == 38);  // (25 + 50) / 2, rounded
  const Window m = windowFor(games, today, Size::Medium);
  CHECK(m.days[13].best == 100 && !m.hasAverage);
}

void testCalendar() {
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  int y, m, d;
  civilFromDays(daysFromCivil(2026, 10, 11), y, m, d);
  CHECK(y == 2026 && m == 10 && d == 11);
  CHECK(weekdayOf(daysFromCivil(2026, 10, 11)) == 6);  // a Sunday
}

}  // namespace

int main() {
  testDeal();
  testBlindTurnsScoreNothing();
  testHitAndMiss();
  testSecondCardSeenIsNotAChance();
  testRefusals();
  testTwoPlayers();
  testSoloKeepsTurn();
  testPlayToTheEnd();
  testWireState();
  testHistory();
  testWindow();
  testCalendar();
  if (failures) {
    std::printf("%d memory test(s) failed\n", failures);
    return 1;
  }
  std::printf("memory: all tests passed\n");
  return 0;
}
