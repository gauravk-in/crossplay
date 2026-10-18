#include "WalletAztec.h"

namespace wallet {

namespace {

// --- Bits ------------------------------------------------------------------

struct Bits {
  std::vector<bool> v;
  void put(const uint32_t value, const int count) {
    for (int i = count - 1; i >= 0; i--) v.push_back(((value >> i) & 1) != 0);
  }
  size_t size() const { return v.size(); }
};

// --- The character modes ---------------------------------------------------

enum Mode : uint8_t { Upper, Lower, Mixed, Digit, Punct };

// The value of `c` in `mode`, or -1 when the mode has no code for it.
int codeIn(const Mode mode, const unsigned char c) {
  switch (mode) {
    case Upper:
      if (c == ' ') return 1;
      if (c >= 'A' && c <= 'Z') return c - 'A' + 2;
      return -1;
    case Lower:
      if (c == ' ') return 1;
      if (c >= 'a' && c <= 'z') return c - 'a' + 2;
      return -1;
    case Digit:
      if (c == ' ') return 1;
      if (c >= '0' && c <= '9') return c - '0' + 2;
      if (c == ',') return 12;
      if (c == '.') return 13;
      return -1;
    case Mixed:
      if (c == ' ') return 1;
      if (c >= 1 && c <= 13) return c + 1;
      if (c >= 27 && c <= 31) return c - 12;
      switch (c) {
        case '@':
          return 20;
        case '\\':
          return 21;
        case '^':
          return 22;
        case '_':
          return 23;
        case '`':
          return 24;
        case '|':
          return 25;
        case '~':
          return 26;
        case 127:
          return 27;
        default:
          return -1;
      }
    case Punct: {
      if (c == '\r') return 1;
      static constexpr char kPunct[] = "!\"#$%&'()*+,-./:;<=>?[]{}";
      for (int i = 0; kPunct[i] != '\0'; i++) {
        if (static_cast<unsigned char>(kPunct[i]) == c) return i + 6;
      }
      return -1;
    }
  }
  return -1;
}

// The punctuation pairs with one code of their own: CR LF, ". ", ", ", ": ".
int pairCode(const unsigned char a, const unsigned char b) {
  if (a == '\r' && b == '\n') return 2;
  if (b != ' ') return -1;
  if (a == '.') return 3;
  if (a == ',') return 4;
  if (a == ':') return 5;
  return -1;
}

int widthOf(const Mode mode) { return mode == Digit ? 4 : 5; }

bool inAnyMode(const unsigned char c) {
  for (const Mode m : {Upper, Lower, Mixed, Digit, Punct}) {
    if (codeIn(m, c) >= 0) return true;
  }
  return false;
}

// Latches from `from` to `to`, never Punct: punctuation is shifted to one
// character at a time, which costs less than the two latches in and one out.
void latch(Bits& bits, Mode& from, const Mode to) {
  while (from != to) {
    switch (from) {
      case Upper:
        bits.put(to == Lower ? 28 : (to == Mixed ? 29 : 30), 5);
        from = to;
        break;
      case Lower:
        if (to == Upper) {
          bits.put(30, 5);  // D/L, then U/L from Digit
          from = Digit;
        } else {
          bits.put(to == Mixed ? 29 : 30, 5);
          from = to;
        }
        break;
      case Mixed:
        if (to == Lower) {
          bits.put(28, 5);
          from = Lower;
        } else {
          bits.put(29, 5);  // U/L, then on from Upper
          from = Upper;
        }
        break;
      case Digit:
        bits.put(14, 4);  // U/L
        from = Upper;
        break;
      case Punct:
        bits.put(31, 5);
        from = Upper;
        break;
    }
  }
}

// The data as a bit stream: text in whichever mode holds each character, and
// any byte no mode holds sent with Binary Shift. Greedy rather than optimal,
// which on a member number or a boarding pass costs a few bits at most.
Bits highLevel(const std::string& text) {
  Bits bits;
  bits.v.reserve(text.size() * 8);
  Mode mode = Upper;
  const size_t n = text.size();
  const auto at = [&](const size_t i) { return static_cast<unsigned char>(text[i]); };
  size_t i = 0;
  while (i < n) {
    const unsigned char c = at(i);
    const unsigned char next = i + 1 < n ? at(i + 1) : 0;
    if (i + 1 < n && codeIn(mode, c) < 0 && pairCode(c, next) >= 0) {
      bits.put(0, widthOf(mode));  // P/S
      bits.put(static_cast<uint32_t>(pairCode(c, next)), 5);
      i += 2;
      continue;
    }
    if (codeIn(mode, c) >= 0) {
      bits.put(static_cast<uint32_t>(codeIn(mode, c)), widthOf(mode));
      i++;
      continue;
    }
    if (!inAnyMode(c)) {
      // A few text characters between bytes ride along as bytes: leaving
      // Binary Shift and coming back costs more than they would save.
      size_t end = i;
      while (end < n && end - i < 2078) {
        if (!inAnyMode(at(end))) {
          end++;
          continue;
        }
        size_t text = end;
        while (text < n && text - end < 4 && inAnyMode(at(text))) text++;
        if (text - end >= 4 || text == n || text - i > 2078) break;
        end = text;
      }
      if (mode == Digit) latch(bits, mode, Upper);
      const size_t length = end - i;
      bits.put(31, 5);  // B/S
      if (length <= 31) {
        bits.put(static_cast<uint32_t>(length), 5);
      } else {
        bits.put(0, 5);
        bits.put(static_cast<uint32_t>(length - 31), 11);
      }
      for (; i < end; i++) bits.put(at(i), 8);
      continue;
    }
    // Shifted rather than latched to: a run of punctuation is rare in a code.
    if (codeIn(Punct, c) >= 0) {
      bits.put(0, widthOf(mode));  // P/S
      bits.put(static_cast<uint32_t>(codeIn(Punct, c)), 5);
      i++;
      continue;
    }
    const bool nextUpper = i + 1 < n && codeIn(Upper, next) >= 0 && next != ' ';
    if (codeIn(Upper, c) >= 0 && (mode == Lower || mode == Digit) && !nextUpper) {
      bits.put(mode == Lower ? 28 : 15, widthOf(mode));  // U/S
      bits.put(static_cast<uint32_t>(codeIn(Upper, c)), 5);
      i++;
      continue;
    }
    Mode to = Upper;
    if (codeIn(Digit, c) >= 0) {
      to = Digit;
    } else if (codeIn(Lower, c) >= 0) {
      to = Lower;
    } else if (codeIn(Mixed, c) >= 0) {
      to = Mixed;
    }
    latch(bits, mode, to);
  }
  return bits;
}

// --- Reed-Solomon ------------------------------------------------------------

struct Field {
  int size;
  std::vector<uint16_t> exp;
  std::vector<uint16_t> log;
  explicit Field(const int wordSize) : size(1 << wordSize), exp(static_cast<size_t>(1 << wordSize)), log(exp.size()) {
    int primitive = 0x13;
    if (wordSize == 6) primitive = 0x43;
    if (wordSize == 8) primitive = 0x12D;
    if (wordSize == 10) primitive = 0x409;
    if (wordSize == 12) primitive = 0x1069;
    int x = 1;
    for (int i = 0; i < size; i++) {
      exp[static_cast<size_t>(i)] = static_cast<uint16_t>(x);
      x <<= 1;
      if (x >= size) x ^= primitive;
    }
    for (int i = 0; i < size - 1; i++) log[exp[static_cast<size_t>(i)]] = static_cast<uint16_t>(i);
  }
  int mul(const int a, const int b) const {
    if (a == 0 || b == 0) return 0;
    return exp[static_cast<size_t>((log[static_cast<size_t>(a)] + log[static_cast<size_t>(b)]) % (size - 1))];
  }
};

// `totalWords` words: the first `dataWords` as given, then their check words,
// with the generator's roots starting at alpha^1.
void addCheckWords(std::vector<int>& words, const size_t dataWords, const int wordSize) {
  const Field field(wordSize);
  const size_t checks = words.size() - dataWords;
  std::vector<int> generator{1};
  generator.reserve(checks + 1);
  for (size_t i = 0; i < checks; i++) {
    const int root = field.exp[(i + 1) % static_cast<size_t>(field.size - 1)];
    generator.push_back(0);
    for (size_t j = generator.size() - 1; j > 0; j--) generator[j] ^= field.mul(generator[j - 1], root);
  }
  std::vector<int> remainder(checks, 0);
  for (size_t d = 0; d < dataWords; d++) {
    const int feedback = words[d] ^ remainder[0];
    for (size_t k = 0; k + 1 < checks; k++) remainder[k] = remainder[k + 1] ^ field.mul(feedback, generator[k + 1]);
    remainder[checks - 1] = field.mul(feedback, generator[checks]);
  }
  for (size_t k = 0; k < checks; k++) words[dataWords + k] = remainder[k];
}

// Words of `wordSize` bits with the check words added, as `totalBits` bits:
// the slack of a layer that is not a whole number of words goes first.
Bits checked(const Bits& data, const int totalBits, const int wordSize) {
  const size_t dataWords = data.size() / static_cast<size_t>(wordSize);
  std::vector<int> words(static_cast<size_t>(totalBits / wordSize), 0);
  for (size_t w = 0; w < dataWords; w++) {
    int value = 0;
    for (int b = 0; b < wordSize; b++) value = (value << 1) | (data.v[w * static_cast<size_t>(wordSize) + b] ? 1 : 0);
    words[w] = value;
  }
  addCheckWords(words, dataWords, wordSize);
  Bits out;
  out.v.reserve(static_cast<size_t>(totalBits));
  out.put(0, totalBits % wordSize);
  for (const int word : words) out.put(static_cast<uint32_t>(word), wordSize);
  return out;
}

// Words that are all ones or all zeros but the last bit would read as
// something else, so their last bit is forced the other way and the bit it
// replaced starts the next word. The tail is padded with ones.
Bits stuff(const Bits& bits, const int wordSize) {
  Bits out;
  out.v.reserve(bits.size() + bits.size() / static_cast<size_t>(wordSize) + static_cast<size_t>(wordSize));
  const int n = static_cast<int>(bits.size());
  const int mask = (1 << wordSize) - 2;
  for (int i = 0; i < n; i += wordSize) {
    int word = 0;
    for (int j = 0; j < wordSize; j++) {
      if (i + j >= n || bits.v[static_cast<size_t>(i + j)]) word |= 1 << (wordSize - 1 - j);
    }
    if ((word & mask) == mask) {
      out.put(static_cast<uint32_t>(word & mask), wordSize);
      i--;
    } else if ((word & mask) == 0) {
      out.put(static_cast<uint32_t>(word | 1), wordSize);
      i--;
    } else {
      out.put(static_cast<uint32_t>(word), wordSize);
    }
  }
  return out;
}

int wordSizeFor(const int layers) {
  if (layers <= 2) return 6;
  if (layers <= 8) return 8;
  if (layers <= 22) return 10;
  return 12;
}

int bitsInLayers(const int layers, const bool compact) { return ((compact ? 88 : 112) + 16 * layers) * layers; }

constexpr int kCheckPercent = 33;

// --- The symbol --------------------------------------------------------------

struct Grid {
  int size;
  std::vector<uint8_t>& bits;
  void set(const int x, const int y) {
    const int at = y * size + x;
    bits[static_cast<size_t>(at) >> 3] |= static_cast<uint8_t>(0x80 >> (at & 7));
  }
};

void bullsEye(Grid& grid, const int center, const int size) {
  for (int i = 0; i < size; i += 2) {
    for (int j = center - i; j <= center + i; j++) {
      grid.set(j, center - i);
      grid.set(j, center + i);
      grid.set(center - i, j);
      grid.set(center + i, j);
    }
  }
  // The orientation marks at three of the corners.
  grid.set(center - size, center - size);
  grid.set(center - size + 1, center - size);
  grid.set(center - size, center - size + 1);
  grid.set(center + size, center - size);
  grid.set(center + size, center - size + 1);
  grid.set(center + size, center + size - 1);
}

void modeMessage(Grid& grid, const bool compact, const Bits& mode) {
  const int center = grid.size / 2;
  if (compact) {
    for (int i = 0; i < 7; i++) {
      const int offset = center - 3 + i;
      if (mode.v[static_cast<size_t>(i)]) grid.set(offset, center - 5);
      if (mode.v[static_cast<size_t>(i + 7)]) grid.set(center + 5, offset);
      if (mode.v[static_cast<size_t>(20 - i)]) grid.set(offset, center + 5);
      if (mode.v[static_cast<size_t>(27 - i)]) grid.set(center - 5, offset);
    }
  } else {
    for (int i = 0; i < 10; i++) {
      const int offset = center - 5 + i + i / 5;
      if (mode.v[static_cast<size_t>(i)]) grid.set(offset, center - 7);
      if (mode.v[static_cast<size_t>(i + 10)]) grid.set(center + 7, offset);
      if (mode.v[static_cast<size_t>(29 - i)]) grid.set(offset, center + 7);
      if (mode.v[static_cast<size_t>(39 - i)]) grid.set(center - 7, offset);
    }
  }
}

}  // namespace

bool encodeAztec(const std::string& payload, AztecSymbol& out) {
  out = AztecSymbol{};
  if (payload.empty()) return false;
  const Bits data = highLevel(payload);
  const int dataBits = static_cast<int>(data.size());
  const int checkBits = dataBits * kCheckPercent / 100 + 11;

  // The smallest symbol that holds the data and its check words: compact
  // with 1 to 4 layers, then full-range from 4 layers up.
  bool compact = true;
  int layers = 0;
  int wordSize = 0;
  Bits stuffed;
  bool found = false;
  for (int i = 0; i <= 32 && !found; i++) {
    compact = i <= 3;
    layers = compact ? i + 1 : i;
    const int total = bitsInLayers(layers, compact);
    if (dataBits + checkBits > total) continue;
    if (wordSize != wordSizeFor(layers)) {
      wordSize = wordSizeFor(layers);
      stuffed = stuff(data, wordSize);
    }
    const int usable = total - total % wordSize;
    // A compact mode message has six bits for the word count.
    if (compact && static_cast<int>(stuffed.size()) > wordSize * 64) continue;
    if (static_cast<int>(stuffed.size()) + checkBits <= usable) found = true;
  }
  if (!found) return false;

  const int totalBits = bitsInLayers(layers, compact);
  const int dataWords = static_cast<int>(stuffed.size()) / wordSize;
  const Bits message = checked(stuffed, totalBits, wordSize);

  Bits mode;
  if (compact) {
    mode.put(static_cast<uint32_t>(layers - 1), 2);
    mode.put(static_cast<uint32_t>(dataWords - 1), 6);
    mode = checked(mode, 28, 4);
  } else {
    mode.put(static_cast<uint32_t>(layers - 1), 5);
    mode.put(static_cast<uint32_t>(dataWords - 1), 11);
    mode = checked(mode, 40, 4);
  }

  // Full-range symbols carry a reference grid every 16 modules out from the
  // centre, so the data's own coordinates skip those rows and columns.
  const int base = (compact ? 11 : 14) + layers * 4;
  std::vector<int> map(static_cast<size_t>(base));
  int size = base;
  if (compact) {
    for (int i = 0; i < base; i++) map[static_cast<size_t>(i)] = i;
  } else {
    size = base + 1 + 2 * ((base / 2 - 1) / 15);
    const int half = base / 2;
    const int center = size / 2;
    for (int i = 0; i < half; i++) {
      const int offset = i + i / 15;
      map[static_cast<size_t>(half - i - 1)] = center - offset - 1;
      map[static_cast<size_t>(half + i)] = center + offset + 1;
    }
  }

  out.size = size;
  out.compact = compact;
  out.layers = layers;
  out.bits.assign(static_cast<size_t>(size * size + 7) / 8, 0);
  Grid grid{size, out.bits};
  const auto bit = [&](const int at) { return message.v[static_cast<size_t>(at)]; };
  const auto m = [&](const int at) { return map[static_cast<size_t>(at)]; };
  // The layers spiral in from the outside, two modules deep, each side's
  // run of dominoes starting at the corner the previous side ended at.
  for (int i = 0, rowOffset = 0; i < layers; i++) {
    const int rowSize = (layers - i) * 4 + (compact ? 9 : 12);
    for (int j = 0; j < rowSize; j++) {
      const int column = j * 2;
      for (int k = 0; k < 2; k++) {
        if (bit(rowOffset + column + k)) grid.set(m(i * 2 + k), m(i * 2 + j));
        if (bit(rowOffset + rowSize * 2 + column + k)) grid.set(m(i * 2 + j), m(base - 1 - i * 2 - k));
        if (bit(rowOffset + rowSize * 4 + column + k)) grid.set(m(base - 1 - i * 2 - k), m(base - 1 - i * 2 - j));
        if (bit(rowOffset + rowSize * 6 + column + k)) grid.set(m(base - 1 - i * 2 - j), m(i * 2 + k));
      }
    }
    rowOffset += rowSize * 8;
  }
  modeMessage(grid, compact, mode);
  if (compact) {
    bullsEye(grid, size / 2, 5);
  } else {
    bullsEye(grid, size / 2, 7);
    for (int i = 0, j = 0; i < base / 2 - 1; i += 15, j += 16) {
      for (int k = (size / 2) & 1; k < size; k += 2) {
        grid.set(size / 2 - j, k);
        grid.set(size / 2 + j, k);
        grid.set(k, size / 2 - j);
        grid.set(k, size / 2 + j);
      }
    }
  }
  return true;
}

}  // namespace wallet
