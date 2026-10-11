#pragma once

// Memory: turn cards two at a time and find the pairs. A pair keeps you at the
// table; a miss passes the turn (to the other player, or back to you alone).
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card -- so that
// host-tests/memory can drive every rule on a laptop.
//
// The recall score. Turning cards blind is how every game starts and costs
// nothing. What is scored is the moment you turn a card whose partner has
// already been seen face up, by anyone: that is a RECALL CHANCE, and your next
// card is a HIT if it is that partner and a MISS if it is anything else. The
// game's recall is hits / chances, as a percentage, so it measures memory and
// not luck: a lucky blind pair is not a hit, and a blind miss is not a miss.
//
// The Game is also the two-device wire state (linkplay::Play<Game>), so it is
// trivially copyable and under the 192-byte packet. Both devices run the same
// rules over it; the recall counts travel with it, one pair per seat.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace memory {

constexpr int kMaxCards = 36;
constexpr int kMaxPairs = kMaxCards / 2;
constexpr int kSeats = 2;
constexpr uint8_t kNone = 0xFF;

// The three boards. Columns by rows, portrait.
enum class Size : uint8_t { Small = 0, Medium = 1, Large = 2 };
constexpr int kSizes = 3;
int columnsOf(Size size);
int rowsOf(Size size);
inline int cardsOf(Size size) { return columnsOf(size) * rowsOf(size); }
const char* sizeLabel(Size size);  // "4 X 4"

// A game needs this many recall chances before its score goes on the chart.
// One lucky hit is 100%, and a daily best made of those says nothing.
constexpr int kMinChances = 3;

struct Game {
  uint8_t size = 0;     // a Size
  uint8_t players = 1;  // 1 alone, 2 at one device or two
  uint8_t turn = 0;     // the seat to play
  uint8_t over = 0;
  uint8_t face[kMaxCards] = {};   // which picture, 0..pairs-1
  uint8_t owner[kMaxCards] = {};  // 0 in play, else the seat + 1 that took it
  uint8_t seen[(kMaxCards + 7) / 8] = {};
  // Taken in the turn on screen, so the other player can see what changed.
  uint8_t fresh[(kMaxCards + 7) / 8] = {};
  // The cards face up and not yet taken: one after a first card, two after a
  // miss (left up until the next card is turned, so they can be studied).
  uint8_t up[2] = {kNone, kNone};
  uint8_t upCount = 0;
  // Whether the card in up[0] opened a recall chance.
  uint8_t chance = 0;
  uint8_t pairs[kSeats] = {};
  uint8_t hits[kSeats] = {};
  uint8_t misses[kSeats] = {};
};

// Shuffles a fresh board. `seed` is the only entropy; the core never reads a
// clock, so a game is reproducible from it.
void deal(Game& game, Size size, int players, uint32_t seed);

enum class Flip : uint8_t {
  Refused,  // not a card in play, already up, or the game is over
  First,    // the first of two
  Pair,     // the second, and it matched: same player again
  Miss,     // the second, and it did not: the turn moves on
};

// Turns card `index` for whoever's turn it is. A pending miss is turned back
// down first.
Flip flip(Game& game, int index);

// What the last flip did to the recall count, for the screen's one-word news.
enum class Recall : uint8_t { None, Hit, Miss };
Recall lastRecall(const Game& before, const Game& after, int seat);

inline bool isSeen(const Game& g, int i) { return (g.seen[i / 8] >> (i % 8)) & 1; }
inline bool isFresh(const Game& g, int i) { return (g.fresh[i / 8] >> (i % 8)) & 1; }
inline bool isUp(const Game& g, int i) { return g.upCount > 0 && (g.up[0] == i || (g.upCount > 1 && g.up[1] == i)); }
int pairCount(const Game& g);
int pairsLeft(const Game& g);
int chancesOf(const Game& g, int seat);
// -1 while there have been no chances.
int recallPercent(const Game& g, int seat);
// The seat with more pairs, or -1 for a tie (or a solo game).
int winner(const Game& g);

// --- History -------------------------------------------------------------

// One line per finished game: `day|size|hits|misses`.
struct GameRecord {
  int day = 0;  // days since 1970-01-01, local
  int size = 0;
  int hits = 0;
  int misses = 0;
};

constexpr size_t kMaxHistoryBytes = 48 * 1024;
constexpr size_t kMaxHistoryGames = 2000;
constexpr int kWindowDays = 14;

// Lenient: malformed lines are skipped, and only the newest kMaxHistoryGames
// are kept.
std::vector<GameRecord> parseHistory(const std::string& text);
std::string formatHistory(const std::vector<GameRecord>& games);

struct ChartDay {
  int day = 0;
  bool has = false;
  int best = 0;  // the day's best recall, 0..100
  int games = 0;
};

// The fortnight ending `today` for one board size. `average` is the mean of
// the daily bests over the days before today, so today is compared against
// the practice that came before it.
struct Window {
  int firstDay = 0;
  ChartDay days[kWindowDays];
  bool hasAverage = false;
  int average = 0;
};

Window windowFor(const std::vector<GameRecord>& games, int today, Size size);
bool counts(const GameRecord& game);
int percentOf(int hits, int misses);

int daysFromCivil(int year, int month, int day);
void civilFromDays(int days, int& year, int& month, int& day);
int weekdayOf(int days);  // 0 Monday .. 6 Sunday

}  // namespace memory
