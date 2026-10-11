#include "ChessPuzzleCore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chesspuz {
namespace {

// ~1KB each, so they are not stack locals (see ChessCore.h). Everything here
// runs on the one UI task.
chess::MoveList scratchMoves;

int squareOf(const char file, const char rank) {
  if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return -1;
  return (rank - '1') * 8 + (file - 'a');
}

uint8_t promotionOf(const char c) {
  switch (c) {
    case 'q':
      return chess::Queen;
    case 'r':
      return chess::Rook;
    case 'b':
      return chess::Bishop;
    case 'n':
      return chess::Knight;
    default:
      return chess::Empty;
  }
}

// Plies the engine looks ahead for the defender once the player is off the
// line: enough to see the mate still coming, capped by the engine's own limit.
constexpr uint32_t kReplyBudget = 40000;

}  // namespace

bool parseUci(const chess::Position& position, const char* uci, chess::Move& out) {
  if (uci == nullptr) return false;
  const size_t len = std::strlen(uci);
  if (len < 4 || len > 5) return false;
  const int from = squareOf(uci[0], uci[1]);
  const int to = squareOf(uci[2], uci[3]);
  if (from < 0 || to < 0) return false;
  const uint8_t promotion = len == 5 ? promotionOf(uci[4]) : static_cast<uint8_t>(chess::Empty);
  if (len == 5 && promotion == chess::Empty) return false;
  chess::generateLegalMoves(position, scratchMoves);
  for (int i = 0; i < scratchMoves.count; ++i) {
    const chess::Move& m = scratchMoves.moves[i];
    if (m.from == from && m.to == to && m.promotion == promotion) {
      out = m;
      return true;
    }
  }
  return false;
}

bool isCheckmate(const chess::Position& position) {
  if (!chess::isInCheck(position)) return false;
  chess::generateLegalMoves(position, scratchMoves);
  return scratchMoves.count == 0;
}

bool loadPuzzle(const PuzzleRecord& record, Puzzle& out) {
  if (record.fen == nullptr || record.moves == nullptr) return false;
  chess::Position position;
  if (!chess::parseFen(record.fen, position)) return false;

  Puzzle puzzle;
  int count = 0;
  const char* p = record.moves;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    char token[8] = {};
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ' ' && n < sizeof(token) - 1) {
      token[n] = p[n];
      ++n;
    }
    if (p[n] != '\0' && p[n] != ' ') return false;
    p += n;

    chess::Move move;
    if (!parseUci(position, token, move)) return false;
    if (count == 0) {
      puzzle.setup = move;
    } else {
      if (count - 1 >= kMaxSolution) return false;
      puzzle.solution[count - 1] = move;
    }
    chess::Undo undo;
    chess::makeMove(position, move, undo);
    if (count == 0) puzzle.start = position;
    ++count;
  }
  puzzle.solutionCount = count - 1;
  // The player moves first and last, so the line is always an odd length.
  if (puzzle.solutionCount < 1 || (puzzle.solutionCount & 1) == 0) return false;
  if (!isCheckmate(position)) return false;
  puzzle.mateIn = (puzzle.solutionCount + 1) / 2;
  puzzle.playerWhite = puzzle.start.whiteToMove;
  out = puzzle;
  return true;
}

// --- Session ---------------------------------------------------------------

void Session::begin(const Puzzle& puzzle, const bool hintsOn) {
  puzzle_ = puzzle;
  position_ = puzzle.start;
  plies_ = 0;
  state_ = State::Playing;
  hintsOn_ = hintsOn;
  hintStage_ = 0;
  hints_ = 0;
  undos_ = 0;
}

int Session::movesMade() const { return (plies_ + 1) / 2; }

chess::Move Session::lastMove() const { return plies_ > 0 ? moves_[plies_ - 1] : puzzle_.setup; }

void Session::push(const chess::Move& move, const bool onLine) {
  if (plies_ >= kMaxPlies) return;
  moves_[plies_] = move;
  onLine_[plies_] = onLine;
  chess::makeMove(position_, move, undo_[plies_]);
  ++plies_;
}

bool Session::play(const chess::Move& move) {
  if (state_ != State::Playing || plies_ >= kMaxPlies) return false;
  chess::Move legal;
  char uci[6];
  chess::moveToString(move, uci);
  if (!parseUci(position_, uci, legal)) return false;
  const bool wasOnLine = plies_ == 0 || onLine_[plies_ - 1];
  const bool onLine = wasOnLine && plies_ < puzzle_.solutionCount && legal == puzzle_.solution[plies_];
  push(legal, onLine);
  hintStage_ = 0;
  judge();
  return true;
}

void Session::judge() {
  if (isCheckmate(position_)) {
    state_ = State::Solved;
    return;
  }
  if (hintsOn_ && !onLine_[plies_ - 1]) {
    state_ = State::Wrong;
    ++hints_;
    return;
  }
  chess::generateLegalMoves(position_, scratchMoves);
  if (movesMade() >= puzzle_.mateIn || scratchMoves.count == 0) {
    state_ = State::NoMate;
    return;
  }
  state_ = State::Reply;
}

void Session::reply(chess::SearchBuffers& buffers) {
  if (state_ != State::Reply) return;
  const bool onLine = onLine_[plies_ - 1];
  if (onLine && plies_ < puzzle_.solutionCount) {
    push(puzzle_.solution[plies_], true);
  } else {
    const int left = puzzle_.mateIn - movesMade();
    const int depth = std::min(chess::kMaxSearchDepth, 2 * left);
    chess::SearchResult result = chess::search(position_, depth, buffers, kReplyBudget);
    if (!result.hasMove) {
      state_ = State::NoMate;
      return;
    }
    push(result.best, false);
  }
  chess::generateLegalMoves(position_, scratchMoves);
  // Stalemated by the reply, or (impossible on a mate line) mated.
  state_ = scratchMoves.count == 0 ? State::NoMate : State::Playing;
}

