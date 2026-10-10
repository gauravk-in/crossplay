#include "UnicornActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "UnicornScreens.h"

namespace uc = unicorns;

namespace {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kStarsPath[] = "/.crosspoint/unicorns.sav";
#endif

// How long a finger rests on a square to place a heart. Longer than
// Minesweeper's 400ms: small children press slowly, and a heart they did not
// mean blocks the square they were trying to open.
constexpr unsigned long kHeartHoldMs = 700;
}  // namespace

std::unique_ptr<Activity> UnicornActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<UnicornActivity>(renderer, mappedInput);
}

void UnicornActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = Screen::Menu;
  loadStars();
  requestUpdate();
}

void UnicornActivity::loadStars() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kStarsPath)) return;
  char buffer[16] = {};
  if (Storage.readFileToBuffer(kStarsPath, buffer, sizeof(buffer)) == 0) return;
  char* end = nullptr;
  const long value = strtol(buffer, &end, 10);
  if (end == buffer || value < 0) return;
  stars = static_cast<int>(value);
#endif
}

void UnicornActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;
  if (game.status != uc::Status::Won) return;
  ++stars;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[16];
  snprintf(line, sizeof(line), "%d\n", stars);
  Storage.writeFile(kStarsPath, String(line));
#endif
}

void UnicornActivity::goTo(const Screen next) {
  screen = next;
  requestUpdate();
}

void UnicornActivity::beginGame() {
  uc::start(game, static_cast<uint32_t>(millis()) * 2654435761u + 1u);
  holdColumn = -1;
  holdRow = -1;
  holdFired = false;
  heartMode = false;
  resultRecorded = false;
  goTo(Screen::Board);
}

// What a tap on the grid means depends on the screen (the menu's PLAY row sits
// where squares are), the tool, and whether the round is over. None of the
// three is in the interaction table, so they are folded in here.
uint32_t UnicornActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  const uint32_t withTool = paintclock::mixMeaning(withScreen, heartMode ? 1u : 0u);
  return paintclock::mixMeaning(withTool, uc::over(game) ? 1u : 0u);
}

void UnicornActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screen == Screen::Menu) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(Screen::Menu);
    return;
  }

  if (screen == Screen::Board && uc::over(game)) recordResult();

  // Hold to place a heart. Done here against the drawing's own geometry for the
  // reason Minesweeper gives: 36 squares are not in the interaction buffer.
  if (screen == Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int hx = 0;
    int hy = 0;
    int column = 0;
    int row = 0;
    if (mappedInput.isScreenTouchHeld(hx, hy) && unicornui::cellAt(device, hx, hy, column, row)) {
      if (!surfaceRevealed()) {
        holdColumn = -1;
        holdRow = -1;
        holdFired = false;
        return;
      }
      if (column != holdColumn || row != holdRow) {
        holdColumn = column;
        holdRow = row;
        holdSinceMs = millis();
        holdFired = false;
        requestUpdate();
      } else if (!holdFired && millis() - holdSinceMs >= kHeartHoldMs) {
        holdFired = true;
        if (uc::toggleHeart(game, column, row)) requestUpdate();
        // Unconditional, so the lift after a hold never also opens a square.
        // See MinesweeperActivity for the case that makes this load-bearing.
        mappedInput.swallowCurrentTouch();
      }
      return;
    }
    if (holdColumn >= 0) {
      holdColumn = -1;
      holdRow = -1;
      requestUpdate();
    }
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  if (screen == Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int column = 0;
    int row = 0;
    if (unicornui::cellAt(device, tapX, tapY, column, row)) {
      if (!surfaceRevealed()) return;
      const bool changed = heartMode ? uc::toggleHeart(game, column, row) : uc::open(game, column, row);
      if (changed) requestUpdate();
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case unicornui::ActionMenuRow:
      switch (static_cast<unicornui::MenuRow>(event.value)) {
        case unicornui::MenuRow::Play:
          beginGame();
          return;
        case unicornui::MenuRow::HowTo:
          howToPage = 0;
          goTo(Screen::HowTo);
          return;
        case unicornui::MenuRow::Count:
          return;
      }
      return;

    case unicornui::ActionHowToNext:
      if (howToPage + 1 < unicornui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      beginGame();
      return;

    case unicornui::ActionAgain:
      beginGame();
      return;

    case unicornui::ActionDone:
      goTo(Screen::Menu);
      return;

    case unicornui::ActionTool: {
      const bool wantHeart = event.value == 1;
      if (wantHeart != heartMode) {
        heartMode = wantHeart;
        requestUpdate();
      }
      return;
    }

    case unicornui::ActionSeeResult:
      goTo(Screen::Result);
      return;

    default:
      return;
  }
}

void UnicornActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case Screen::Menu: {
      unicornui::MenuModel model;
      model.stars = stars;
      unicornui::buildMenu(surface, model);
      break;
    }
    case Screen::HowTo: {
      unicornui::HowToModel model;
      model.page = howToPage;
      unicornui::buildHowTo(surface, model);
      break;
    }
    case Screen::Board: {
      unicornui::BoardModel model;
      model.game = game;
      model.holdColumn = holdColumn;
      model.holdRow = holdRow;
      model.heartMode = heartMode;
      unicornui::buildBoard(surface, model);
      break;
    }
    case Screen::Result: {
      unicornui::ResultModel model;
      model.won = game.status == uc::Status::Won;
      model.stars = stars;
      unicornui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Unicorns");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
