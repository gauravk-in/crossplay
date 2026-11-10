#pragma once

// MATH QUIZ: arithmetic practice for school kids.
//
// Pick your class, answer sums with four choices each, and END to see two
// charts: the share right and the average seconds per answer, today against
// the fortnight before it. OLDER and NEWER page back through earlier
// fortnights. See docs/apps/mathquiz.md.
//
// The activity is the thin layer: it owns the files, the round and the input.
// Questions, history and chart windows are in MathQuizCore (freestanding,
// host-tested) and the drawing is in MathQuizScreens (freestanding).

#include <cstdint>
#include <memory>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "MathQuizCore.h"

class MathQuizActivity final : public Activity {
 public:
  MathQuizActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MathQuiz", renderer, mappedInput) {}
  ~MathQuizActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Question, Results };

  int currentDay() const;
  void loadHistory();
  void saveHistory();
  void saveGrade();
  void startRound(int grade);
  void deal();
  void answer(int choice);
  void showResults();
  void go(View next);
  void routeAction(int action, int value);

  View view_ = View::Menu;
  int grade_ = 0;
  mathquiz::Rng rng_;
  mathquiz::Question question_;
  int chosen_ = -1;

  // This round.
  int asked_ = 0;
  int right_ = 0;
  uint32_t roundMs_ = 0;
  int lastTenths_ = 0;

  // Set from the paint that first shows a question, so the time a child has
  // to answer starts when the sum is on the glass rather than when it was
  // dealt. Zero until then.
  uint32_t shownAt_ = 0;
  bool timing_ = false;

  std::vector<mathquiz::DayRecord> history_;
  mathquiz::Window window_;
  bool haveWindow_ = false;
  int page_ = 0;

  bool fullRefresh_ = true;
  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
