#pragma once

// CHECKMATE: find the mate in one, two or three moves.
//
// Freestanding C++17 like the Chess app's rules (which this reuses): no
// Arduino, no renderer, no SD card, so host-tests/chesspuz can play every
// bundled puzzle through these functions on a laptop.
//
// A puzzle is a Lichess record: the FEN before the opponent's last move, then
// that move, then the solution in UCI, the player's moves and the replies
// alternating. The opponent's move is played on the device so the board can
// show what just happened, the way Lichess does.
//
// Judging a move:
//   - A move that mates wins, whether or not it is the recorded one.
//   - With hints on, any other move off the recorded line is marked wrong and
//     the opponent does not answer; the player undoes it. The mark counts as a
//     hint, because it told them something.
//   - With hints off, nothing is marked. The opponent answers from the line
//     while the player is on it, and with the engine once they leave it, so a
//     different mate still counts and a miss shows up as "no mate" at the end.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../chess/ChessCore.h"
#include "../chess/ChessEngine.h"

namespace chesspuz {

constexpr int kLevels = 3;  // mate in 1, 2, 3
// Plies after the setup move: the player's N moves and the N - 1 replies.
constexpr int kMaxSolution = 2 * kLevels - 1;
constexpr int kMaxPlies = kMaxSolution;

struct PuzzleRecord {
  const char* id;     // Lichess puzzle id, for the credit line and the URL
  const char* fen;    // before the setup move
  const char* moves;  // UCI, space separated: setup move, then the solution
  uint16_t rating;
};

struct Puzzle {
  chess::Position start;  // after the setup move, the player to move
  chess::Move setup{};
  chess::Move solution[kMaxSolution] = {};
  int solutionCount = 0;
  int mateIn = 0;
  bool playerWhite = true;
};

// Finds the legal move a UCI string names ("e2e4", "e7e8q"). False when the
// text is malformed or the move is not legal here.
bool parseUci(const chess::Position& position, const char* uci, chess::Move& out);

// Builds a puzzle from its record. False when anything in it does not parse or
// is illegal, so a bad row is skipped rather than played.
bool loadPuzzle(const PuzzleRecord& record, Puzzle& out);

// True when the side to move is checkmated.
bool isCheckmate(const chess::Position& position);

enum class State : uint8_t {
  Playing,  // the player's move
  Reply,    // the player moved; the opponent answers next
  Wrong,    // hints on: the last move was off the line, undo it
  Solved,   // mate
  NoMate,   // the moves are used up (or stalemate) and there is no mate
};

// One attempt at one puzzle: the board, the moves made, and what was used.
class Session {
 public:
  void begin(const Puzzle& puzzle, bool hintsOn);

  // The player's move. Must be legal in position(); returns false otherwise.
  bool play(const chess::Move& move);
  // Plays the opponent's answer while state() is Reply. Uses the line while
  // the player is on it and the engine after; `buffers` is the engine's
  // per-ply scratch, owned by the caller because it is ~5KB.
  void reply(chess::SearchBuffers& buffers);
  // Takes back the player's last move and the reply to it.
  bool canUndo() const;
  bool undo();

  // HINT: the first press shows the piece to move, the second where it goes.
  // Only while on the line with hints on.
  bool canHint() const;
  void hint();
  // The squares a hint has revealed for the current move, or -1.
  int hintFrom() const { return hintStage_ >= 1 ? solutionMove().from : -1; }
  int hintTo() const { return hintStage_ >= 2 ? solutionMove().to : -1; }

  // What this puzzle had already used before the app was left mid-attempt.
  void restoreCounts(int hints, int undos) {
    hints_ = hints;
    undos_ = undos;
  }
  bool hintsOn() const { return hintsOn_; }

  const chess::Position& position() const { return position_; }
  const Puzzle& puzzle() const { return puzzle_; }
  State state() const { return state_; }
  bool finished() const { return state_ == State::Solved; }
  int movesMade() const;  // the player's, 0..mateIn
  int hintsUsed() const { return hints_; }
  int undosUsed() const { return undos_; }
  // The last move on the board, for the from/to marks: the setup move until
  // anyone has moved.
  chess::Move lastMove() const;
  bool lastMoveWasPlayers() const { return plies_ > 0 && (plies_ & 1) == 1; }

 private:
  const chess::Move& solutionMove() const {
    return puzzle_.solution[2 * movesMade() < kMaxSolution ? 2 * movesMade() : 0];
  }
  void push(const chess::Move& move, bool onLine);
  void judge();

  Puzzle puzzle_{};
  chess::Position position_{};
  chess::Move moves_[kMaxPlies] = {};
  chess::Undo undo_[kMaxPlies] = {};
  // Whether the line was still being followed after each ply.
  bool onLine_[kMaxPlies] = {};
  int plies_ = 0;
  State state_ = State::Playing;
  bool hintsOn_ = true;
  int hintStage_ = 0;
  int hints_ = 0;
  int undos_ = 0;
};

// --- History -------------------------------------------------------------

constexpr int kWindowDays = 14;
constexpr size_t kMaxHistoryBytes = 32 * 1024;
constexpr size_t kMaxHistoryDays = 1200;

// One line per day, `day|played|solved|clean`, in
// /.crosspoint/chesspuz/history.txt. A puzzle is played once it is solved or
// skipped; clean means solved with no hints.
struct DayRecord {
  int day = 0;  // days since 1970-01-01, local
  int played = 0;
  int solved = 0;
  int clean = 0;
};

std::vector<DayRecord> parseHistory(const std::string& text);
std::string formatHistory(const std::vector<DayRecord>& days);
void addResult(std::vector<DayRecord>& days, int day, bool solved, bool clean);

// The day's score: the share of the puzzles played that were solved without
// a hint, rounded.
int scoreOf(const DayRecord& day);

struct ChartDay {
  int day = 0;
  bool has = false;
  int score = 0;  // percent
  int played = 0;
  int solved = 0;
};

// Fourteen days ending on `lastDay`; page 0 ends today, page n 14 * n days
// earlier. The average is over every puzzle in the window except today's,
// weighted by puzzle, the same rule Math Quiz uses.
struct Window {
  int firstDay = 0;
  int lastDay = 0;
  int highlight = -1;
  ChartDay days[kWindowDays];
  bool hasAverage = false;
  int avgScore = 0;
  int mostPlayed = 0;
  bool hasOlder = false;
};

Window windowFor(const std::vector<DayRecord>& days, int today, int page);

int daysFromCivil(int year, int month, int day);
void civilFromDays(int days, int& year, int& month, int& day);
int weekdayOf(int days);  // 0 Monday .. 6 Sunday

// --- Progress ------------------------------------------------------------

// Which puzzle each level is on, and what the current one has used so far, so
// leaving mid-puzzle cannot wipe a hint off the record. Plus the HINTS switch.
// Text: `hints=1` then `l<level>=<index>,<hints>,<undos>` lines.
struct Progress {
  bool hintsOn = true;
  int index[kLevels] = {};
  int hints[kLevels] = {};
  int undos[kLevels] = {};
};

Progress parseProgress(const std::string& text);
std::string formatProgress(const Progress& progress);

}  // namespace chesspuz
