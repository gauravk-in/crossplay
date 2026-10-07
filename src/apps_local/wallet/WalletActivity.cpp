#include "WalletActivity.h"

#include <ESPmDNS.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <qrcode.h>

#include <cstdio>
#include <cstring>

#include "../../DevMode.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WalletStore.h"

namespace fui = freeink::ui;

std::unique_ptr<Activity> WalletActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WalletActivity>(renderer, mappedInput);
}

void WalletActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  if (!wallet::store::begin()) {
    showNotice("The SD card would not open, so there are no cards to show.");
    return;
  }
  reload();
  openList();
}

void WalletActivity::onExit() {
  stopPhone();
  Activity::onExit();
}

// --- Data ----------------------------------------------------------------

void WalletActivity::reload() {
  cards_ = wallet::store::loadAll();
  rows_.clear();
  rows_.reserve(cards_.size());
  for (const wallet::Card& card : cards_) {
    walletui::ListRow row;
    row.title = card.title.empty() ? "UNTITLED" : card.title.c_str();
    row.caption = card.caption.c_str();
    rows_.push_back(row);
  }
}

// "1/2" for the list when it runs past a page, and the top row snapped onto a
// page that exists after the cards under it changed.
void WalletActivity::relabel() {
  pageLabel_.clear();
  const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const int page = walletui::listCapacity(target.deviceContext());
  const int count = static_cast<int>(rows_.size());
  if (page <= 0 || count <= page) {
    listTop_ = 0;
    return;
  }
  if (listTop_ >= count) listTop_ = ((count - 1) / page) * page;
  listTop_ = (listTop_ / page) * page;
  char label[24];
  std::snprintf(label, sizeof(label), "%d/%d", listTop_ / page + 1, (count + page - 1) / page);
  pageLabel_ = label;
}

// --- Navigation ----------------------------------------------------------

void WalletActivity::openList() {
  view_ = View::List;
  open_ = -1;
  relabel();
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::openCard(const int index) {
  if (index < 0 || index >= static_cast<int>(cards_.size())) return;
  open_ = index;
  char position[24];
  std::snprintf(position, sizeof(position), "%d/%d", index + 1, static_cast<int>(cards_.size()));
  position_ = position;
  view_ = View::Card;
  interactionsReady_ = false;
  requestUpdate();
}

// Next and previous stop at the ends rather than wrapping: at a till, landing
// back on the first card reads as "that was the wrong button".
void WalletActivity::step(const int delta) {
  const int next = open_ + delta;
  if (next < 0 || next >= static_cast<int>(cards_.size())) return;
  openCard(next);
}

void WalletActivity::showNotice(const char* text) {
  notice_ = text;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::askDelete() {
  if (open_ < 0 || open_ >= static_cast<int>(cards_.size())) return;
  confirm_ = "Delete this card from the reader? It cannot be brought back here; add it again from your phone.";
  view_ = View::ConfirmDelete;
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::deleteOpen() {
  if (open_ < 0 || open_ >= static_cast<int>(cards_.size())) return;
  if (!wallet::store::remove(cards_[static_cast<size_t>(open_)].file)) {
    showNotice("The SD card would not delete it, so the card is still there.");
    return;
  }
  const int was = open_;
  reload();
  if (cards_.empty()) {
    openList();
    return;
  }
  // The card that slid into its place, or the new last one.
  openCard(was < static_cast<int>(cards_.size()) ? was : static_cast<int>(cards_.size()) - 1);
}

// --- The phone -----------------------------------------------------------

void WalletActivity::startPhone() {
#ifndef SIMULATOR
  // Never launch the picker unconditionally: it disconnects a working
  // association on every path. The same guard Notes and Workouts use.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                               showNotice("Adding cards from your phone needs Wi-Fi. Nothing changed.");
                               return;
                             }
                             startPhone();
                           });
    return;
  }
#endif

  // Developer Mode holds port 80 while its toggle is on; it yields while this
  // screen is up, and every way out of here goes through stopPhone().
  devmode::pause();
  devPaused_ = true;

  server_ = makeUniqueNoThrow<WalletServer>();
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
  const bool started = server_->begin();
#ifndef SIMULATOR
  if (!started) {
    stopPhone();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
#else
  (void)started;
#endif

#ifdef SIMULATOR
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  // The code carries the address, which depends on no service; the name, which
  // does, is only what a person reads.
  phoneUrl_ = "http://" + dotted + "/cards";
#ifdef SIMULATOR
  phoneReadable_ = phoneUrl_;
#else
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/cards" : phoneUrl_;
#endif
  view_ = View::Phone;
  interactionsReady_ = false;
  requestUpdate();
}

void WalletActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
}

// --- Input ---------------------------------------------------------------

