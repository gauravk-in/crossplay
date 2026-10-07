#include "WalletSleep.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <qrcode.h>

#include <cstring>
#include <string>

#include "../../components/themes/BaseTheme.h"  // Rect, which ToyboxTheme.h uses and does not include
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxTheme.h"
#include "WalletStore.h"

namespace fui = freeink::ui;

namespace wallet {

bool readAsleep(AsleepChoice& out) {
  std::string text;
  if (!Storage.exists(kAsleepFile)) return false;
  if (!Storage.readFileToString("CARDS", kAsleepFile, 160, text)) return false;
  return parseAsleep(text, out);
}

bool writeAsleep(const AsleepChoice& choice) {
  HalFile file;
  if (!Storage.openFileForWrite("CARDS", kAsleepFile, file)) {
    LOG_ERR("CARDS", "could not write %s", kAsleepFile);
    return false;
  }
  const std::string text = formatAsleep(choice);
  const size_t wrote = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  file.close();
  return wrote == text.size();
}

void clearAsleep() { Storage.remove(kAsleepFile); }

bool drawCode(GfxRenderer& renderer, const fui::Rect& square, const std::string& payload) {
  if (payload.empty() || square.width <= 0) return false;
  // The version is chosen from the capacity table, never by trial: the QR
  // library does not check that the data fits the version it is given and
  // writes past its buffers when it does not. M first, the level most codes
  // at a till were printed with; L when only L holds it.
  bool medium = true;
  int version = qrVersionFor(payload.size(), true);
  if (version == 0) {
    medium = false;
    version = qrVersionFor(payload.size(), false);
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

bool drawAsleep(GfxRenderer& renderer) {
  AsleepChoice choice;
  if (!readAsleep(choice)) {
    LOG_INF("CARDS", "sleep screen: no card chosen");
    return false;
  }
  Card card;
  if (!store::load(choice.file, card)) {
    LOG_INF("CARDS", "sleep screen: %s is gone", choice.file.c_str());
    return false;
  }

  toybox::ensureFonts(renderer);
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  toybox::Interactions interactions;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  walletui::CardModel model;
  model.title = card.title.c_str();
  model.caption = card.caption.c_str();
  model.asleep = true;
  const fui::Rect square = walletui::buildCard(screen, model);
  if (!drawCode(renderer, square, card.payload)) {
    walletui::buildCardFailure(screen, square, "This code holds too much to draw on the panel.");
  }
  LOG_INF("CARDS", "sleep screen: %s", choice.file.c_str());
  return true;
}

}  // namespace wallet
