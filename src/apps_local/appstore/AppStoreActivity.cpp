#include "AppStoreActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WasmAppActivity.h"

namespace fui = freeink::ui;
using appstore::CatalogEntry;
using appstore::InstalledApp;

namespace {

constexpr size_t kMaxCatalogBytes = 256 * 1024;
constexpr size_t kMaxAddressLength = 120;

std::string kilobytes(const uint32_t bytes) {
  char text[24];
  std::snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
  return text;
}

uint32_t packageBytes(const appstore::Manifest& m) {
  uint32_t total = 0;
  for (const auto& f : m.files) total += f.size;
  return total;
}

}  // namespace

std::unique_ptr<Activity> AppStoreActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<AppStoreActivity>(renderer, mappedInput);
}

bool AppStoreActivity::hasTouch() const { return mappedInput.hasTouch(); }

void AppStoreActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  server_ = appstore::configuredServer();
  // A staging folder outliving its install is a download a power cut ended.
  if (Storage.exists(appstore::kStagingDir)) Storage.removeDir(appstore::kStagingDir);
  refreshInstalled();
  showList(0);
}

void AppStoreActivity::onExit() {
  // The radio comes down with the activity, the way the other network apps do
  // it, unless Developer Mode raised it.
  if (raisedWifi_ && WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Activity::onExit();
}

// --- Models ------------------------------------------------------------------

void AppStoreActivity::refreshInstalled() { installed_ = appstore::listInstalled(hasTouch(), true); }

void AppStoreActivity::buildRows() {
  rows_.clear();
  if (tab_ == 0) {
    rows_.reserve(installed_.size());
    for (const InstalledApp& app : installed_) {
      appstoreui::Row row;
      row.title = app.manifest.name;
      row.detail = app.problem.empty() ? app.manifest.version + "  " + app.manifest.summary : app.problem;
      row.badge = app.problem.empty() ? "OPEN" : "CAN'T RUN";
      row.dim = !app.problem.empty();
      rows_.push_back(std::move(row));
    }
    return;
  }
  rows_.reserve(catalog_.size());
  for (const CatalogEntry& entry : catalog_) {
    appstoreui::Row row;
    row.title = entry.name;
    const std::string have = appstore::installedVersion(installed_, entry.id);
    switch (appstore::offerFor(entry, have)) {
      case appstore::Offer::Install:
        row.badge = "INSTALL";
        row.detail = entry.version + "  " + kilobytes(entry.size) + "  " + entry.summary;
        break;
      case appstore::Offer::Update:
        row.badge = "UPDATE";
        row.detail = have + " to " + entry.version + "  " + entry.summary;
        break;
      case appstore::Offer::Installed:
        row.badge = "INSTALLED";
        row.dim = true;
        row.detail = have + "  " + entry.summary;
        break;
      case appstore::Offer::Incompatible:
        row.badge = "NOT HERE";
        row.dim = true;
        row.detail = entry.reason.empty() ? "Does not run on this firmware." : entry.reason;
        break;
    }
    rows_.push_back(std::move(row));
  }
}

void AppStoreActivity::showList(const int tab) {
  tab_ = tab;
  view_ = View::List;
  detail_ = -1;
  buildRows();
  fullRefresh_ = true;
  requestUpdate();
}

void AppStoreActivity::showDetail(const int index) {
  const int count = tab_ == 0 ? static_cast<int>(installed_.size()) : static_cast<int>(catalog_.size());
  if (index < 0 || index >= count) return;
  detail_ = index;
  view_ = View::Detail;
  fullRefresh_ = true;
  requestUpdate();
}

void AppStoreActivity::showNotice(const std::string& title, const std::string& message, const char* button,
                                  const NoticeAction action) {
  if (view_ != View::Notice) afterNotice_ = view_;
  notice_ = appstoreui::NoticeModel{};
  notice_.title = title;
  notice_.message = message;
  notice_.button = button;
  noticeAction_ = action;
  view_ = View::Notice;
  fullRefresh_ = true;
  requestUpdate();
}

// --- The network -----------------------------------------------------------

void AppStoreActivity::openCatalog() {
  tab_ = 1;
  if (catalogLoaded_) {
    showList(1);
    return;
  }
  if (server_.empty()) {
    showNotice("NO STORE SET",
               "This device does not know which store to use yet. Type the address of the store server.", "SET ADDRESS",
               NoticeAction::SetAddress);
    return;
  }
  needNetwork(Work::FetchCatalog);
}

void AppStoreActivity::askAddress() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "STORE ADDRESS",
                                                           server_.empty() ? "http://" : server_, kMaxAddressLength);
  if (!keyboard) {
    showNotice("NO MEMORY", "There was not enough memory to open the keyboard.", "OK");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    interactionsReady_ = false;
    if (result.isCancelled) {
      showList(0);
      return;
    }
    const std::string typed = std::get<KeyboardResult>(result.data).text;
    const std::string server = appstore::serverFromConfig("{\"server\":\"" + typed + "\"}");
    if (server.empty() || !appstore::saveServer(server)) {
      showNotice("NOT AN ADDRESS", "A store address starts with http:// or https://.", "TRY AGAIN",
                 NoticeAction::SetAddress);
      return;
    }
    server_ = server;
    catalogLoaded_ = false;
    openCatalog();
  });
}

