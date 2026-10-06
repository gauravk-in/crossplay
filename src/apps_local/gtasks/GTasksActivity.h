#pragma once

// Google Tasks, on the reader.
//
// ---------------------------------------------------------------------------
// The shape of it, and the decisions worth knowing.
//
// 1. Opens offline, on the list the card already has. Ticking works with no
//    radio at all: a tick is written to the card as pending and goes up with
//    the next sync. See GTasksCore.h.
//
// 2. Two things start a sync, and they run the same function. REFRESH paints
//    a busy screen first and the work happens on the following pass, the way
//    Instapaper's SYNC does. The charger's poll paints NOTHING first: it runs
//    every minute by default, and a busy screen each time would be a full
//    e-ink flash a minute for a list that usually has not changed. It repaints
//    afterwards only when what is on the glass would change.
//
// 3. Polling is a property of this app being open on the charger, not of the
//    device. It needs no wake timer and cannot drain a battery: off the
//    charger nothing polls, and preventAutoSleep() is true only on it. The
//    radio a poll brings up stays up between polls on the charger, because
//    rejoining Wi-Fi every minute is most of what a poll would cost, and is
//    put down when the cable comes out.
//
// 4. Signing in happens on the reader, with no service in between. Google's
//    device sign-in does not allow the Tasks scope, so the reader runs the
//    installed-app flow: it serves a one-page sign-in site on its own Wi-Fi
//    address and shows it as a QR. The phone opens Google's consent screen
//    from there, lands on a 127.0.0.1 page that will not load, and pastes that
//    address back. The reader trades the code in it for a refresh token and
//    keeps it in auth.cfg on the card. See GTasksCore.h, "Signing in".
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../../network/CrossPointWebServer.h"
#include "../ui/ToyboxScreen.h"
#include "GTasksApi.h"
#include "GTasksCore.h"
#include "GTasksLibrary.h"
#include "GTasksScreens.h"

class GTasksActivity final : public Activity {
 public:
  GTasksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("GTasks", renderer, mappedInput) {}
  ~GTasksActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  // Awake only while it is doing the job it was left on the charger to do.
  bool preventAutoSleep() override;
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class Phase : uint8_t { List, Settings, SignOutConfirm, SignIn, Phone, Busy, Notice };
  // What the busy screen announced and the next pass runs.
  enum class Step : uint8_t { None, Sync, SignIn };

  void show(Phase phase);
  void showNotice(const char* headline, std::string message);
  void paintBusyNow(const char* headline);
  void requestStep(Step step, const char* headline);
  void requestRefresh();
  void onWifiChosen(bool connected);
  void runStep(Step step);
  void startSignIn();
  void startPhone();
  void stopPhone();
  void takePaste(const std::string& pasted);
  // The whole sync: push ticks, read the list, merge, save. Returns false with
  // `message` filled when it could not finish. `changed` says whether the
  // glass would now look different.
  bool sync(std::string& message, bool& changed);
  bool ensureToken(std::string& message);
  void backgroundPoll();
  void toggle(int index);
  void stepPage(int delta);
  void reloadCredentials();
  void signOut();
  bool joinWifi(std::string& message);
  void releaseWifi();
  bool polling() const;

  gtasks::Library library_;
  gtasks::Api api_;
  gtasks::Client client_;
  gtasks::Credentials creds_;
  gtasks::AccessToken token_;
  gtasks::Settings settings_;
  gtasks::Meta meta_;
  std::vector<gtasks::Task> tasks_;

  Phase phase_ = Phase::List;
  Step step_ = Step::None;
  // The step the Wi-Fi picker interrupted, run once it connects.
  Step afterWifi_ = Step::None;
  std::string busyHeadline_ = "SYNCING";
  std::string noticeHeadline_;
  std::string noticeMessage_;
  // Why the sign-in screen is up, when it is not simply "never signed in".
  std::string signInReason_;

  // A sign-in in flight: the page the phone opens, and the PKCE verifier and
  // state its consent address was made with. Nothing reaches the card until
  // Google has traded the pasted code for a token.
  std::unique_ptr<CrossPointWebServer> server_;
  std::string verifier_;
  std::string state_;
  std::string phoneUrl_;
  std::string phoneReadable_;

  // The charger's schedule. Attempts, not successes: see gtasks::pollDue.
  bool everAttempted_ = false;
  uint32_t lastAttemptMs_ = 0;
  // The last background poll could not reach Google; the band says OFFLINE.
  bool lastPollFailed_ = false;
  bool wasCharging_ = false;

  // Radio bookkeeping, the Live engine's: put down only what we picked up.
  bool broughtRadioUp_ = false;
  bool yieldedDevMode_ = false;

  int page_ = 0;
  int perPage_ = 1;
  // Owned here because the screen model holds pointers.
  std::vector<gtasksui::Row> rows_;
  std::vector<std::string> dueLabels_;
  char status_[24] = "";
  char pageLabel_[16] = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
  // A tap within this long of a screen appearing answers the previous screen.
  // REFRESH and the notice's button share the footer's pixels. See
  // InstapaperActivity.h for the bug that made this a rule.
  static constexpr uint32_t kSettleMs = 600;
  Phase lastShownPhase_ = Phase::List;
  uint32_t phaseShownAtMs_ = 0;
  bool everShown_ = false;
};
