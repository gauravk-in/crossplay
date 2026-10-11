#pragma once

// APP STORE: the apps on this card, and the ones a store server offers.
//
// INSTALLED lists what is on the card and opens it. GET APPS asks the
// configured server for its catalog, tagged with this firmware's app API and
// this device's inputs; the server marks what will run here, and the device
// checks again rather than trusting it. See docs/app-store.md.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "AppLibrary.h"
#include "AppStoreScreens.h"

class AppStoreActivity final : public Activity {
 public:
  AppStoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppStore", renderer, mappedInput) {}
  ~AppStoreActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { List, Detail, Notice };
  enum class Work : uint8_t { None, FetchCatalog, Install };
  // What the notice's button does.
  enum class NoticeAction : uint8_t { Dismiss, SetAddress, ConfirmRemove };

  void refreshInstalled();
  void buildRows();
  void showList(int tab);
  void showDetail(int index);
  void showNotice(const std::string& title, const std::string& message, const char* button,
                  NoticeAction action = NoticeAction::Dismiss);
  void openCatalog();
  void askAddress();
  void needNetwork(Work then);
  void runWork();
  void fetchCatalog();
  void installSelected();
  void launch(const appstore::InstalledApp& app);
  void removeSelected();
  void onPrimary();
  void onSecondary();
  static bool onProgress(void* ctx, uint32_t done, uint32_t total);

  bool hasTouch() const;

  View view_ = View::List;
  int tab_ = 0;
  int page_[2] = {0, 0};
  int pages_ = 1;

  std::vector<appstore::InstalledApp> installed_;
  std::vector<appstore::CatalogEntry> catalog_;
  bool catalogLoaded_ = false;
  std::string catalogStatus_;
  std::vector<appstoreui::Row> rows_;

  int detail_ = -1;  // index into installed_ or catalog_, by tab_
  appstoreui::NoticeModel notice_;
  NoticeAction noticeAction_ = NoticeAction::Dismiss;
  View afterNotice_ = View::List;

  std::string server_;
  Work work_ = Work::None;
  bool workQueued_ = false;
  bool raisedWifi_ = false;
  int lastProgress_ = -1;

  bool fullRefresh_ = true;
  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