void AppStoreActivity::needNetwork(const Work then) {
  work_ = then;
#if defined(SIMULATOR)
  // The simulator's network is the host's; its WiFi only has to say so.
  if (WiFi.status() != WL_CONNECTED) WiFi.begin();
  const bool connected = true;
#else
  const bool connected = WiFi.status() == WL_CONNECTED;
#endif
  if (connected) {
    showNotice(then == Work::Install ? "INSTALLING" : "GET APPS",
               then == Work::Install ? "Starting the download." : "Asking the store what it has.", nullptr);
    workQueued_ = true;
    return;
  }
  WiFi.mode(WIFI_STA);
  raisedWifi_ = true;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    showNotice("NO MEMORY", "There was not enough memory to open the WiFi screen.", "OK");
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    interactionsReady_ = false;
    if (result.isCancelled) {
      const Work was = work_;
      work_ = Work::None;
      if (was == Work::Install) {
        view_ = View::Detail;
        fullRefresh_ = true;
        requestUpdate();
      } else {
        showList(0);
      }
      return;
    }
    showNotice(work_ == Work::Install ? "INSTALLING" : "GET APPS",
               work_ == Work::Install ? "Starting the download." : "Asking the store what it has.", nullptr);
    workQueued_ = true;
  });
}

void AppStoreActivity::runWork() {
  const Work work = work_;
  work_ = Work::None;
  switch (work) {
    case Work::FetchCatalog:
      fetchCatalog();
      return;
    case Work::Install:
      installSelected();
      return;
    case Work::None:
      return;
  }
}

void AppStoreActivity::fetchCatalog() {
  std::string text;
  const std::string url = appstore::catalogUrl(server_, hasTouch(), true, CROSSPOINT_VERSION);
  if (!HttpDownloader::fetchUrl(url, text)) {
    const int status = HttpDownloader::lastStatus();
    char message[200];
    if (status == 0) {
      std::snprintf(message, sizeof(message),
                    "Could not reach the store at %s. Is it running, and is this device on "
                    "the same network?",
                    server_.c_str());
    } else {
      std::snprintf(message, sizeof(message), "The store answered with an error (HTTP %d).", status);
    }
    showNotice("STORE OFFLINE", message, "SET ADDRESS", NoticeAction::SetAddress);
    afterNotice_ = View::List;
    tab_ = 0;
    return;
  }
  if (text.size() > kMaxCatalogBytes) {
    showNotice("STORE ERROR", "The catalog is larger than this device will read.", "OK");
    return;
  }
  std::string error;
  std::vector<CatalogEntry> entries;
  if (!appstore::parseCatalog(text, entries, error)) {
    showNotice("STORE ERROR", "The store sent a catalog this device cannot read: " + error, "OK");
    return;
  }
  catalog_ = std::move(entries);
  catalogLoaded_ = true;
  int runs = 0;
  for (const CatalogEntry& entry : catalog_) runs += entry.compatible ? 1 : 0;
  char status[96];
  std::snprintf(status, sizeof(status), "%d of %d run on app API %s", runs, static_cast<int>(catalog_.size()),
                appstore::firmwareApi().str().c_str());
  catalogStatus_ = status;
  page_[1] = 0;
  showList(1);
}

