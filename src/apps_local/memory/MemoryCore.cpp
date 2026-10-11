#include "MemoryCore.h"

#include <cstdio>
#include <cstdlib>

namespace memory {

namespace {

constexpr int kColumns[kSizes] = {4, 4, 6};
constexpr int kRows[kSizes] = {4, 6, 6};
constexpr const char* kLabels[kSizes] = {"4 X 4", "4 X 6", "6 X 6"};

int sizeIndex(const Size size) {
  const int i = static_cast<int>(size);
  return i >= 0 && i < kSizes ? i : 0;
}

void setBit(uint8_t* bits, const int i) { bits[i / 8] = static_cast<uint8_t>(bits[i / 8] | (1u << (i % 8))); }

// xorshift32: small and deterministic.
uint32_t next(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

// The other card with the same picture.
int partnerOf(const Game& g, const int index) {
  const int n = cardsOf(static_cast<Size>(g.size));
  for (int i = 0; i < n; ++i) {
    if (i != index && g.face[i] == g.face[index]) return i;
  }
  return -1;
}

}  // namespace

int columnsOf(const Size size) { return kColumns[sizeIndex(size)]; }
int rowsOf(const Size size) { return kRows[sizeIndex(size)]; }
const char* sizeLabel(const Size size) { return kLabels[sizeIndex(size)]; }

void deal(Game& game, const Size size, const int players, const uint32_t seed) {
  game = Game{};
  game.size = static_cast<uint8_t>(sizeIndex(size));
  game.players = static_cast<uint8_t>(players == 2 ? 2 : 1);
  const int n = cardsOf(size);
  for (int i = 0; i < n; ++i) game.face[i] = static_cast<uint8_t>(i / 2);
  // The pictures on a small board are drawn from all eighteen, so a 4 x 4 is
  // not always the same eight.
  uint32_t s = seed ? seed : 1u;
  uint8_t pick[kMaxPairs];
  for (int i = 0; i < kMaxPairs; ++i) pick[i] = static_cast<uint8_t>(i);
  for (int i = kMaxPairs - 1; i > 0; --i) {
    const int j = static_cast<int>(next(s) % static_cast<uint32_t>(i + 1));
    const uint8_t t = pick[i];
    pick[i] = pick[j];
    pick[j] = t;
  }
  for (int i = 0; i < n; ++i) game.face[i] = pick[game.face[i]];
  for (int i = n - 1; i > 0; --i) {
    const int j = static_cast<int>(next(s) % static_cast<uint32_t>(i + 1));
    const uint8_t t = game.face[i];
    game.face[i] = game.face[j];
    game.face[j] = t;
  }
}

Flip flip(Game& game, const int index) {
  const int n = cardsOf(static_cast<Size>(game.size));
  if (game.over || index < 0 || index >= n || game.owner[index] != 0) return Flip::Refused;
  if (game.upCount == 1 && game.up[0] == index) return Flip::Refused;

  if (game.upCount == 2) {
    // The miss left up for study goes back down; the player's tap is the
    // next card, so nobody has to tap twice to carry on. A new turn starts
    // here, so what the last one took is no longer news.
    game.upCount = 0;
    game.up[0] = game.up[1] = kNone;
    for (uint8_t& bits : game.fresh) bits = 0;
  }

  const int seat = game.turn;
  if (game.upCount == 0) {
    const int partner = partnerOf(game, index);
    game.chance = static_cast<uint8_t>(partner >= 0 && isSeen(game, partner) ? 1 : 0);
    game.up[0] = static_cast<uint8_t>(index);
    game.upCount = 1;
    setBit(game.seen, index);
    return Flip::First;
  }

  const int first = game.up[0];
  const bool match = game.face[first] == game.face[index];
  if (game.chance) {
    if (match) {
      ++game.hits[seat];
    } else {
      ++game.misses[seat];
    }
  }
  game.chance = 0;
  setBit(game.seen, index);

  if (match) {
    game.owner[first] = game.owner[index] = static_cast<uint8_t>(seat + 1);
    setBit(game.fresh, first);
    setBit(game.fresh, index);
    ++game.pairs[seat];
    game.upCount = 0;
    game.up[0] = game.up[1] = kNone;
    if (pairsLeft(game) == 0) game.over = 1;
    return Flip::Pair;
  }

  game.up[1] = static_cast<uint8_t>(index);
  game.upCount = 2;
  if (game.players == 2) game.turn = static_cast<uint8_t>(1 - seat);
  // What this turn took stays marked through the hand-over, so the next
  // player (on this device or the other) sees it; their first card clears it.
  return Flip::Miss;
}

Recall lastRecall(const Game& before, const Game& after, const int seat) {
  if (seat < 0 || seat >= kSeats) return Recall::None;
  if (after.hits[seat] > before.hits[seat]) return Recall::Hit;
  if (after.misses[seat] > before.misses[seat]) return Recall::Miss;
  return Recall::None;
}

int pairCount(const Game& g) { return cardsOf(static_cast<Size>(g.size)) / 2; }

int pairsLeft(const Game& g) { return pairCount(g) - g.pairs[0] - g.pairs[1]; }

int chancesOf(const Game& g, const int seat) {
  if (seat < 0 || seat >= kSeats) return 0;
  return g.hits[seat] + g.misses[seat];
}

int recallPercent(const Game& g, const int seat) {
  if (chancesOf(g, seat) == 0) return -1;
  return percentOf(g.hits[seat], g.misses[seat]);
}

int winner(const Game& g) {
  if (g.players != 2 || g.pairs[0] == g.pairs[1]) return -1;
  return g.pairs[0] > g.pairs[1] ? 0 : 1;
}

// --- History -------------------------------------------------------------

std::vector<GameRecord> parseHistory(const std::string& text) {
  std::vector<GameRecord> out;
  size_t lines = 0;
  for (const char c : text) lines += c == '\n' ? 1 : 0;
  out.reserve(lines + 1 < kMaxHistoryGames ? lines + 1 : kMaxHistoryGames);
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    GameRecord r;
    char tail = 0;
    if (std::sscanf(line.c_str(), "%d|%d|%d|%d%c", &r.day, &r.size, &r.hits, &r.misses, &tail) != 4) continue;
    if (r.day <= 0 || r.size < 0 || r.size >= kSizes || r.hits < 0 || r.misses < 0) continue;
    if (r.hits > kMaxCards || r.misses > 999) continue;
    out.push_back(r);
  }
  if (out.size() > kMaxHistoryGames) out.erase(out.begin(), out.end() - kMaxHistoryGames);
  return out;
}

std::string formatHistory(const std::vector<GameRecord>& games) {
  std::string out;
  out.reserve(games.size() * 16);
  char line[48];
  for (const GameRecord& r : games) {
    std::snprintf(line, sizeof(line), "%d|%d|%d|%d\n", r.day, r.size, r.hits, r.misses);
    out += line;
  }
  return out;
}

bool counts(const GameRecord& game) { return game.hits + game.misses >= kMinChances; }

int percentOf(const int hits, const int misses) {
  const int chances = hits + misses;
  if (chances <= 0) return 0;
  return (hits * 200 + chances) / (chances * 2);
}

Window windowFor(const std::vector<GameRecord>& games, const int today, const Size size) {
  Window w;
  w.firstDay = today - (kWindowDays - 1);
  for (int i = 0; i < kWindowDays; ++i) w.days[i].day = w.firstDay + i;
  for (const GameRecord& r : games) {
    if (r.size != static_cast<int>(size) || !counts(r)) continue;
    const int i = r.day - w.firstDay;
    if (i < 0 || i >= kWindowDays) continue;
    ChartDay& d = w.days[i];
    const int p = percentOf(r.hits, r.misses);
    if (!d.has || p > d.best) d.best = p;
    d.has = true;
    ++d.games;
  }
  int sum = 0;
  int count = 0;
  for (int i = 0; i < kWindowDays - 1; ++i) {
    if (!w.days[i].has) continue;
    sum += w.days[i].best;
    ++count;
  }
  if (count > 0) {
    w.hasAverage = true;
    w.average = (sum * 2 + count) / (count * 2);
  }
  return w;
}

// --- Calendar (Howard Hinnant's algorithms) -------------------------------

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

}  // namespace memory
