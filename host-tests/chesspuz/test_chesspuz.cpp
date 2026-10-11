#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../../src/apps_local/chesspuz/ChessPuzzleCore.h"
#include "../../src/apps_local/chesspuz/ChessPuzzleData.h"

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

chess::SearchBuffers buffers;

chess::Move uci(const chess::Position& position, const char* text) {
  chess::Move m;
  if (!chesspuz::parseUci(position, text, m)) std::printf("FAIL: %s is not legal here\n", text), ++failures;
  return m;
}

// Fixtures written for these tests, not Lichess's: a back-rank mate, and the
// two-rook ladder in two and in three.
constexpr chesspuz::PuzzleRecord kBackRank = {"T1", "6k1/5ppp/8/8/8/8/5PPP/R5K1 b - - 0 1", "g8h8 a1a8", 800};
constexpr chesspuz::PuzzleRecord kLadder2 = {"T2", "5k2/8/8/8/8/8/1R6/R5K1 b - - 0 1", "f8e8 a1a7 e8d8 b2b8", 900};
constexpr chesspuz::PuzzleRecord kLadder3 = {"T3", "8/5k2/8/8/8/8/1R6/R5K1 b - - 0 1", "f7e7 a1a6 e7d7 b2b7 d7e8 a6a8",
                                             1000};

void testBundle() {
  std::set<std::string> ids;
  for (int level = 0; level < chesspuz::kLevels; ++level) {
    const chesspuz::Level& l = chesspuz::kPuzzleLevels[level];
    check(l.count > 0, "every level has puzzles");
    int lastRating = 0;
    for (int i = 0; i < l.count; ++i) {
      chesspuz::Puzzle p;
      const bool ok = chesspuz::loadPuzzle(l.puzzles[i], p);
      if (!ok) std::printf("FAIL: puzzle %s does not load\n", l.puzzles[i].id), ++failures;
      if (ok && p.mateIn != level + 1)
        std::printf("FAIL: puzzle %s is not mate in %d\n", l.puzzles[i].id, level + 1), ++failures;
      check(l.puzzles[i].rating >= lastRating, "puzzles run easiest first");
      lastRating = l.puzzles[i].rating;
      check(ids.insert(l.puzzles[i].id).second, "puzzle ids are unique");
    }
  }
}

void testLoad() {
  chesspuz::Puzzle p;
  check(chesspuz::loadPuzzle(kLadder2, p), "ladder loads");
  check(p.mateIn == 2 && p.solutionCount == 3, "ladder is mate in 2");
  check(p.playerWhite, "white solves after black's setup move");
  check(p.start.squares[60] == (chess::King | chess::BlackFlag), "setup move is played: king on e8");

  chesspuz::PuzzleRecord bad = kLadder2;
  bad.moves = "f8e8 a1a7 e8d8 b2b7";  // not mate
  check(!chesspuz::loadPuzzle(bad, p), "a line that does not mate is rejected");
  bad.moves = "f8e8 a1a9";
  check(!chesspuz::loadPuzzle(bad, p), "a malformed move is rejected");
  bad.moves = "f8e8 a1a7 e8d8";
  check(!chesspuz::loadPuzzle(bad, p), "a line ending on the opponent's move is rejected");
}

void testSolveOnLine() {
  chesspuz::Puzzle p;
  chesspuz::loadPuzzle(kLadder3, p);
  chesspuz::Session s;
  s.begin(p, true);
  check(s.play(uci(s.position(), "a1a6")), "first move plays");
  check(s.state() == chesspuz::State::Reply, "the opponent answers next");
  s.reply(buffers);
  check(s.lastMove() == p.solution[1], "the answer is the line's");
  s.play(uci(s.position(), "b2b7"));
  s.reply(buffers);
  s.play(uci(s.position(), "a6a8"));
  check(s.state() == chesspuz::State::Solved, "the line mates");
  check(s.hintsUsed() == 0 && s.undosUsed() == 0, "nothing used");
  check(!s.canUndo(), "no undo after solving");
}

void testWrongWithHints() {
  chesspuz::Puzzle p;
  chesspuz::loadPuzzle(kLadder2, p);
  chesspuz::Session s;
  s.begin(p, true);
  s.play(uci(s.position(), "g1g2"));
  check(s.state() == chesspuz::State::Wrong, "an off-line move is marked wrong");
  check(s.hintsUsed() == 1, "the mark counts as a hint");
  check(s.canUndo() && !s.canHint(), "undo, not hint, while wrong");
  check(s.undo(), "undo works");
  check(s.state() == chesspuz::State::Playing && s.movesMade() == 0, "back to the start");
  check(s.undosUsed() == 1, "the undo is counted");

  check(s.canHint(), "hint available on the line");
  s.hint();
  check(s.hintFrom() == p.solution[0].from && s.hintTo() == -1, "first hint shows the piece");
  s.hint();
  check(s.hintTo() == p.solution[0].to, "second hint shows the square");
  check(!s.canHint(), "two hints per move");
  check(s.hintsUsed() == 3, "hints add up");
  s.play(p.solution[0]);
  check(s.hintFrom() == -1, "hints clear after the move");
}

