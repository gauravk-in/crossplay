// Prints "payload-hex<TAB>size<TAB>modules<TAB>layers" for tools_local/wallet/aztec_roundtrip.sh:
// a few real-looking payloads, then seeded random ones that reach every
// compact size and most full-range ones.
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "WalletAztec.h"
using namespace wallet;
int main() {
  std::string boarding = "M1DESMARAIS/LUC       EABC123 YULFRAAC 0834 226F001A0025 100";
  std::string bytes;
  for (int i = 0; i < 256; i++) bytes.push_back(static_cast<char>(i));
  std::string longText;
  for (int i = 0; i < 40; i++) longText += "Member 00042-XY, Lidl Plus. ";
  std::vector<std::string> cases = {
      "A",
      "1234567890",
      "Hello, World!",
      "https://example.com/pass?id=42&x=y",
      "MiXeD cAsE wItH lOtS oF sHiFtS",
      "tab\there\r\nline two: done. ok",
      "@\\^_`|~ and {braces} [brackets]",
      "caf\xc3\xa9 \xe2\x82\xac 5,00",
      boarding,
      bytes,
      longText,
      std::string(1200, 'Z'),
      std::string(1200, '\xff'),
  };
  const char* alphabets[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZ ", "abcdefghijklmnopqrstuvwxyz ", "0123456789., ",
                             "!@#$%^&*()[]{}<>/\\|~`'\"\r\n\t:;-_+=?"};
  std::mt19937 rng(7);
  for (int t = 0; t < 400; t++) {
    const int length = 1 + static_cast<int>(rng() % (t < 200 ? 120 : 1200));
    const int style = static_cast<int>(rng() % 6);
    std::string s;
    for (int i = 0; i < length; i++) {
      if (style == 5) {
        s.push_back(static_cast<char>(rng() % 256));
      } else {
        const char* a = alphabets[style == 4 ? rng() % 4 : style];
        s.push_back(a[rng() % std::strlen(a)]);
      }
    }
    cases.push_back(s);
  }
  for (const std::string& c : cases) {
    AztecSymbol s;
    for (unsigned char ch : c) std::printf("%02x", ch);
    if (!encodeAztec(c, s)) {
      std::printf("\tFAIL\n");
      continue;
    }
    std::printf("\t%d\t", s.size);
    for (int y = 0; y < s.size; y++)
      for (int x = 0; x < s.size; x++) std::putchar(s.dark(x, y) ? '1' : '0');
    std::printf("\t%d\n", s.layers);
  }
}
