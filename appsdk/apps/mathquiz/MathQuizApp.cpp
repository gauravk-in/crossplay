// MATH QUIZ as a downloadable app: the same questions and history
// (MathQuizCore) and the same screens (MathQuizScreens) as the built-in app,
// with this file standing in for MathQuizActivity.

#include <CrossPlayApp.h>

#include <cstdio>
#include <cstdlib>

#include "../../../src/apps_local/mathquiz/MathQuizCore.h"
#include "../../../src/apps_local/mathquiz/MathQuizScreens.h"

namespace {

namespace fui = freeink::ui;

constexpr const char* kHistoryFile = "history.txt";
constexpr const char* kGradeFile = "class.txt";

// One typical sum per class for the picker, in the question format.
constexpr const char* kSamples[mathquiz::kGrades] = {
    "7 + 5", "46 - 18", "6 * 7", "84 / 4", "2.5 + 1.8", "-3 + 8",
};

class MathQuizApp final : public cpapp::App {
 public:
  bool init() override {
    rng_ = mathquiz::Rng(cp_random());
    history_ = mathquiz::parseHistory(cpapp::readFile(kHistoryFile, mathquiz::kMaxHistoryBytes));
    const std::string saved = cpapp::readFile(kGradeFile, 8);
    const int grade = saved.empty() ? 0 : std::atoi(saved.c_str());
    grade_ = grade >= 1 && grade <= mathquiz::kGrades ? grade : 0;
    go(View::Menu);
    return true;
  }

  // A round ends to its charts, the charts go back to the class picker, the
  // picker leaves.
  bool onBack() override {
    switch (view_) {
      case View::Question:
        showResults();
        return true;
      case View::Results:
        go(View::Menu);
        return true;
      case View::Menu:
        return false;
    }
    return false;
  }

  void onTap(const int x, const int y) override {
    if (!interactionsReady_) return;
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    const fui::ActionEvent event = interactions_.route(input);
    route(static_cast<int>(event.action), static_cast<int>(event.value));
  }

  void render(cpapp::Target& target) override {
    // Labels in the button cut, sums and answers in the 44px cut, the band in
    // the display cut.
    target.setFaces(cpapp::Faces{CP_FACE_BUTTON, CP_FACE_LARGE, CP_FACE_DISPLAY});
    const fui::InputSnapshot noInput{};
    interactionsReady_ = false;
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);

    switch (view_) {
      case View::Menu: {
        mathquizui::MenuModel model;
        model.grade = grade_;
        for (int i = 0; i < mathquiz::kGrades; ++i) model.sample[i] = kSamples[i];
        mathquizui::buildMenu(screen, model);
        break;
      }
      case View::Question: {
        mathquizui::QuestionModel model;
        model.grade = grade_;
        model.question = &question_;
        model.chosen = chosen_;
        model.asked = asked_;
        model.right = right_;
        model.tenthsSec = lastTenths_;
        mathquizui::buildQuestion(screen, model);
        break;
      }
      case View::Results: {
        mathquizui::ResultsModel model;
        model.window = haveWindow_ ? &window_ : nullptr;
        model.page = page_;
        model.asked = asked_;
        model.right = right_;
        model.tenthsSec = mathquiz::tenthsOf(roundMs_, asked_);
        mathquizui::buildResults(screen, model);
        break;
      }
    }
    interactionsReady_ = true;
    if (interactions_.overflowed()) cpapp::logf(0, "more controls than the interaction table holds");
  }

  // The clock for an answer starts when the sum is on the glass, not when it
  // was dealt.
  void onShown() override {
    if (view_ == View::Question && chosen_ < 0 && !timing_) {
      shownAt_ = cpapp::nowMs();
      timing_ = true;
    }
  }

 private:
  enum class View : uint8_t { Menu, Question, Results };

  void saveHistory() {
    if (!cpapp::writeFile(kHistoryFile, mathquiz::formatHistory(history_))) {
      cpapp::logf(0, "history was not written");
    }
  }

  void startRound(const int grade) {
    if (grade != grade_) {
      grade_ = grade;
      char text[16];
      std::snprintf(text, sizeof(text), "%d\n", grade_);
      cpapp::writeFile(kGradeFile, text);
    }
    asked_ = 0;
    right_ = 0;
    roundMs_ = 0;
    deal();
  }

  void deal() {
    question_ = mathquiz::makeQuestion(grade_, rng_);
    chosen_ = -1;
    timing_ = false;
    shownAt_ = 0;
    go(View::Question);
  }

  void answer(const int choice) {
    if (chosen_ >= 0 || choice < 0 || choice >= mathquiz::kChoices) return;
    const uint32_t ms = timing_ ? cpapp::nowMs() - shownAt_ : 0;
    const uint32_t counted = ms > mathquiz::kMaxAnswerMs ? mathquiz::kMaxAnswerMs : ms;
    chosen_ = choice;
    const bool right = choice == question_.correct;
    ++asked_;
    if (right) ++right_;
    roundMs_ += counted;
    lastTenths_ = mathquiz::tenthsOf(counted, 1);

    // Written now rather than at END: a round left by Home or a flat battery
    // still counts.
    const int day = cpapp::currentDay();
    if (day >= 0) {
      mathquiz::addAnswer(history_, day, right, counted);
      saveHistory();
    }
    requestRender();
  }

  void showResults() {
    page_ = 0;
    go(View::Results);
  }

  void go(const View next) {
    view_ = next;
    if (next == View::Results) {
      const int today = cpapp::currentDay();
      haveWindow_ = today >= 0;
      if (haveWindow_) window_ = mathquiz::windowFor(history_, today, page_);
    }
    requestRender(next != View::Question ? CP_REFRESH_HALF : CP_REFRESH_FAST);
  }

  void route(const int action, const int value) {
    switch (action) {
      case mathquizui::ActionGrade:
        startRound(value);
        return;
      case mathquizui::ActionScores:
        asked_ = 0;
        showResults();
        return;
      case mathquizui::ActionChoice:
        answer(value);
        return;
      case mathquizui::ActionNext:
        deal();
        return;
      case mathquizui::ActionEnd:
        showResults();
        return;
      case mathquizui::ActionOlder:
        if (haveWindow_ && window_.hasOlder) {
          ++page_;
          go(View::Results);
        }
        return;
      case mathquizui::ActionNewer:
        if (page_ > 0) {
          --page_;
          go(View::Results);
        }
        return;
      case mathquizui::ActionPractise:
        if (grade_ >= 1) {
          startRound(grade_);
        } else {
          go(View::Menu);
        }
        return;
      default:
        return;
    }
  }

  View view_ = View::Menu;
  int grade_ = 0;
  mathquiz::Rng rng_;
  mathquiz::Question question_;
  int chosen_ = -1;

  int asked_ = 0;
  int right_ = 0;
  uint32_t roundMs_ = 0;
  int lastTenths_ = 0;
  uint32_t shownAt_ = 0;
  bool timing_ = false;

  std::vector<mathquiz::DayRecord> history_;
  mathquiz::Window window_;
  bool haveWindow_ = false;
  int page_ = 0;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};

}  // namespace

cpapp::App* cpapp::createApp() {
  static MathQuizApp app;
  return &app;
}