void testAnyMateCounts() {
  chesspuz::Puzzle p;
  chesspuz::loadPuzzle(kLadder2, p);
  chesspuz::Session s;
  s.begin(p, true);
  // b2b7 first, then Ra8, is the same ladder the other way round.
  s.play(uci(s.position(), "b2b7"));
  check(s.state() == chesspuz::State::Wrong, "with hints on, off the line is wrong even if it would work");

  s.begin(p, false);
  s.play(uci(s.position(), "b2b7"));
  check(s.state() == chesspuz::State::Reply, "with hints off, nothing is marked");
  s.reply(buffers);
  check(s.state() == chesspuz::State::Playing, "the engine answers");
  check(!(s.lastMove() == p.solution[1]), "the engine plays the reply, not the line's");
  s.play(uci(s.position(), "a1a8"));
  check(s.state() == chesspuz::State::Solved, "a different mate still solves it");
}

void testNoMate() {
  chesspuz::Puzzle p;
  chesspuz::loadPuzzle(kBackRank, p);
  chesspuz::Session s;
  s.begin(p, false);
  s.play(uci(s.position(), "a1a7"));
  check(s.state() == chesspuz::State::NoMate, "the last move without mate is NO MATE");
  check(s.hintsUsed() == 0, "nothing marked with hints off");
  check(s.undo() && s.state() == chesspuz::State::Playing, "undo from NO MATE");
  s.play(uci(s.position(), "a1a8"));
  check(s.state() == chesspuz::State::Solved, "then the mate");
  check(s.undosUsed() == 1, "one undo");
}

void testUndoPair() {
  chesspuz::Puzzle p;
  chesspuz::loadPuzzle(kLadder3, p);
  chesspuz::Session s;
  s.begin(p, true);
  s.play(p.solution[0]);
  s.reply(buffers);
  check(s.movesMade() == 1, "one move made");
  s.undo();
  check(s.movesMade() == 0, "undo takes the reply and the move");
  char fen[96];
  char start[96];
  chess::positionToFen(s.position(), fen);
  chess::positionToFen(p.start, start);
  check(std::string(fen) == start, "the board is back where it began");
  check(s.lastMove() == p.setup, "the setup move shows again");
}

void testHistory() {
  std::vector<chesspuz::DayRecord> days;
  chesspuz::addResult(days, 100, true, true);
  chesspuz::addResult(days, 100, true, false);
  chesspuz::addResult(days, 100, false, false);
  chesspuz::addResult(days, 98, true, true);
  check(days.size() == 2 && days[0].day == 98, "days sorted");
  check(chesspuz::scoreOf(days[1]) == 33, "one of three clean is 33%");
  const auto back = chesspuz::parseHistory(chesspuz::formatHistory(days) + "junk\n5|1|2|0\n100|1|1|1\n");
  check(back.size() == 2 && back[1].played == 4 && back[1].clean == 2, "round trip, merge, skip bad lines");

  const chesspuz::Window w = chesspuz::windowFor(days, 100, 0);
  check(w.firstDay == 87 && w.lastDay == 100 && w.highlight == 100, "window ends today");
  check(w.days[13].has && w.days[13].score == 33 && w.days[13].played == 3, "today in the window");
  check(w.hasAverage && w.avgScore == 100, "average leaves today out");
  check(!w.hasOlder && w.mostPlayed == 3, "older and busiest day");
  const chesspuz::Window older = chesspuz::windowFor(days, 115, 1);
  check(older.lastDay == 101 && older.highlight == -1 && older.hasAverage, "page back two weeks");
}

void testProgress() {
  chesspuz::Progress p;
  p.hintsOn = false;
  p.index[1] = 12;
  p.hints[1] = 2;
  p.undos[2] = 3;
  const chesspuz::Progress q = chesspuz::parseProgress(chesspuz::formatProgress(p));
  check(!q.hintsOn && q.index[1] == 12 && q.hints[1] == 2 && q.undos[2] == 3, "progress round trip");
  check(chesspuz::parseProgress("").hintsOn, "hints default on");
  check(chesspuz::parseProgress("l4=1,0,0\nl1=-3,0,0\n").index[0] == 0, "bad lines ignored");
}

void testCalendar() {
  check(chesspuz::daysFromCivil(1970, 1, 1) == 0, "epoch");
  int y = 0, m = 0, d = 0;
  chesspuz::civilFromDays(chesspuz::daysFromCivil(2026, 10, 11), y, m, d);
  check(y == 2026 && m == 10 && d == 11, "civil round trip");
  check(chesspuz::weekdayOf(chesspuz::daysFromCivil(2026, 10, 11)) == 6, "2026-10-11 is a Sunday");
}

}  // namespace

int main() {
  testBundle();
  testLoad();
  testSolveOnLine();
  testWrongWithHints();
  testAnyMateCounts();
  testNoMate();
  testUndoPair();
  testHistory();
  testProgress();
  testCalendar();
  if (failures > 0) {
    std::printf("%d chesspuz check(s) failed\n", failures);
    return 1;
  }
  int total = 0;
  for (const auto& l : chesspuz::kPuzzleLevels) total += l.count;
  std::printf("chesspuz: all checks passed (%d bundled puzzles)\n", total);
  return 0;
}
