#pragma once

// Unicorns on the device: Minesweeper for a five year old, where the mines are
// unicorns hiding under the squares. The thin layer: renderer, storage, input,
// shelf. The rules are in UnicornCore.h.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "UnicornCore.h"

class UnicornActivity final : public Activity {
 public:
  UnicornActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Unicorns", renderer, mappedInput) {}
  ~UnicornActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen : uint8_t { Menu, HowTo, Board, Result };

  void beginGame();
  void goTo(Screen next);
  void recordResult();
  void loadStars();

  Screen screen = Screen::Menu;
  unicorns::Game game{};
  // Which square a finger is resting on, since when, and whether the hold has
  // already placed its heart. Holding is a shortcut for HEART mode; the buttons
  // under the board are the way a child is shown.
  int holdColumn = -1;
  int holdRow = -1;
  unsigned long holdSinceMs = 0;
  bool holdFired = false;
  bool heartMode = false;
  int howToPage = 0;

  // One star per board cleared, kept on the card.
  int stars = 0;
  bool resultRecorded = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;

  // The grid is hit-tested against geometry and never reaches route(), so the
  // table digest cannot see a square tap. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;
};
