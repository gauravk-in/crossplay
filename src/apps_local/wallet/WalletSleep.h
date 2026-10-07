#pragma once

// A card on the sleep screen (Settings > Sleep screen > Card).
//
// Drawn live when the device goes to sleep, from the card's file, in the
// card's own look, as Notes draws its note: a boarding pass at the gate is
// then one press of the power button away, with nothing to unlock or open.
//
// The choice lives in /.crosspoint/cards-asleep.txt (wallet::AsleepChoice).
// The moon on an open card writes it and switches the setting in one tap.

#include "WalletCore.h"
#include "WalletScreens.h"

class GfxRenderer;

namespace wallet {

constexpr const char* kAsleepFile = "/.crosspoint/cards-asleep.txt";

bool readAsleep(AsleepChoice& out);
bool writeAsleep(const AsleepChoice& choice);
void clearAsleep();

// Draws `payload` as a QR code centred in `square`; false when it cannot be
// drawn at a size a scanner reads. Shared by the open card and the sleep
// screen so the two are the same code at the same size.
bool drawCode(GfxRenderer& renderer, const freeink::ui::Rect& square, const std::string& payload);

// Draws the chosen card into the renderer's buffer and returns true; the
// caller puts it on the panel. False, having drawn nothing worth keeping, when
// no card is chosen or it has been deleted, so the caller falls back to the
// default sleep screen instead of an empty page.
bool drawAsleep(GfxRenderer& renderer);

}  // namespace wallet
