#include "MemoryActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;
namespace mem = memory;

namespace {

constexpr const char* kDir = "/.crosspoint/memory";
constexpr const char* kHistoryPath = "/.crosspoint/memory/history.txt";
constexpr const char* kSizePath = "/.crosspoint/memory/size.txt";
// Below this the clock was never set; the same floor Study and Math Quiz use.
constexpr time_t kClockFloor = 1700000000;

std::string readFile(const char* path, const size_t cap) {
  std::string out;
  if (!Storage.exists(path)) return out;
  if (!Storage.readFileToString("MEMORY", path, cap, out)) out.clear();
  return out;
}

// Written beside the file and renamed over it, so a power cut mid-write leaves
// yesterday's history rather than none.
bool writeFile(const char* path, const std::string& text) {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("MEMORY", "could not make %s", kDir);
    return false;
  }
  const std::string part = std::string(path) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("MEMORY", part.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(part.c_str(), path)) {
    Storage.remove(part.c_str());
    LOG_ERR("MEMORY", "could not replace %s", path);
    return false;
  }
  return true;
}

uint32_t freshSeed() { return static_cast<uint32_t>(millis()) * 2654435761u ^ static_cast<uint32_t>(time(nullptr)); }

}  // namespace

std::unique_ptr<Activity> MemoryActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<MemoryActivity>(renderer, mappedInput);
}

void MemoryActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadHistory();
  const std::string saved = readFile(kSizePath, 8);
  const int size = saved.empty() ? 0 : std::atoi(saved.c_str());
  size_ = static_cast<mem::Size>(size >= 0 && size < mem::kSizes ? size : 0);
  go(View::Menu);
}

// --- Data ----------------------------------------------------------------

int MemoryActivity::currentDay() const {
  const time_t now = time(nullptr);
  if (now < kClockFloor) return -1;
  tm local{};
  localtime_r(&now, &local);
  return mem::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void MemoryActivity::loadHistory() { history_ = mem::parseHistory(readFile(kHistoryPath, mem::kMaxHistoryBytes)); }

void MemoryActivity::saveSettings() {
  char text[8];
  snprintf(text, sizeof(text), "%d\n", static_cast<int>(size_));
  writeFile(kSizePath, text);
}

void MemoryActivity::refreshWindow(const mem::Size size) {
  const int today = currentDay();
  haveWindow_ = today >= 0;
  if (haveWindow_) window_ = mem::windowFor(history_, today, size);
}

// --- The game ------------------------------------------------------------

void MemoryActivity::startLocal(const Mode mode) {
  mode_ = mode;
  seat_ = 0;
  mem::deal(game_, size_, mode == Mode::Pass ? 2 : 1, freshSeed());
  dealt_ = true;
  recorded_ = false;
  news_ = "";
  go(View::Board);
}

void MemoryActivity::flipCard(const int index) {
  if (view_ != View::Board || !dealt_ || game_.over) return;
  if (mode_ == Mode::Nearby && (!inMatch() || !linkYourTurn())) return;

  const mem::Game before = game_;
  const mem::Flip result = mem::flip(game_, index);
  if (result == mem::Flip::Refused) return;
  const mem::Recall recall = mem::lastRecall(before, game_, before.turn);

  switch (result) {
    case mem::Flip::First:
      news_ = "";
      break;
    case mem::Flip::Pair:
      news_ = recall == mem::Recall::Hit ? "A HIT, AND A PAIR" : "A PAIR. GO AGAIN";
      break;
    case mem::Flip::Miss:
      news_ = recall == mem::Recall::Miss ? "A MISS: ITS PAIR WAS SEEN" : "NO PAIR";
      break;
    case mem::Flip::Refused:
      return;
  }

  // A match sends the board only when the turn changes hands or the game
  // ends: a turn's pairs stay face up and its miss stays up for study, so the
  // other device sees everything this turn turned.
  if (mode_ == Mode::Nearby && (result == mem::Flip::Miss || game_.over)) play_.play(game_);
  if (game_.over && mode_ != Mode::Nearby) {
    finishGame();
    return;
  }
  requestUpdate();
}

void MemoryActivity::finishGame() {
  if (!recorded_) {
    recorded_ = true;
    charted_ = false;
    tooFew_ = false;
    // Two people on one device: the device cannot say whose memory this was.
    const int seat = mode_ == Mode::Solo ? 0 : (mode_ == Mode::Nearby ? seat_ : -1);
    if (seat >= 0) {
      mem::GameRecord record;
      record.size = game_.size;
      record.hits = game_.hits[seat];
      record.misses = game_.misses[seat];
      tooFew_ = !mem::counts(record);
      record.day = currentDay();
      if (record.day > 0) {
        history_.push_back(record);
        if (!writeFile(kHistoryPath, mem::formatHistory(history_))) LOG_ERR("MEMORY", "history was not written");
        charted_ = !tooFew_;
      }
    }
  }
  refreshWindow(static_cast<mem::Size>(game_.size));
  go(View::Result);
}

void MemoryActivity::go(const View next) {
  if (next == View::Menu) refreshWindow(size_);
  fullRefresh_ = next != view_;
  view_ = next;
  requestUpdate();
}

// --- The match -----------------------------------------------------------

const char* MemoryActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (!dealt_ || !game_.over) return "MEMORY";
  const int win = mem::winner(game_);
  if (win < 0) return "A DRAW";
  return win == seat_ ? "YOU WIN" : "THEY WIN";
}