bool Session::canUndo() const { return plies_ > 0 && state_ != State::Solved; }

bool Session::undo() {
  if (!canUndo()) return false;
  // Back to the player's move: past the reply if there was one, then the move.
  if ((plies_ & 1) == 0) {
    --plies_;
    chess::unmakeMove(position_, moves_[plies_], undo_[plies_]);
  }
  --plies_;
  chess::unmakeMove(position_, moves_[plies_], undo_[plies_]);
  state_ = State::Playing;
  hintStage_ = 0;
  ++undos_;
  return true;
}

bool Session::canHint() const {
  const bool onLine = plies_ == 0 || onLine_[plies_ - 1];
  return hintsOn_ && state_ == State::Playing && onLine && hintStage_ < 2;
}

void Session::hint() {
  if (!canHint()) return;
  ++hintStage_;
  ++hints_;
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
    DayRecord r;
    if (std::sscanf(line.c_str(), "%d|%d|%d|%d", &r.day, &r.played, &r.solved, &r.clean) != 4) continue;
    if (r.day < 0 || r.played <= 0 || r.solved < 0 || r.solved > r.played || r.clean < 0 || r.clean > r.solved) {
      continue;
    }
    out.push_back(r);
  }
  std::stable_sort(out.begin(), out.end(), [](const DayRecord& a, const DayRecord& b) { return a.day < b.day; });

  std::vector<DayRecord> merged;
  merged.reserve(out.size());
  for (const DayRecord& r : out) {
    if (!merged.empty() && merged.back().day == r.day) {
      merged.back().played += r.played;
      merged.back().solved += r.solved;
      merged.back().clean += r.clean;
    } else {
      merged.push_back(r);
    }
  }
  if (merged.size() > kMaxHistoryDays) merged.erase(merged.begin(), merged.end() - kMaxHistoryDays);
  return merged;
}

std::string formatHistory(const std::vector<DayRecord>& days) {
  std::string out;
  out.reserve(days.size() * 20);
  char line[64];
  for (const DayRecord& r : days) {
    std::snprintf(line, sizeof(line), "%d|%d|%d|%d\n", r.day, r.played, r.solved, r.clean);
    out += line;
  }
  return out;
}

void addResult(std::vector<DayRecord>& days, const int day, const bool solved, const bool clean) {
  auto it = std::lower_bound(days.begin(), days.end(), day, [](const DayRecord& r, const int d) { return r.day < d; });
  if (it == days.end() || it->day != day) {
    DayRecord r;
    r.day = day;
    it = days.insert(it, r);
  }
  it->played += 1;
  if (solved) it->solved += 1;
  if (solved && clean) it->clean += 1;
}

namespace {
int percentOf(const int part, const int whole) { return whole <= 0 ? 0 : (part * 200 + whole) / (whole * 2); }
}  // namespace

int scoreOf(const DayRecord& day) { return percentOf(day.clean, day.played); }

Window windowFor(const std::vector<DayRecord>& days, const int today, const int page) {
  Window w;
  w.lastDay = today - kWindowDays * (page < 0 ? 0 : page);
  w.firstDay = w.lastDay - (kWindowDays - 1);
  w.highlight = page <= 0 ? today : -1;
  for (int i = 0; i < kWindowDays; ++i) w.days[i].day = w.firstDay + i;

  int played = 0;
  int clean = 0;
  for (const DayRecord& r : days) {
    if (r.day < w.firstDay) {
      w.hasOlder = true;
      continue;
    }
    if (r.day > w.lastDay) continue;
    ChartDay& c = w.days[r.day - w.firstDay];
    c.has = true;
    c.score = scoreOf(r);
    c.played = r.played;
    c.solved = r.solved;
    if (r.played > w.mostPlayed) w.mostPlayed = r.played;
    if (r.day == w.highlight) continue;
    played += r.played;
    clean += r.clean;
  }
  if (played > 0) {
    w.hasAverage = true;
    w.avgScore = percentOf(clean, played);
  }
  return w;
}

// --- Calendar (Howard Hinnant's algorithms) ----------------------------------

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

// --- Progress --------------------------------------------------------------

Progress parseProgress(const std::string& text) {
  Progress p;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    int on = 0;
    int level = 0;
    int index = 0;
    int hints = 0;
    int undos = 0;
    if (std::sscanf(line.c_str(), "hints=%d", &on) == 1) {
      p.hintsOn = on != 0;
    } else if (std::sscanf(line.c_str(), "l%d=%d,%d,%d", &level, &index, &hints, &undos) == 4 && level >= 1 &&
               level <= kLevels && index >= 0 && hints >= 0 && undos >= 0) {
      p.index[level - 1] = index;
      p.hints[level - 1] = hints;
      p.undos[level - 1] = undos;
    }
  }
  return p;
}

std::string formatProgress(const Progress& progress) {
  std::string out;
  char line[64];
  std::snprintf(line, sizeof(line), "hints=%d\n", progress.hintsOn ? 1 : 0);
  out += line;
  for (int i = 0; i < kLevels; ++i) {
    std::snprintf(line, sizeof(line), "l%d=%d,%d,%d\n", i + 1, progress.index[i], progress.hints[i], progress.undos[i]);
    out += line;
  }
  return out;
}

}  // namespace chesspuz
