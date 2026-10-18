#pragma once

// Aztec codes, drawn from their payload like a QR code is.
//
// Freestanding like WalletBars, so host-tests/wallet checks the encoder and
// tools_local/wallet/aztec_roundtrip.sh reads its output back with ZXing.
// Follows ISO/IEC 24778: compact symbols up to 4 layers, full-range up to 32,
// with a third of the codewords spent on error correction. The symbol layout
// is adapted from ZXing's Java Aztec encoder (Apache-2.0, THIRD-PARTY-NOTICES).

#include <cstdint>
#include <string>
#include <vector>

namespace wallet {

struct AztecSymbol {
  // Modules across, which is also modules down: 15 to 151.
  int size = 0;
  bool compact = false;
  int layers = 0;
  // Row-major, one bit a module, most significant bit first.
  std::vector<uint8_t> bits;

  bool dark(int x, int y) const {
    const int at = y * size + x;
    return (bits[static_cast<size_t>(at) >> 3] >> (7 - (at & 7))) & 1;
  }
};

// False when the payload is empty or holds more than the largest symbol does.
bool encodeAztec(const std::string& payload, AztecSymbol& out);

}  // namespace wallet
