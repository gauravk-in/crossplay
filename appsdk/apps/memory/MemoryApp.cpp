// MEMORY as a downloadable app: the same rules (MemoryCore) and the same
// screens (MemoryScreens) as the built-in game, with this file standing in for
// MemoryActivity. What it leaves out is PLAY NEARBY, because the app API has no
// radio: alone and pass-and-play only.

#include <CrossPlayApp.h>

#include <cstdio>
#include <cstdlib>

#include "../../../src/apps_local/memory/MemoryCore.h"
#include "../../../src/apps_local/memory/MemoryScreens.h"
#include "../../../src/apps_local/ui/ToyboxTokens.h"

namespace {

namespace mem = memory;
namespace fui = freeink::ui;

constexpr const char* kHistoryFile = "history.txt";
constexpr const char* kSizeFile = "size.txt";

class MemoryApp final : public cpapp::App {
 public:
  bool init() override {
    history_ = mem::parseHistory(cpapp::readFile(kHistoryFile, mem::kMaxHistoryBytes));
    const std::string saved = cpapp::readFile(kSizeFile, 8);
    const int size = saved.empty() ? 0 : std::atoi(saved.c_str());
    size_ = static_cast<mem::Size>(size >= 0 && size < mem::kSizes ? size : 0);
    go(View::Menu);
    return true;
  }

  bool onBack() override {
    if (view_ == View::Menu) return false;
    go(View::Menu);
    return true;
  }

  void onTap(const int x, const int y) override {
    if (!interactionsReady_) return;
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    const fui::ActionEvent event = interactions_.route(input);

    switch (event.action) {
      case memoryui::ActionSize: {
        const int s = event.value;
        if (s < 0 || s >= mem::kSizes || s == static_cast<int>(size_)) return;
        size_ = static_cast<mem::Size>(s);
        char text[8];
        std::snprintf(text, sizeof(text), "%d\n", s);
        cpapp::writeFile(kSizeFile, text);
        refreshWindow(size_);
        requestRender();
        return;
      }
      case memoryui::ActionMenuRow:
        switch (static_cast<memoryui::MenuRow>(event.value)) {
          case memoryui::MenuRow::Solo:
            start(Mode::Solo);
            return;
          case memoryui::MenuRow::Pass:
            start(Mode::Pass);
            return;
          case memoryui::MenuRow::HowTo:
            go(View::HowTo);
            return;
          default:
            return;
        }
      case memoryui::ActionBoard:
        flip(layout_.cardAt(x, y));
        return;
      case memoryui::ActionAgain:
        start(mode_);
        return;
      case memoryui::ActionMenu:
        go(View::Menu);
        return;
      default:
        return;
    }
  }

  void render(cpapp::Target& target) override {
    // The faces MemoryActivity binds (toybox::proseMenuFaces).
    target.setFaces(cpapp::Faces{CP_FACE_BUTTON, CP_FACE_UI, CP_FACE_DISPLAY});
    const fui::InputSnapshot noInput{};
    interactionsReady_ = false;
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);

    switch (view_) {
      case View::Menu: {
        memoryui::MenuModel model;
        model.size = static_cast<int>(size_);
        model.window = haveWindow_ ? &window_ : nullptr;
        model.nearby = false;
        memoryui::buildMenu(screen, model);
        break;
      }
      case View::Board: {
        memoryui::BoardModel model;
        model.game = &game_;
        model.seat = mode_ == Mode::Pass ? -1 : 0;
        model.news = news_;
        layout_ = memoryui::buildBoard(screen, model);
        break;
      }
      case View::Result: {
        memoryui::ResultModel model;
        model.game = &game_;
        model.seat = mode_ == Mode::Pass ? -1 : 0;
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
    interactionsReady_ = true;
    if (interactions_.overflowed()) cpapp::logf(0, "more controls than the interaction table holds");
  }

 private:
  enum class View : uint8_t { Menu, Board, Result, HowTo };
  enum class Mode : uint8_t { Solo, Pass };

  void refreshWindow(const mem::Size size) {
    const int today = cpapp::currentDay();
    haveWindow_ = today >= 0;
    if (haveWindow_) window_ = mem::windowFor(history_, today, size);
  }

  void start(const Mode mode) {
    mode_ = mode;
    mem::deal(game_, size_, mode == Mode::Pass ? 2 : 1, cp_random());
    recorded_ = false;
    news_ = "";
    go(View::Board);
  }

  void flip(const int index) {
    if (view_ != View::Board || game_.over) return;
    const mem::Game before = game_;
    const mem::Flip result = mem::flip(game_, index);
    if (result == mem::Flip::Refused) return;
    // A fast paint leaves a ghost of a face turned back down; the half refresh
    // scrubs every pixel.
    const bool scrub = before.upCount == 2;
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
    if (game_.over) {
      finish();
      return;
    }
    requestRender(scrub ? CP_REFRESH_HALF : CP_REFRESH_FAST);
  }

  void finish() {
    if (!recorded_) {
      recorded_ = true;
      charted_ = false;
      tooFew_ = false;
      // Two people on one device: the device cannot say whose memory this was.
      if (mode_ == Mode::Solo) {
        mem::GameRecord record;
        record.size = game_.size;
        record.hits = game_.hits[0];
        record.misses = game_.misses[0];
        tooFew_ = !mem::counts(record);
        record.day = cpapp::currentDay();
        if (record.day > 0) {
          history_.push_back(record);
          if (!cpapp::writeFile(kHistoryFile, mem::formatHistory(history_))) {
            cpapp::logf(0, "history was not written");
          }
          charted_ = !tooFew_;
        }
      }
    }
    refreshWindow(static_cast<mem::Size>(game_.size));
    go(View::Result);
  }

  void go(const View next) {
    if (next == View::Menu) refreshWindow(size_);
    const bool changed = next != view_;
    view_ = next;
    requestRender(changed ? CP_REFRESH_HALF : CP_REFRESH_FAST);
  }

  View view_ = View::Menu;
  Mode mode_ = Mode::Solo;
  mem::Size size_ = mem::Size::Small;
  mem::Game game_{};
  const char* news_ = "";
  bool recorded_ = false;
  bool charted_ = false;
  bool tooFew_ = false;

  std::vector<mem::GameRecord> history_;
  mem::Window window_{};
  bool haveWindow_ = false;

  memoryui::BoardLayout layout_{};
  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};

}  // namespace

// MemoryScreens names the PLAY NEARBY mark, which this build never draws
// (MenuModel::nearby is false). The link library is not linked in, so the
// symbol is satisfied with an empty icon.
namespace linkui {
const freeink::Icon& nearbyMark() {
  static const freeink::Icon kNone{0, 0, 0, nullptr};
  return kNone;
}
}  // namespace linkui

cpapp::App* cpapp::createApp() {
  static MemoryApp app;
  return &app;
}