bool AppStoreActivity::onProgress(void* ctx, const uint32_t done, const uint32_t total) {
  auto* self = static_cast<AppStoreActivity*>(ctx);
  const int percent = total == 0 ? 0 : static_cast<int>(static_cast<uint64_t>(done) * 100 / total);
  // A panel refresh per chunk would take longer than the download.
  if (percent / 20 != self->lastProgress_ / 20) {
    self->lastProgress_ = percent;
    self->notice_.progress = percent;
    self->fullRefresh_ = false;
    self->requestUpdateAndWait();
  }
  return true;
}

void AppStoreActivity::installSelected() {
  if (detail_ < 0 || detail_ >= static_cast<int>(catalog_.size())) return;
  const CatalogEntry entry = catalog_[detail_];
  lastProgress_ = -1;
  notice_.progress = 0;
  notice_.message = "Downloading " + entry.name + " " + entry.version + " (" + kilobytes(entry.size) + ").";
  requestUpdateAndWait();
  std::string error;
  if (!appstore::install(entry, server_, hasTouch(), true, &AppStoreActivity::onProgress, this, error)) {
    showNotice("NOT INSTALLED", error, "OK");
    afterNotice_ = View::Detail;
    return;
  }
  refreshInstalled();
  buildRows();
  view_ = View::Detail;
  fullRefresh_ = true;
  requestUpdate();
}

// --- Installed apps ----------------------------------------------------------

void AppStoreActivity::launch(const InstalledApp& app) {
  if (!app.problem.empty()) {
    showNotice("CAN'T RUN", app.manifest.name + ": " + app.problem, "OK");
    return;
  }
  auto activity = makeUniqueNoThrow<WasmAppActivity>(renderer, mappedInput, app);
  if (!activity) {
    showNotice("NO MEMORY", "There was not enough memory to open it.", "OK");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    interactionsReady_ = false;
    fullRefresh_ = true;
    requestUpdate();
  });
}

void AppStoreActivity::removeSelected() {
  if (detail_ < 0 || detail_ >= static_cast<int>(installed_.size())) return;
  const std::string id = installed_[detail_].manifest.id;
  std::string error;
  if (!appstore::uninstall(id, false, error)) {
    showNotice("NOT REMOVED", error, "OK");
    return;
  }
  refreshInstalled();
  showList(0);
}

// --- Input -------------------------------------------------------------------

void AppStoreActivity::onPrimary() {
  if (view_ == View::Notice) {
    switch (noticeAction_) {
      case NoticeAction::SetAddress:
        askAddress();
        return;
      case NoticeAction::ConfirmRemove:
        removeSelected();
        return;
      case NoticeAction::Dismiss:
        view_ = afterNotice_ == View::Notice ? View::List : afterNotice_;
        if (view_ == View::List) buildRows();
        fullRefresh_ = true;
        requestUpdate();
        return;
    }
  }
  if (view_ != View::Detail) return;
  if (tab_ == 0) {
    launch(installed_[detail_]);
    return;
  }
  const CatalogEntry& entry = catalog_[detail_];
  const std::string have = appstore::installedVersion(installed_, entry.id);
  switch (appstore::offerFor(entry, have)) {
    case appstore::Offer::Install:
    case appstore::Offer::Update:
      needNetwork(Work::Install);
      return;
    case appstore::Offer::Installed:
      for (const InstalledApp& app : installed_) {
        if (app.manifest.id == entry.id) launch(app);
      }
      return;
    case appstore::Offer::Incompatible:
      return;
  }
}

void AppStoreActivity::onSecondary() {
  if (view_ != View::Detail || tab_ != 0) return;
  const InstalledApp& app = installed_[detail_];
  showNotice("REMOVE " + app.manifest.name + "?",
             "It and everything it saved come off the card. You can install it again from the store.", "REMOVE",
             NoticeAction::ConfirmRemove);
  afterNotice_ = View::Detail;
}

