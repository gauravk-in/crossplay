#pragma once

// Google Calendar, on the reader: the Schedule view of every calendar the
// account shows, read only.
//
// ---------------------------------------------------------------------------
// The shape of it, and the decisions worth knowing.
//
// 1. Opens offline, on today, from the card. Paging (the side keys or the
//    arrows) walks the schedule forward and back; TODAY returns.
//    The card holds two weeks back and three months ahead; see GCalCore.h.
//
// 2. Two things start a sync and they run the same function, as in Tasks.
//    REFRESH paints a busy screen first and the work happens on the next
//    pass. The charger's poll paints NOTHING first and repaints only when
//    what is on the glass would change. Off the charger nothing polls.
//
// 3. The Google account is Tasks' (one sign-in for both apps, asking for both
//    scopes), so signing in here runs Tasks' phone page and writes Tasks'
//    auth.cfg. A reader that signed in for Tasks before Calendar existed has a
//    token without the Calendar scope; Google says so on the first sync and
//    the app goes back to the sign-in screen to ask again.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../../network/CrossPointWebServer.h"
#include "../gtasks/GTasksApi.h"
#include "../gtasks/GTasksLibrary.h"
#include "../ui/ToyboxScreen.h"
#include "GCalApi.h"
#include "GCalCore.h"
#include "GCalLibrary.h"
#include "GCalScreens.h"

class GCalActivity final : public Activity {
 public:
  GCalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("GCal", renderer, mappedInput) {}
  ~GCalActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class Phase : uint8_t { Schedule, Settings, SignOutConfirm, SignIn, Phone, Busy, Notice };
  enum class Step : uint8_t { None, Sync, SignIn };

  void show(Phase phase);
  void showNotice(const char* headline, std::string message);
  void paintBusyNow(const char* headline);
  void requestStep(Step step, const char* headline);
  void onWifiChosen(bool connected);
  void runStep(Step step);
  void startSignIn();
  void startPhone();
  void stopPhone();
  void takePaste(const std::string& pasted);
  bool sync(std::string& message, bool& changed);
  bool ensureToken(std::string& message);
  // Gives a clock that was never given a zone the account's; true when it did.
  bool adoptAccountZone();
  void backgroundPoll();
  // Rebuilds the schedule from events_ for the reader's current day, keeping
  // the page on `keepDay` (or today when it is -1).
  void rebuild(int64_t keepDay);
  int64_t currentDay() const;
  void stepPage(int delta);
  void goToday();
  void toggleAsleep();
  void restoreSleepSettings(const gcal::Asleep& asleep);
  bool asleepOn() const;
  void reloadCredentials();
  void signOut();
  bool joinWifi(std::string& message);
  void releaseWifi();
  bool polling() const;

  gcal::Library library_;
  gcal::Api api_;
  gtasks::Library account_;
  gtasks::Api auth_;
  gtasks::Client client_;
  gtasks::Credentials creds_;
  gtasks::AccessToken token_;
  gcal::Settings settings_;
  gcal::Meta meta_;
  std::vector<gcal::Event> events_;

  // The schedule as drawn, and where the page starts in it. Heights come from
  // the renderer's fonts, so they are measured on the render pass and kept for
  // the next page turn.
  std::vector<gcal::Item> items_;
  std::vector<int> heights_;
  int pageHeight_ = 0;
  int first_ = 0;
  int shown_ = 0;
  int64_t builtFor_ = -1;  // the day items_ was built on
  int todayIndex_ = 0;     // the first item of today
  bool measured_ = false;  // heights_ matches items_

  Phase phase_ = Phase::Schedule;
  Step step_ = Step::None;
  Step afterWifi_ = Step::None;
  std::string busyHeadline_ = "SYNCING";
  std::string noticeHeadline_;
  std::string noticeMessage_;
  std::string signInReason_;

  std::unique_ptr<CrossPointWebServer> server_;
  std::string verifier_;
  std::string state_;
  std::string phoneUrl_;
  std::string phoneReadable_;

  bool everAttempted_ = false;
  uint32_t lastAttemptMs_ = 0;
  bool lastPollFailed_ = false;
  bool wasCharging_ = false;

  bool broughtRadioUp_ = false;
  bool yieldedDevMode_ = false;

  char status_[24] = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
  static constexpr uint32_t kSettleMs = 600;
  Phase lastShownPhase_ = Phase::Schedule;
  uint32_t phaseShownAtMs_ = 0;
  bool everShown_ = false;
};