void MemoryActivity::onMatchStart(const bool goesFirst) {
  // The leader deals at its own board size and sends the deal at once, which
  // hands the first turn to the follower: that send is the only way both
  // devices can hold the same shuffled board, and the coin toss already made
  // the order fair.
  mode_ = Mode::Nearby;
  seat_ = goesFirst ? 1 : 0;
  recorded_ = false;
  news_ = "";
  if (goesFirst) {
    mem::deal(game_, size_, 2, freshSeed());
    dealt_ = true;
    play_.play(game_);
  } else {
    game_ = mem::Game{};
    dealt_ = false;
  }
  go(View::Board);
}

bool MemoryActivity::takeOpponentState() {
  if (!play_.takeOpponent(game_)) return false;
  dealt_ = true;
  news_ = game_.upCount == 2 ? "THEY MISSED. YOUR TURN" : "";
  return true;
}

// Their rolled name is three words, which a seat box cannot hold beside a
// score. The first word is enough to tell two players apart.
void MemoryActivity::shortenName() {
  const char* full = opponentName();
  size_t n = 0;
  while (full != nullptr && full[n] != '\0' && full[n] != ' ' && n + 1 < sizeof(theirName_)) {
    theirName_[n] = full[n];
    ++n;
  }
  theirName_[n] = '\0';
  if (n == 0) snprintf(theirName_, sizeof(theirName_), "THEM");
  theirBadge_[0] = theirName_[0];
  theirBadge_[1] = '\0';
}

void MemoryActivity::onRematch() { onMatchStart(play_.goesFirst()); }

void MemoryActivity::onLinkEnded() {
  mode_ = Mode::Solo;
  seat_ = 0;
  go(View::Menu);
}

void MemoryActivity::onMatchEnded() { finishGame(); }

// --- Input and paint -----------------------------------------------------

void MemoryActivity::gameLoop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (view_ == View::Menu) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    go(View::Menu);
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);

  switch (event.action) {
    case memoryui::ActionSize: {
      const int s = event.value;
      if (s < 0 || s >= mem::kSizes || s == static_cast<int>(size_)) return;
      size_ = static_cast<mem::Size>(s);
      saveSettings();
      refreshWindow(size_);
      requestUpdate();
      return;
    }
    case memoryui::ActionMenuRow:
      switch (static_cast<memoryui::MenuRow>(event.value)) {
        case memoryui::MenuRow::Solo:
          startLocal(Mode::Solo);
          return;
        case memoryui::MenuRow::Pass:
          startLocal(Mode::Pass);
          return;
        case memoryui::MenuRow::Nearby:
          enterLink(linkplay::GameId::Memory);
          return;
        case memoryui::MenuRow::HowTo:
          go(View::HowTo);
          return;
        case memoryui::MenuRow::Count:
          return;
      }
      return;
    case memoryui::ActionBoard:
      flipCard(layout_.cardAt(tapX, tapY));
      return;
    case memoryui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      startLocal(mode_);
      return;
    case memoryui::ActionMenu:
      if (inMatch()) {
        leaveLink();
        return;
      }
      go(View::Menu);
      return;
    default:
      return;
  }
}

void MemoryActivity::gameRender() {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  const bool nearby = mode_ == Mode::Nearby;
  switch (view_) {
    case View::Menu: {
      memoryui::MenuModel model;
      model.size = static_cast<int>(size_);
      model.window = haveWindow_ ? &window_ : nullptr;
      memoryui::buildMenu(screen, model);
      break;
    }
    case View::Board: {
      if (!dealt_) {
        // A follower waiting for the deal: the board it would draw is not the
        // board it is about to be handed.
        memoryui::BoardModel model;
        mem::Game blank;
        blank.size = static_cast<uint8_t>(size_);
        blank.over = 1;
        model.game = &blank;
        model.seat = seat_;
        model.waiting = true;
        model.news = "DEALING";
        layout_ = memoryui::buildBoard(screen, model);
        break;
      }
      memoryui::BoardModel model;
      model.game = &game_;
      model.seat = nearby ? seat_ : (mode_ == Mode::Pass ? -1 : 0);
      if (nearby) {
        shortenName();
        model.names[seat_] = "YOU";
        model.names[1 - seat_] = theirName_;
        model.badges[seat_] = "Y";
        model.badges[1 - seat_] = theirBadge_;
      }
      model.waiting = nearby && !linkYourTurn();
      model.news = news_;
      layout_ = memoryui::buildBoard(screen, model);
      break;
    }
    case View::Result: {
      memoryui::ResultModel model;
      model.game = &game_;
      model.seat = nearby ? seat_ : (mode_ == Mode::Pass ? -1 : 0);
      if (nearby) {
        shortenName();
        model.names[seat_] = "YOU";
        model.names[1 - seat_] = theirName_;
      }
      model.window = model.seat >= 0 && haveWindow_ ? &window_ : nullptr;
      model.charted = charted_;
      model.tooFew = tooFew_;
      memoryui::buildResult(screen, model);
      break;
    }
    case View::HowTo:
      memoryui::buildHowTo(screen);
      break;
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Memory");
  renderer.displayBuffer(fullRefresh_ ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefresh_ = false;
}