void AppStoreActivity::loop() {
  if (workQueued_) {
    workQueued_ = false;
    runWork();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Notice:
        view_ = afterNotice_ == View::Notice ? View::List : afterNotice_;
        if (view_ == View::List) buildRows();
        fullRefresh_ = true;
        requestUpdate();
        return;
      case View::Detail:
        showList(tab_);
        return;
      case View::List:
        shelf::leave(renderer, mappedInput);
        return;
    }
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent event = interactions_.route(input);
  switch (event.action) {
    case appstoreui::ActionTab:
      if (event.value == 1) {
        openCatalog();
      } else {
        showList(0);
      }
      return;
    case appstoreui::ActionRow:
      showDetail(event.value);
      return;
    case appstoreui::ActionOlder:
      if (page_[tab_] > 0) --page_[tab_];
      fullRefresh_ = false;
      requestUpdate();
      return;
    case appstoreui::ActionNewer:
      if (page_[tab_] + 1 < pages_) ++page_[tab_];
      fullRefresh_ = false;
      requestUpdate();
      return;
    case appstoreui::ActionPrimary:
      onPrimary();
      return;
    case appstoreui::ActionSecondary:
      onSecondary();
      return;
    default:
      return;
  }
}

// --- Paint -------------------------------------------------------------------

void AppStoreActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::List: {
      appstoreui::ListModel model;
      model.tab = tab_;
      model.rows = &rows_;
      model.page = page_[tab_];
      if (tab_ == 0) {
        model.empty = "No apps yet. GET APPS shows what the store has for this device.";
        char status[64];
        std::snprintf(status, sizeof(status), "This firmware runs app API %s", appstore::firmwareApi().str().c_str());
        model.status = status;
      } else {
        model.empty = "The store has no apps yet.";
        model.status = catalogStatus_;
      }
      pages_ = appstoreui::buildList(screen, model);
      break;
    }
    case View::Detail: {
      appstoreui::DetailModel model;
      if (tab_ == 0 && detail_ >= 0 && detail_ < static_cast<int>(installed_.size())) {
        const InstalledApp& app = installed_[detail_];
        model.title = app.manifest.name;
        model.summary = app.manifest.summary;
        model.facts = {{"VERSION", app.manifest.version},
                       {"APP API", app.manifest.api.str()},
                       {"BY", app.manifest.author.empty() ? "-" : app.manifest.author},
                       {"SIZE", kilobytes(packageBytes(app.manifest))},
                       {"ID", app.manifest.id}};
        model.warning = app.problem;
        model.primary = app.problem.empty() ? "OPEN" : nullptr;
        model.secondary = "REMOVE";
      } else if (tab_ == 1 && detail_ >= 0 && detail_ < static_cast<int>(catalog_.size())) {
        const CatalogEntry& entry = catalog_[detail_];
        const std::string have = appstore::installedVersion(installed_, entry.id);
        model.title = entry.name;
        model.summary = entry.summary;
        model.facts = {{"VERSION", entry.version.empty() ? entry.latest : entry.version},
                       {"APP API", entry.api.str()},
                       {"BY", entry.author.empty() ? "-" : entry.author},
                       {"SIZE", kilobytes(entry.size)},
                       {"INSTALLED", have.empty() ? "no" : have}};
        switch (appstore::offerFor(entry, have)) {
          case appstore::Offer::Install:
            model.primary = "INSTALL";
            break;
          case appstore::Offer::Update:
            model.primary = "UPDATE";
            break;
          case appstore::Offer::Installed:
            model.primary = "OPEN";
            break;
          case appstore::Offer::Incompatible:
            model.warning = entry.reason.empty() ? "No version of this app runs on this firmware." : entry.reason;
            break;
        }
        if (entry.compatible && !entry.latest.empty() && entry.latest != entry.version) {
          model.warning = "Version " + entry.latest + " exists but needs a newer firmware.";
        }
      }
      appstoreui::buildDetail(screen, model);
      break;
    }
    case View::Notice:
      appstoreui::buildNotice(screen, notice_);
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "AppStore");
  renderer.displayBuffer(fullRefresh_ ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefresh_ = false;
}
