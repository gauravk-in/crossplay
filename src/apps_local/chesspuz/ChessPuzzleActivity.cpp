#include "ChessPuzzleActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "../Shelf.h"
#include "../chess/ChessPieces.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "ChessPuzzleData.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kDir = "/.crosspoint/chesspuz";
constexpr const char* kHistoryPath = "/.crosspoint/chesspuz/history.txt";
constexpr const char* kProgressPath = "/.crosspoint/chesspuz/progress.txt";
// Below this the clock was never set; the same floor Study and Math Quiz use.
constexpr time_t kClockFloor = 1700000000;
// Long enough to see your own move land before the answer does.
constexpr uint32_t kReplyDelayMs = 700;

constexpr int fileOf(const int square) { return square & 7; }
constexpr int rankOf(const int square) { return square >> 3; }

std::string readFile(const char* path, const size_t cap) {
  std::string out;
  if (!Storage.exists(path)) return out;
  if (!Storage.readFileToString("CHESSPUZ", path, cap, out)) out.clear();
  return out;
}

// Written beside the file and renamed over it, so a power cut mid-write leaves
// the old file rather than none.
bool writeFile(const char* path, const std::string& text) {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("CHESSPUZ", "could not make %s", kDir);
    return false;
  }
  const std::string part = std::string(path) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("CHESSPUZ", part.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(part.c_str(), path)) {
    Storage.remove(part.c_str());
    LOG_ERR("CHESSPUZ", "could not replace %s", path);
    return false;
  }
  return true;
}

const uint8_t* solidFor(const uint8_t type) {
  switch (type) {
    case chess::Pawn:
      return chessart::kPawnSolid;
    case chess::Knight:
      return chessart::kKnightSolid;
    case chess::Bishop:
      return chessart::kBishopSolid;
    case chess::Rook:
      return chessart::kRookSolid;
    case chess::Queen:
      return chessart::kQueenSolid;
    case chess::King:
      return chessart::kKingSolid;
    default:
      return nullptr;
  }
}

const uint8_t* outlineFor(const uint8_t type) {
  switch (type) {
    case chess::Pawn:
      return chessart::kPawnLight;
    case chess::Knight:
      return chessart::kKnightLight;
    case chess::Bishop:
      return chessart::kBishopLight;
    case chess::Rook:
      return chessart::kRookLight;
    case chess::Queen:
      return chessart::kQueenLight;
    case chess::King:
      return chessart::kKingLight;
    default:
      return nullptr;
  }
}

// White pieces are knocked out to the paper first and then outlined, so a dark
// square's dither does not show through them (see ChessActivity's drawPieceAt).
void drawPiece(const GfxRenderer& renderer, const uint8_t piece, const int x, const int y) {
  const uint8_t* solid = solidFor(chess::pieceType(piece));
  const uint8_t* outline = outlineFor(chess::pieceType(piece));
  if (solid == nullptr || outline == nullptr) return;
  if (chess::isBlack(piece)) {
    toybox::blit1bpp(renderer, solid, chessart::kPieceSize, x, y, true);
    return;
  }
  toybox::blit1bpp(renderer, solid, chessart::kPieceSize, x, y, false);
  toybox::blit1bpp(renderer, outline, chessart::kPieceSize, x, y, true);
}

}  // namespace

std::unique_ptr<Activity> ChessPuzzleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ChessPuzzleActivity>(renderer, mappedInput);
}

void ChessPuzzleActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadState();
  go(View::Menu);
}

void ChessPuzzleActivity::onExit() {
  rememberCounts();
  Activity::onExit();
}

// --- Data ------------------------------------------------------------------