void WalletActivity::loop() {
  // Back is read above the tap guard: the global back-swipe arrives as
  // Button::Back, and a swipe is not a tap.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::List:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Phone:
        stopPhone();
        reload();
        openList();
        return;
      case View::ConfirmDelete:
        openCard(open_);
        return;
      case View::Card:
      case View::Notice:
        openList();
        return;
    }
  }

  // The side keys: a page of the list, or the next and previous card.
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (down || up) {
    if (view_ == View::Card) {
      step(down ? 1 : -1);
    } else if (view_ == View::List) {
      const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
      const int page = walletui::listCapacity(target.deviceContext());
      const int count = static_cast<int>(rows_.size());
      const int next = down ? listTop_ + page : listTop_ - page;
      if (page > 0 && count > page && next >= 0 && next < count) {
        listTop_ = next;
        relabel();
        interactionsReady_ = false;
        requestUpdate();
      }
    }
    return;
  }

  if (server_ && server_->isRunning()) {
    // Pumped from loop(): there are no background threads in this firmware.
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    if (server_->takeChanged()) {
      reload();
      interactionsReady_ = false;
      requestUpdate();
    }
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input{};
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent action = interactions_.route(input);

  switch (action.action) {
    case walletui::ActionOpenCard:
      openCard(action.value);
      return;
    case walletui::ActionPrev:
      step(-1);
      return;
    case walletui::ActionNext:
      step(1);
      return;
    case walletui::ActionDelete:
      askDelete();
      return;
    case walletui::ActionDeleteConfirm:
      deleteOpen();
      return;
    case walletui::ActionDeleteKeep:
      openCard(open_);
      return;
    case walletui::ActionUsePhone:
      startPhone();
      return;
    case walletui::ActionDismiss:
      stopPhone();
      reload();
      openList();
      return;
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------

bool WalletActivity::drawCode(const fui::Rect& square, const std::string& payload) const {
  if (payload.empty() || square.width <= 0) return false;
  // The version is chosen from the capacity table, never by trial: the QR
  // library does not check that the data fits the version it is given and
  // writes past its buffers when it does not. M first, the level most codes
  // at a till were printed with; L when only L holds it.
  bool medium = true;
  int version = wallet::qrVersionFor(payload.size(), true);
  if (version == 0) {
    medium = false;
    version = wallet::qrVersionFor(payload.size(), false);
  }
  if (version == 0) return false;
  auto modules = makeUniqueNoThrow<uint8_t[]>(qrcode_getBufferSize(static_cast<uint8_t>(version)));
  if (!modules) {
    LOG_ERR("CARDS", "OOM: QR version %d", version);
    return false;
  }
  QRCode qr;
  const uint8_t ecc = medium ? ECC_MEDIUM : ECC_LOW;
  int8_t result;
  // initText picks numeric or alphanumeric mode when the payload allows it, so
  // a member number draws as a smaller code. It reads a C string, so a payload
  // with a NUL byte in it goes through initBytes whole.
  if (std::memchr(payload.data(), '\0', payload.size()) == nullptr) {
    result = qrcode_initText(&qr, modules.get(), static_cast<uint8_t>(version), ecc, payload.c_str());
  } else {
    std::string bytes = payload;
    result = qrcode_initBytes(&qr, modules.get(), static_cast<uint8_t>(version), ecc,
                              reinterpret_cast<uint8_t*>(bytes.data()), static_cast<uint16_t>(bytes.size()));
  }
  if (result != 0) {
    LOG_ERR("CARDS", "QR encode failed at version %d", version);
    return false;
  }
  // Whole pixels per module, with two modules of white inside the square on
  // every side on top of the page's own margin: the quiet zone a scanner
  // needs to find the corners.
  const int px = square.width / (qr.size + 4);
  if (px < 2) return false;
  const int drawn = qr.size * px;
  const int x0 = square.x + (square.width - drawn) / 2;
  const int y0 = square.y + (square.height - drawn) / 2;
  for (uint8_t cy = 0; cy < qr.size; cy++) {
    // Runs of dark modules as one rectangle each, so a row is a few fills
    // rather than a hundred.
    uint8_t cx = 0;
    while (cx < qr.size) {
      if (!qrcode_getModule(&qr, cx, cy)) {
        cx++;
        continue;
      }
      const uint8_t start = cx;
      while (cx < qr.size && qrcode_getModule(&qr, cx, cy)) cx++;
      renderer.fillRect(x0 + px * start, y0 + px * cy, px * (cx - start), px, true);
    }
  }
  return true;
}

void WalletActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  const bool cardOpen = open_ >= 0 && open_ < static_cast<int>(cards_.size());
  switch (view_) {
    case View::List: {
      walletui::ListModel model;
      model.rows = rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.firstVisible = listTop_;
      model.pageLabel = pageLabel_.empty() ? nullptr : pageLabel_.c_str();
      walletui::buildList(screen, model);
      break;
    }
    case View::Card: {
      if (!cardOpen) break;
      const wallet::Card& card = cards_[static_cast<size_t>(open_)];
      walletui::CardModel model;
      model.title = card.title.empty() ? "UNTITLED" : card.title.c_str();
      model.caption = card.caption.c_str();
      model.position = position_.c_str();
      model.hasPrev = open_ > 0;
      model.hasNext = open_ + 1 < static_cast<int>(cards_.size());
      const fui::Rect square = walletui::buildCard(screen, model);
      if (!drawCode(square, card.payload)) {
        walletui::buildCardFailure(screen, square, "This code holds too much to draw on the panel.");
      }
      break;
    }
    case View::ConfirmDelete:
      walletui::buildDeleteConfirm(screen, cardOpen ? cards_[static_cast<size_t>(open_)].title.c_str() : "CARDS",
                                   confirm_.c_str());
      break;
    case View::Phone: {
      walletui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.added = server_ ? server_->added() : 0;
      const fui::Rect qr = walletui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice:
      walletui::buildNotice(screen, notice_.c_str());
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Cards");
  renderer.displayBuffer();
}
