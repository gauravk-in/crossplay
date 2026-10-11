#include "MathQuizActivity.h"

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
#include "MathQuizScreens.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kDir = "/.crosspoint/mathquiz";
constexpr const char* kHistoryPath = "/.crosspoint/mathquiz/history.txt";
constexpr const char* kGradePath = "/.crosspoint/mathquiz/class.txt";
// Below this the clock was never set; the same floor Study and Live use.
constexpr time_t kClockFloor = 1700000000;

// One typical sum per class for the picker, in the question format.
constexpr const char* kSamples[mathquiz::kGrades] = {
    "7 + 5", "46 - 18", "6 * 7", "84 / 4", "2.5 + 1.8", "-3 + 8",
};

std::string readFile(const char* path, const size_t cap) {
  std::string out;
  if (!Storage.exists(path)) return out;
  if (!Storage.readFileToString("MATHQUIZ", path, cap, out)) out.clear();
  return out;
}

// Written beside the file and renamed over it, so a power cut mid-write leaves
// yesterday's history rather than none.
bool writeFile(const char* path, const std::string& text) {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("MATHQUIZ", "could not make %s", kDir);
    return false;
  }
  const std::string part = std::string(path) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("MATHQUIZ", part.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(part.c_str(), path)) {
    Storage.remove(part.c_str());
    LOG_ERR("MATHQUIZ", "could not replace %s", path);
    return false;
  }
  return true;
}

}  // namespace

std::unique_ptr<Activity> MathQuizActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<MathQuizActivity>(renderer, mappedInput);
}

void MathQuizActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  rng_ = mathquiz::Rng(static_cast<uint32_t>(millis()) ^ static_cast<uint32_t>(time(nullptr)));
  loadHistory();
  const std::string saved = readFile(kGradePath, 8);
  const int grade = saved.empty() ? 0 : std::atoi(saved.c_str());
  grade_ = grade >= 1 && grade <= mathquiz::kGrades ? grade : 0;
  go(View::Menu);
}

// --- Data ----------------------------------------------------------------

int MathQuizActivity::currentDay() const {
  const time_t now = time(nullptr);
  if (now < kClockFloor) return -1;
  tm local{};
  localtime_r(&now, &local);
  return mathquiz::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void MathQuizActivity::loadHistory() {
  history_ = mathquiz::parseHistory(readFile(kHistoryPath, mathquiz::kMaxHistoryBytes));
}

void MathQuizActivity::saveHistory() {
  if (!writeFile(kHistoryPath, mathquiz::formatHistory(history_))) LOG_ERR("MATHQUIZ", "history was not written");
}

void MathQuizActivity::saveGrade() {
  char text[16];
  snprintf(text, sizeof(text), "%d\n", grade_);
  writeFile(kGradePath, text);
}

// --- The round -----------------------------------------------------------

void MathQuizActivity::startRound(const int grade) {
  if (grade != grade_) {
    grade_ = grade;
    saveGrade();
  }
  asked_ = 0;
  right_ = 0;
  roundMs_ = 0;
  deal();
}

void MathQuizActivity::deal() {
  question_ = mathquiz::makeQuestion(grade_, rng_);
  chosen_ = -1;
  timing_ = false;
  shownAt_ = 0;
  go(View::Question);
}

void MathQuizActivity::answer(const int choice) {
  if (chosen_ >= 0 || choice < 0 || choice >= mathquiz::kChoices) return;
  const uint32_t now = millis();
  // A tap before the first paint landed cannot have been an answer to a sum
  // nobody saw; RevealGate already holds those back, this is the backstop.
  const uint32_t ms = timing_ ? now - shownAt_ : 0;
  const uint32_t counted = ms > mathquiz::kMaxAnswerMs ? mathquiz::kMaxAnswerMs : ms;
  chosen_ = choice;
  const bool right = choice == question_.correct;
  ++asked_;
  if (right) ++right_;
  roundMs_ += counted;
  lastTenths_ = mathquiz::tenthsOf(counted, 1);

  // Written now rather than at END: a round left by the Home key or a flat
  // battery still counts.
  const int day = currentDay();
  if (day >= 0) {
    mathquiz::addAnswer(history_, day, right, counted);
    saveHistory();
  }
  requestUpdate();
}

void MathQuizActivity::showResults() {
  page_ = 0;
  go(View::Results);
}

void MathQuizActivity::go(const View next) {
  view_ = next;
  if (next == View::Results) {
    const int today = currentDay();
    haveWindow_ = today >= 0;
    if (haveWindow_) window_ = mathquiz::windowFor(history_, today, page_);
  }
  fullRefresh_ = next != View::Question;
  requestUpdate();
}

void MathQuizActivity::routeAction(const int action, const int value) {
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

// --- Input and paint -----------------------------------------------------

void MathQuizActivity::loop() {
  // Back is also the left-edge swipe on this board. A round ends to its
  // charts, the charts go back to the class picker, the picker leaves.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Question:
        showResults();
        return;
      case View::Results:
        go(View::Menu);
        return;
      case View::Menu:
        shelf::leave(renderer, mappedInput);
        return;
    }
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
  routeAction(static_cast<int>(event.action), static_cast<int>(event.value));
}

void MathQuizActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // Labels in the button cut, sums and answers in the 44px cut, the band in
  // the display cut.
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, toybox::Faces{toybox::kButtonFontId, toybox::kLargeFontId, toybox::kDisplayFontId});
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
  toybox::reportOverflow(interactions_, "MathQuiz");
  renderer.displayBuffer(fullRefresh_ ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefresh_ = false;

  if (view_ == View::Question && chosen_ < 0 && !timing_) {
    shownAt_ = millis();
    timing_ = true;
  }
}
