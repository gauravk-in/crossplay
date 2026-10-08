#pragma once

// Prompter: a teleprompter, paged rather than scrolled, because an e-ink panel
// turns a page cleanly and smears a scroll.
//
// Scripts arrive from a phone (the QR code opens a page served by the reader)
// or as .txt files in /prompter on the card. The text is cut into pages at the
// size picked in Settings. A page turns by tap (left third back, right third
// forward), by the side keys, by a Bluetooth page turner, or by the page timer;
// a tap in the middle starts and pauses the timer.
//
// The usual split: PrompterCore holds the rules and has a host suite,
// PrompterScreens lays out the menus, PrompterTurner is the Bluetooth link, and
// this file keeps what needs the panel, the card and the radio.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "PrompterCore.h"
#include "PrompterServer.h"
#include "PrompterTurner.h"

class PrompterActivity final : public Activity {
 public:
  PrompterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Prompter", renderer, mappedInput) {}
  ~PrompterActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Somebody reading from it is not touching it, and a page turner's keys are
  // not the reader's own input.
  bool preventAutoSleep() override { return view_ == View::Reading || (server_ && server_->isRunning()); }
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class View : uint8_t { Library, Reading, Settings, Turner, Phone, Notice };

  // Everything render() reads is changed under a RenderLock.
  void loadSettings();
  void saveSettings();
  void refreshScripts();
  bool openScript(const std::string& name, bool resume);
  // Cuts the open script into pages for the current size and orientation,
  // keeping the reader on the words they were looking at.
  void repaginate();
  void setView(View view);
  void showNotice(const std::string& text);
  void leaveReading();

  void turnPage(int delta, bool byHand);
  void toggleTimer();

  void startTurner();
  void applyTurnerSettings();
  std::string turnerStatus() const;
  std::string turnerLine() const;

  void startPhone();
  void stopPhone();
  bool joinWifi();
  void releaseWifi();

  void handleAction(const freeink::ui::ActionEvent& action);
  void readingInput();

  void drawReading();
  void drawLine(int fontId, int x, int y, const prompter::Line& line, bool black);
  void drawSizeSample(const freeink::ui::Rect& box);

  int fontId() const;
  int fontIdFor(int size) const;

  prompter::Settings settings_;
  std::vector<std::string> scripts_;
  std::vector<std::string> names_;  // display names, parallel to scripts_
  int listTop_ = 0;
  int listFit_ = 1;

  View view_ = View::Library;
  View settingsReturn_ = View::Library;
  std::string notice_;

  // The open script.
  std::string scriptName_;
  std::string text_;
  prompter::Paged paged_;
  int page_ = 0;
  // What the pages were cut for; a change re-cuts them.
  int pagedSize_ = -1;
  bool pagedLandscape_ = false;
  std::string scratch_;

  prompter::PageTimer timer_;
  int shownPermille_ = -1;
  int paintsSinceClean_ = 0;
  bool cleanNext_ = true;

  prompter::TurnerLink turner_;
  uint32_t turnerGen_ = 0;
  bool turnerScanning_ = false;
  std::vector<prompter::TurnerLink::Found> found_;
  std::vector<std::string> foundLabels_;
  std::string lastTurn_;

  std::unique_ptr<PrompterServer> server_;
  bool devPaused_ = false;
  bool broughtRadioUp_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;
  std::string phoneSaved_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
