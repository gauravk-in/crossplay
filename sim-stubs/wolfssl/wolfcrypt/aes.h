#pragma once

// Simulator stand-in for the two wolfCrypt AES calls CrossPointWebServer's
// /api/crypto "aesenc" op makes. Both fail, so the endpoint answers
// "aesenc failed" in the simulator; see sim-stubs/WolfsslCrypto.h.

struct Aes {
  int unused = 0;
};

#define AES_ENCRYPTION 0

inline int wc_AesSetKey(Aes*, const unsigned char*, unsigned int, const unsigned char*, int) { return -1; }
inline int wc_AesCbcEncrypt(Aes*, unsigned char*, const unsigned char*, unsigned int) { return -1; }
