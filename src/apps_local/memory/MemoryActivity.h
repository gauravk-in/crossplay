#pragma once

// MEMORY: turn two cards at a time and find the pairs. Alone, two players on
// one device, or two devices side by side (PLAY NEARBY, the same link every
// other two-player game uses). A pair keeps the turn; a miss passes it.
//
// What it scores is recall, not luck: see MemoryCore.h. The menu charts each
// day's best recall for the last two weeks, per board size.
//
// The activity is the thin layer: files, input, the match. Rules, scoring and
// the chart window are in MemoryCore (freestanding, host-tested) and the
// drawing is in MemoryScreens (freestanding).

#include <cstdint>
#include <memory>
#include <vector>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "MemoryCore.h"
#include "MemoryScreens.h"

class MemoryActivity final : public linkplay::LinkActivity {
 public:
  MemoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Memory", renderer, mappedInput) {}
  ~MemoryActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  linkplay::PlayBase& linkState() override { return play_; }
  const linkplay::PlayBase& linkState() const override { return play_; }
  const char* linkGameTitle() const override { return "MEMORY"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return dealt_ && game_.over != 0; }
  void onMatchEnded() override;
  void gameLoop() override;
  void gameRender() override;

 private:
  enum class View : uint8_t { Menu, Board, Result, HowTo };
  enum class Mode : uint8_t { Solo, Pass, Nearby };

  int currentDay() const;
  void loadHistory();
  void saveSettings();
  void refreshWindow(memory::Size size);
  void startLocal(Mode mode);
  void flipCard(int index);
  void finishGame();
  void go(View next);
  void shortenName();

  View view_ = View::Menu;
  Mode mode_ = Mode::Solo;
  memory::Size size_ = memory::Size::Small;
  memory::Game game_{};
  // A match's follower has no board until the leader's deal arrives.
  bool dealt_ = false;
  int seat_ = 0;
  const char* news_ = "";
  bool recorded_ = false;
  bool charted_ = false;
  bool tooFew_ = false;
  char theirName_[10] = {};
  char theirBadge_[2] = {};

  // The whole shared state is the Game: 96 bytes, and the same rules both
  // devices play by.
  linkplay::Play<memory::Game> play_;

  std::vector<memory::GameRecord> history_;
  memory::Window window_{};
  bool haveWindow_ = false;

  memoryui::BoardLayout layout_{};
  bool fullRefresh_ = true;
};