int ChessPuzzleActivity::currentDay() const {
  const time_t now = time(nullptr);
  if (now < kClockFloor) return -1;
  tm local{};
  localtime_r(&now, &local);
  return chesspuz::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void ChessPuzzleActivity::loadState() {
  history_ = chesspuz::parseHistory(readFile(kHistoryPath, chesspuz::kMaxHistoryBytes));
  progress_ = chesspuz::parseProgress(readFile(kProgressPath, 512));
}

void ChessPuzzleActivity::saveProgress() {
  if (!writeFile(kProgressPath, chesspuz::formatProgress(progress_))) LOG_ERR("CHESSPUZ", "progress was not written");
}

void ChessPuzzleActivity::saveHistory() {
  if (!writeFile(kHistoryPath, chesspuz::formatHistory(history_))) LOG_ERR("CHESSPUZ", "history was not written");
}

// Keeps what the puzzle on screen has used, so leaving and coming back cannot
// clear a hint off the record. Written only when it changed.
void ChessPuzzleActivity::rememberCounts() {
  if (!active_ || session_.finished()) return;
  if (progress_.hints[level_] == session_.hintsUsed() && progress_.undos[level_] == session_.undosUsed()) return;
  progress_.hints[level_] = session_.hintsUsed();
  progress_.undos[level_] = session_.undosUsed();
  saveProgress();
}

// --- Puzzles ---------------------------------------------------------------

void ChessPuzzleActivity::startLevel(const int level) {
  if (level < 0 || level >= chesspuz::kLevels) return;
  level_ = level;
  loadCurrent();
}

// Loads the level's current puzzle. A record that does not load is skipped:
// host-tests/chesspuz replays them all, so this is a backstop.
void ChessPuzzleActivity::loadCurrent() {
  const chesspuz::Level& l = chesspuz::kPuzzleLevels[level_];
  if (l.count <= 0) return;
  chesspuz::Puzzle puzzle;
  for (int tries = 0; tries < l.count; ++tries) {
    const int index = progress_.index[level_] % l.count;
    if (chesspuz::loadPuzzle(l.puzzles[index], puzzle)) break;
    LOG_ERR("CHESSPUZ", "puzzle %s does not load", l.puzzles[index].id);
    progress_.index[level_] = index + 1;
  }
  session_.begin(puzzle, progress_.hintsOn);
  session_.restoreCounts(progress_.hints[level_], progress_.undos[level_]);
  active_ = true;
  recorded_ = false;
  selected_ = -1;
  replyAt_ = 0;
  chess::generateLegalMoves(session_.position(), legal_);
  go(View::Puzzle);
}

void ChessPuzzleActivity::finishPuzzle(const bool solved) {
  if (recorded_) return;
  recorded_ = true;
  const int day = currentDay();
  if (day >= 0) {
    chesspuz::addResult(history_, day, solved, session_.hintsUsed() == 0);
    saveHistory();
  }
  progress_.index[level_] = (progress_.index[level_] + 1) % chesspuz::kPuzzleLevels[level_].count;
  progress_.hints[level_] = 0;
  progress_.undos[level_] = 0;
  saveProgress();
}

void ChessPuzzleActivity::advance() {
  if (!session_.finished()) finishPuzzle(false);
  loadCurrent();
}

void ChessPuzzleActivity::afterPlayerMove() {
  selected_ = -1;
  switch (session_.state()) {
    case chesspuz::State::Solved:
      finishPuzzle(true);
      break;
    case chesspuz::State::Reply:
      replyAt_ = millis() + kReplyDelayMs;
      break;
    default:
      break;
  }
  rememberCounts();
  chess::generateLegalMoves(session_.position(), legal_);
  requestUpdate();
}

void ChessPuzzleActivity::tapSquare(const int square) {
  if (session_.state() != chesspuz::State::Playing || replyAt_ != 0) return;
  const chess::Position& pos = session_.position();
  const uint8_t piece = pos.squares[square];
  if (selected_ >= 0) {
    for (int i = 0; i < legal_.count; ++i) {
      chess::Move m = legal_.moves[i];
      if (m.from != selected_ || m.to != square) continue;
      if (m.promotion != chess::Empty) {
        // One tap cannot say which piece, so the solution's choice is taken
        // when it is this move, and a queen otherwise.
        const chess::Move& want = session_.puzzle().solution[2 * session_.movesMade()];
        m.promotion = (want.from == m.from && want.to == m.to) ? want.promotion : static_cast<uint8_t>(chess::Queen);
      }
      session_.play(m);
      afterPlayerMove();
      return;
    }
  }
  const bool own = piece != chess::Empty && chess::isColour(piece, pos.whiteToMove);
  selected_ = (own && square != selected_) ? square : -1;
  requestUpdate();
}

// --- Board geometry ----------------------------------------------------------

// The player's side is always at the bottom.
int ChessPuzzleActivity::screenColumnOf(const int square) const {
  return session_.puzzle().playerWhite ? fileOf(square) : 7 - fileOf(square);
}

int ChessPuzzleActivity::screenRowOf(const int square) const {
  return session_.puzzle().playerWhite ? 7 - rankOf(square) : rankOf(square);
}

bool ChessPuzzleActivity::squareAt(const int x, const int y, int& square) const {
  if (board_.width <= 0) return false;
  const int size = board_.width / 8;
  if (x < board_.x || x >= board_.x + 8 * size || y < board_.y || y >= board_.y + 8 * size) return false;
  const int column = (x - board_.x) / size;
  const int row = (y - board_.y) / size;
  const bool white = session_.puzzle().playerWhite;
  const int file = white ? column : 7 - column;
  const int rank = white ? 7 - row : row;
  square = rank * 8 + file;
  return true;
}

void ChessPuzzleActivity::drawSquare(const int square) const {
  const int size = board_.width / 8;
  const int x = board_.x + screenColumnOf(square) * size;
  const int y = board_.y + screenRowOf(square) * size;
  // Dithered, never solid: the board repaints on every move (Toybox.h).
  if (((fileOf(square) + rankOf(square)) & 1) == 0) renderer.fillRectDither(x, y, size, size, LightGray);

  const chess::Move last = session_.lastMove();
  if (square == last.from || square == last.to) {
    toybox::cornerMarks(renderer, Rect{x + 3, y + 3, size - 6, size - 6}, std::max(8, size / 5), 3);
  }

  const uint8_t piece = session_.position().squares[square];
  if (piece != chess::Empty) {
    const int inset = (size - chessart::kPieceSize) / 2;
    drawPiece(renderer, piece, x + inset, y + inset);
  }

  // Where the selected piece can go: a dot on an empty square, a ring of
  // corners on a capture so the piece under it still reads.
  if (selected_ >= 0) {
    for (int i = 0; i < legal_.count; ++i) {
      if (legal_.moves[i].from != selected_ || legal_.moves[i].to != square) continue;
      if (piece == chess::Empty) {
        const int d = std::max(8, size / 4);
        renderer.fillRoundedRect(x + (size - d) / 2, y + (size - d) / 2, d, d, d / 2, Black);
      } else {
        toybox::cornerMarks(renderer, Rect{x, y, size, size}, std::max(10, size / 4), toybox::kFrame);
      }
      break;
    }
  }
}

void ChessPuzzleActivity::drawBoard() const {
  const int size = board_.width / 8;
  for (int square = 0; square < 64; ++square) drawSquare(square);

  auto frameSquare = [&](const int square, const int weight) {
    const int sx = board_.x + screenColumnOf(square) * size;
    const int sy = board_.y + screenRowOf(square) * size;
    renderer.drawRect(sx - weight, sy - weight, size + 2 * weight, size + 2 * weight, weight, true);
  };

  // A hint frames the piece, then rings the square it goes to.
  if (session_.hintFrom() >= 0) frameSquare(session_.hintFrom(), toybox::kFrame + 2);
  if (session_.hintTo() >= 0) {
    const int sx = board_.x + screenColumnOf(session_.hintTo()) * size;
    const int sy = board_.y + screenRowOf(session_.hintTo()) * size;
    const int d = size / 2;
    renderer.fillRoundedRect(sx + (size - d) / 2, sy + (size - d) / 2, d, d, d / 2, Black);
    renderer.fillRoundedRect(sx + (size - d) / 2 + 6, sy + (size - d) / 2 + 6, d - 12, d - 12, d / 2 - 6, White);
  }
  if (selected_ >= 0) frameSquare(selected_, toybox::kFrame);

  // The wrong move is struck through where it landed, heavy enough to read
  // over a black piece: a white cross under a black one.
  if (session_.state() == chesspuz::State::Wrong) {
    const int to = session_.lastMove().to;
    const int sx = board_.x + screenColumnOf(to) * size;
    const int sy = board_.y + screenRowOf(to) * size;
    const int m = 6;
    renderer.drawLine(sx + m, sy + m, sx + size - m, sy + size - m, 10, false);
    renderer.drawLine(sx + m, sy + size - m, sx + size - m, sy + m, 10, false);
    renderer.drawLine(sx + m, sy + m, sx + size - m, sy + size - m, 5, true);
    renderer.drawLine(sx + m, sy + size - m, sx + size - m, sy + m, 5, true);
    frameSquare(to, toybox::kFrame);
  }

  renderer.drawRect(board_.x - toybox::kBoardFrame, board_.y - toybox::kBoardFrame, 8 * size + 2 * toybox::kBoardFrame,
                    8 * size + 2 * toybox::kBoardFrame, toybox::kBoardFrame, true);
}

// --- Flow ------------------------------------------------------------------

void ChessPuzzleActivity::go(const View next) {
  view_ = next;
  if (next == View::Results) {
    const int today = currentDay();
    haveWindow_ = today >= 0;
    if (haveWindow_) window_ = chesspuz::windowFor(history_, today, page_);
  }
  fullRefresh_ = true;
  requestUpdate();
}

void ChessPuzzleActivity::routeAction(const int action, const int value, const int x, const int y) {
  switch (action) {
    case chesspuzui::ActionLevel:
      startLevel(value - 1);
      return;
    case chesspuzui::ActionHints:
      progress_.hintsOn = !progress_.hintsOn;
      saveProgress();
      requestUpdate();
      return;
    case chesspuzui::ActionScores:
      page_ = 0;
      go(View::Results);
      return;
    case chesspuzui::ActionBoard: {
      int square = -1;
      if (squareAt(x, y, square)) tapSquare(square);
      return;
    }
    case chesspuzui::ActionUndo:
      if (replyAt_ == 0 && session_.undo()) {
        selected_ = -1;
        rememberCounts();
        chess::generateLegalMoves(session_.position(), legal_);
        requestUpdate();
      }
      return;
    case chesspuzui::ActionHint:
      if (replyAt_ == 0 && session_.canHint()) {
        session_.hint();
        selected_ = -1;
        rememberCounts();
        requestUpdate();
      }
      return;
    case chesspuzui::ActionSkip:
    case chesspuzui::ActionNext:
      if (replyAt_ == 0) advance();
      return;
    case chesspuzui::ActionOlder:
      if (haveWindow_ && window_.hasOlder) {
        ++page_;
        go(View::Results);
      }
      return;
    case chesspuzui::ActionNewer:
      if (page_ > 0) {
        --page_;
        go(View::Results);
      }
      return;
    case chesspuzui::ActionPlay:
      if (active_ && !session_.finished()) {
        go(View::Puzzle);
      } else if (active_) {
        advance();
      } else {
        go(View::Menu);
      }
      return;
    default:
      return;
  }
}

// --- Input and paint ---------------------------------------------------------

void ChessPuzzleActivity::loop() {
  if (replyAt_ != 0 && static_cast<int32_t>(millis() - replyAt_) >= 0) {
    replyAt_ = 0;
    session_.reply(buffers_);
    chess::generateLegalMoves(session_.position(), legal_);
    requestUpdate();
  }

  // Back is also the left-edge swipe. The puzzle and the scores go back to the
  // menu; the menu leaves.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Menu) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    if (view_ == View::Puzzle) {
      rememberCounts();
      // A solved puzzle is done with; the menu then offers the next one.
      if (session_.finished()) active_ = false;
    }
    go(View::Menu);
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);
  routeAction(static_cast<int>(event.action), static_cast<int>(event.value), tapX, tapY);
}

void ChessPuzzleActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, toybox::Faces{toybox::kButtonFontId, toybox::kLargeFontId, toybox::kDisplayFontId});
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Menu: {
      chesspuzui::MenuModel model;
      model.hintsOn = progress_.hintsOn;
      for (int i = 0; i < chesspuz::kLevels; ++i) {
        model.count[i] = chesspuz::kPuzzleLevels[i].count;
        model.next[i] = model.count[i] > 0 ? progress_.index[i] % model.count[i] + 1 : 0;
      }
      const int today = currentDay();
      model.haveToday = today >= 0;
      for (const chesspuz::DayRecord& r : history_) {
        if (r.day != today) continue;
        model.todayPlayed = r.played;
        model.todayClean = r.clean;
      }
      chesspuzui::buildMenu(screen, model);
      break;
    }
    case View::Puzzle: {
      chesspuzui::PuzzleModel model;
      model.mateIn = session_.puzzle().mateIn;
      model.count = chesspuz::kPuzzleLevels[level_].count;
      model.number = progress_.index[level_] % (model.count > 0 ? model.count : 1) + 1;
      if (session_.finished()) model.number = (model.number + model.count - 2) % model.count + 1;
      model.playerWhite = session_.puzzle().playerWhite;
      model.state = session_.state();
      model.movesMade = session_.movesMade();
      model.hintsOn = session_.hintsOn();
      model.hintsUsed = session_.hintsUsed();
      model.undosUsed = session_.undosUsed();
      model.canUndo = session_.canUndo();
      model.canHint = session_.canHint();
      model.thinking = replyAt_ != 0;
      board_ = chesspuzui::buildPuzzle(screen, model);
      drawBoard();
      break;
    }
    case View::Results: {
      chesspuzui::ResultsModel model;
      model.window = haveWindow_ ? &window_ : nullptr;
      model.page = page_;
      chesspuzui::buildResults(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "ChessPuzzle");
  renderer.displayBuffer(fullRefresh_ ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefresh_ = false;
}
