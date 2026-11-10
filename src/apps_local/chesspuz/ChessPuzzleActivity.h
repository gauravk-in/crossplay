#pragma once

// CHECKMATE: chess puzzles where you find the mate in one, two or three.
//
// Pick a length, then tap a piece and tap where it goes. The opponent answers
// on its own. With HINTS on, a move off the solution is crossed out until you
// undo it, and HINT shows the piece and then the square; every mark and hint
// counts against the puzzle. UNDO always works. The day's score is the share of
// puzzles solved without a hint, charted over two weeks. See
// docs/apps/chesspuz.md.
//
// Puzzles are a bundled selection from the Lichess puzzle database (CC0); the
// menu credits Lichess. The rules are the Chess app's ChessCore, and the judging
// is ChessPuzzleCore, both freestanding and host-tested.

#include <cstdint>
#include <memory>
#include <vector>

#include "../../activities/Activity.h"
#include "../chess/ChessEngine.h"
#include "../ui/ToyboxScreen.h"
#include "ChessPuzzleCore.h"
#include "ChessPuzzleScreens.h"

class ChessPuzzleActivity final : public Activity {
 public:
  ChessPuzzleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ChessPuzzle", renderer, mappedInput) {}
  ~ChessPuzzleActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Puzzle, Results };

  int currentDay() const;
  void loadState();
  void saveProgress();
  void saveHistory();
  void startLevel(int level);
  void loadCurrent();
  void finishPuzzle(bool solved);
  void advance();
  void tapSquare(int square);
  bool squareAt(int x, int y, int& square) const;
  void afterPlayerMove();
  void rememberCounts();
  void go(View next);
  void routeAction(int action, int value, int x, int y);

  void drawBoard() const;
  void drawSquare(int square) const;
  int screenColumnOf(int square) const;
  int screenRowOf(int square) const;

  View view_ = View::Menu;
  chesspuz::Progress progress_;
  int level_ = 0;  // 0-based: mate in level_ + 1
  bool active_ = false;
  chesspuz::Session session_;
  int selected_ = -1;
  // ~1KB, a member for the same reason as Chess's: see ChessCore.h.
  chess::MoveList legal_;
  // ~5KB of per-ply engine scratch for the opponent's answers off the line.
  chess::SearchBuffers buffers_;
  // When the opponent answers. Zero while nothing is due.
  uint32_t replyAt_ = 0;
  bool recorded_ = false;

  std::vector<chesspuz::DayRecord> history_;
  chesspuz::Window window_;
  bool haveWindow_ = false;
  int page_ = 0;

  freeink::ui::Rect board_{};
  bool fullRefresh_ = true;
  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
