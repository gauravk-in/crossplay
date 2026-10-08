#include "PrompterCore.h"

#include <cstdio>
#include <cstdlib>

namespace prompter {

// --- Settings ----------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

int clampInt(const int v, const int lo, const int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool truthy(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on"; }

// One line of a key=value file, kept to printable text so a hand-edited file
// cannot smuggle a newline into the next save.
std::string printable(const std::string& v, const size_t cap) {
  std::string out;
  for (const char c : v) {
    if (static_cast<unsigned char>(c) < 0x20) continue;
    out += c;
    if (out.size() >= cap) break;
  }
  return out;
}

bool knownAuto(const int seconds) {
  for (const int c : kAutoChoices) {
    if (c == seconds) return true;
  }
  return false;
}

}  // namespace

Settings parseSettings(const std::string& text) {
  Settings s;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find('\n', i);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(i, end - i);
    i = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "text_size") {
      s.size = clampInt(std::atoi(value.c_str()), 0, kSizeCount - 1);
    } else if (key == "size") {
      // Saved before the two smaller sizes went in front of the list.
      s.size = clampInt(std::atoi(value.c_str()) + 2, 0, kSizeCount - 1);
    } else if (key == "auto") {
      const int seconds = std::atoi(value.c_str());
      s.autoSeconds = knownAuto(seconds) ? seconds : 0;
    } else if (key == "landscape") {
      s.landscape = truthy(value);
    } else if (key == "dark") {
      s.dark = truthy(value);
    } else if (key == "swap_turner") {
      s.swapTurner = truthy(value);
    } else if (key == "turner") {
      s.turnerAddress = printable(value, 17);
    } else if (key == "turner_type") {
      s.turnerType = clampInt(std::atoi(value.c_str()), 0, 3);
    } else if (key == "turner_name") {
      s.turnerName = printable(value, 40);
    } else if (key == "script") {
      s.script = isScriptName(value) ? value : std::string();
    } else if (key == "page") {
      s.page = clampInt(std::atoi(value.c_str()), 0, 100000);
    }
  }
  return s;
}

std::string formatSettings(const Settings& s) {
  char head[128];
  std::snprintf(head, sizeof(head),
                "text_size=%d\nauto=%d\nlandscape=%d\ndark=%d\nswap_turner=%d\npage=%d\nturner_type=%d\n", s.size,
                s.autoSeconds, s.landscape ? 1 : 0, s.dark ? 1 : 0, s.swapTurner ? 1 : 0, s.page, s.turnerType);
  std::string out = head;
  out += "turner=" + printable(s.turnerAddress, 17) + "\n";
  out += "turner_name=" + printable(s.turnerName, 40) + "\n";
  out += "script=" + (isScriptName(s.script) ? s.script : std::string()) + "\n";
  return out;
}

int nextAuto(const int seconds) {
  for (const int c : kAutoChoices) {
    if (c > seconds) return c;
  }
  return kAutoChoices[kAutoChoiceCount - 1];
}

int prevAuto(const int seconds) {
  for (int i = kAutoChoiceCount - 1; i >= 0; --i) {
    if (kAutoChoices[i] < seconds) return kAutoChoices[i];
  }
  return 0;
}

std::string autoLabel(const int seconds) {
  if (seconds <= 0) return "OFF";
  char text[16];
  if (seconds < 60) {
    std::snprintf(text, sizeof(text), "%d S", seconds);
  } else if (seconds % 60 == 0) {
    std::snprintf(text, sizeof(text), "%d MIN", seconds / 60);
  } else {
    std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
  }
  return text;
}

// --- Scripts -----------------------------------------------------------------

std::string safeName(const std::string& wanted) {
  std::string stem = wanted;
  const size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos) stem = stem.substr(slash + 1);
  if (stem.size() >= 4 &&
      (stem.compare(stem.size() - 4, 4, ".txt") == 0 || stem.compare(stem.size() - 4, 4, ".TXT") == 0)) {
    stem.resize(stem.size() - 4);
  }
  std::string out;
  bool lastSpace = true;  // drops leading space and collapses runs
  for (const char c : stem) {
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    if (ok) {
      out += c;
      lastSpace = false;
    } else if (!lastSpace) {
      out += ' ';
      lastSpace = true;
    }
    if (out.size() >= kMaxNameBytes - 4) break;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  while (!out.empty() && out.front() == '.') out.erase(out.begin());
  if (out.empty()) out = "Script";
  return out + ".txt";
}

bool isScriptName(const std::string& name) {
  if (name.size() < 5 || name.size() > 100 || name[0] == '.') return false;
  for (const char c : name) {
    if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20) return false;
  }
  const std::string ext = name.substr(name.size() - 4);
  return ext == ".txt" || ext == ".TXT" || ext == ".Txt";
}

std::string displayName(const std::string& name) {
  if (name.size() > 4 && name[name.size() - 4] == '.') return name.substr(0, name.size() - 4);
  return name;
}

namespace {

// U+00C0..U+00FF without their accents; '\0' where a letter has no plain form
// worth guessing (the multiplication and division signs).
constexpr char kFold[64 + 1] = "AAAAAAACEEEEIIIIDNOOOOO\0OUUUUYTsaaaaaaaceeeeiiiidnooooo\0ouuuuyty";

}  // namespace

std::string cleanScript(const std::string& raw) {
  std::string text;
  text.reserve(raw.size());
  size_t i = 0;
  if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB &&
      static_cast<unsigned char>(raw[2]) == 0xBF) {
    i = 3;
  }
  for (; i < raw.size(); ++i) {
    const char c = raw[i];
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == 0xC2 && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xA0) {
      text += ' ';  // no-break space
      ++i;
      continue;
    }
    if (u == 0xC3 && i + 1 < raw.size()) {
      const unsigned char next = static_cast<unsigned char>(raw[i + 1]);
      if (next >= 0x80 && next <= 0xBF && kFold[next - 0x80] != '\0') {
        text += kFold[next - 0x80];
        if (next == 0x9F) text += 's';  // sharp s reads as "ss"
        ++i;
        continue;
      }
    }
    if (c == '\r') {
      text += '\n';
      if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
    } else if (c == '\t') {
      text += ' ';
    } else if (static_cast<unsigned char>(c) < 0x20 && c != '\n') {
      continue;
    } else {
      text += c;
    }
  }

  // Trim each line's trailing space and keep at most one blank line in a row.
  std::string out;
  out.reserve(text.size());
  int newlines = 0;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    size_t e = end;
    while (e > start && text[e - 1] == ' ') --e;
    if (e == start) {
      ++newlines;
    } else {
      if (!out.empty()) out.append(newlines >= 2 ? "\n\n" : "\n");
      out.append(text, start, e - start);
      newlines = 1;
    }
    if (end == text.size()) break;
    start = end + 1;
  }
  return out;
}

// --- Pages -------------------------------------------------------------------

uint32_t Paged::firstLine(const int page) const {
  if (page < 0 || page >= pageCount()) return 0;
  return pages[static_cast<size_t>(page)];
}

uint32_t Paged::endLine(const int page) const {
  if (page < 0 || page >= pageCount()) return 0;
  if (page + 1 < pageCount()) return pages[static_cast<size_t>(page) + 1];
  return static_cast<uint32_t>(lines.size());
}

namespace {

// Bytes in the UTF-8 sequence starting with `lead`.
size_t utf8Length(const unsigned char lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

struct Wrapper {
  const std::string& text;
  int width;
  MeasureFn measure;
  void* context;
  std::vector<Line>& lines;

  int widthOf(const size_t start, const size_t len) const { return measure(context, text.data() + start, len); }

  void emit(const size_t start, const size_t len) {
    Line line;
    line.start = static_cast<uint32_t>(start);
    line.length = static_cast<uint16_t>(len > 0xFFFF ? 0xFFFF : len);
    lines.push_back(line);
  }

  // A word too wide for any line, broken between characters.
  void breakWord(size_t start, const size_t end) {
    while (start < end) {
      size_t cut = start;
      size_t next = start + utf8Length(static_cast<unsigned char>(text[start]));
      if (next > end) next = end;
      while (next <= end && widthOf(start, next - start) <= width) {
        cut = next;
        if (next == end) break;
        next += utf8Length(static_cast<unsigned char>(text[next]));
        if (next > end) next = end;
      }
      // Always take at least one character, or a glyph wider than the line
      // would loop here forever.
      if (cut == start) cut = start + utf8Length(static_cast<unsigned char>(text[start]));
      if (cut > end) cut = end;
      emit(start, cut - start);
      start = cut;
    }
  }

  // One paragraph: [start, end) with no newline inside.
  void paragraph(const size_t start, const size_t end) {
    size_t lineStart = std::string::npos;
    size_t lineEnd = 0;
    size_t i = start;
    while (i < end) {
      while (i < end && text[i] == ' ') ++i;
      if (i >= end) break;
      size_t wordEnd = i;
      while (wordEnd < end && text[wordEnd] != ' ') ++wordEnd;

      if (lineStart == std::string::npos) {
        if (widthOf(i, wordEnd - i) > width) {
          breakWord(i, wordEnd);
        } else {
          lineStart = i;
          lineEnd = wordEnd;
        }
      } else if (widthOf(lineStart, wordEnd - lineStart) <= width) {
        lineEnd = wordEnd;
      } else {
        emit(lineStart, lineEnd - lineStart);
        lineStart = std::string::npos;
        if (widthOf(i, wordEnd - i) > width) {
          breakWord(i, wordEnd);
        } else {
          lineStart = i;
          lineEnd = wordEnd;
        }
      }
      i = wordEnd;
    }
    if (lineStart != std::string::npos) emit(lineStart, lineEnd - lineStart);
  }
};

}  // namespace

Paged paginate(const std::string& text, const int width, const int linesPerPage, const MeasureFn measure,
               void* context) {
  Paged out;
  out.linesPerPage = linesPerPage < 1 ? 1 : linesPerPage;
  // About eight words a line at the smallest size; a guess, so the vector
  // grows a few times at most rather than once per line.
  out.lines.reserve(text.size() / 40 + 8);
  Wrapper wrap{text, width < 1 ? 1 : width, measure, context, out.lines};

  size_t start = 0;
  bool first = true;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    if (end == start) {
      // A blank line, as a zero-length line, between two paragraphs only.
      if (!first) wrap.emit(start, 0);
    } else {
      wrap.paragraph(start, end);
      first = false;
    }
    if (end == text.size()) break;
    start = end + 1;
  }
  while (!out.lines.empty() && out.lines.back().length == 0) out.lines.pop_back();

  out.pages.reserve(out.lines.size() / static_cast<size_t>(out.linesPerPage) + 1);
  uint32_t i = 0;
  const uint32_t count = static_cast<uint32_t>(out.lines.size());
  while (i < count) {
    while (i < count && out.lines[i].length == 0) ++i;
    if (i >= count) break;
    out.pages.push_back(i);
    i += static_cast<uint32_t>(out.linesPerPage);
  }
  if (out.pages.empty()) out.pages.push_back(0);
  return out;
}

int pageOfOffset(const Paged& paged, const uint32_t offset) {
  int page = 0;
  for (int p = 0; p < paged.pageCount(); ++p) {
    const uint32_t line = paged.firstLine(p);
    if (line < paged.lines.size() && paged.lines[line].start <= offset) page = p;
  }
  return page;
}

// --- The page timer ----------------------------------------------------------

void PageTimer::start(const uint32_t now) {
  if (seconds_ <= 0) {
    running_ = false;
    return;
  }
  running_ = true;
  pageStart_ = now;
}

void PageTimer::restart(const uint32_t now) { pageStart_ = now; }

bool PageTimer::due(const uint32_t now) {
  if (!running_ || seconds_ <= 0) return false;
  if (now - pageStart_ < static_cast<uint32_t>(seconds_) * 1000u) return false;
  pageStart_ = now;
  return true;
}

int PageTimer::secondsLeft(const uint32_t now) const {
  if (seconds_ <= 0) return 0;
  if (!running_) return seconds_;
  const uint32_t gone = now - pageStart_;
  const uint32_t total = static_cast<uint32_t>(seconds_) * 1000u;
  if (gone >= total) return 0;
  return static_cast<int>((total - gone + 999u) / 1000u);
}

int PageTimer::permille(const uint32_t now) const {
  if (seconds_ <= 0 || !running_) return 0;
  const uint32_t gone = now - pageStart_;
  const uint32_t total = static_cast<uint32_t>(seconds_) * 1000u;
  if (gone >= total) return 1000;
  return static_cast<int>((static_cast<uint64_t>(gone) * 1000u) / total);
}

// --- Page turners ------------------------------------------------------------

ReportKind ReportKinds::kindOf(const uint8_t id) const {
  for (const Entry& e : entries) {
    if (e.id == id) return e.kind;
  }
  return ReportKind::Unknown;
}

namespace {

ReportKind kindForPage(const uint32_t page) {
  if (page == 0x07) return ReportKind::Keyboard;
  if (page == 0x0C) return ReportKind::Consumer;
  return ReportKind::Other;
}

// Keyboard or consumer beats anything else a report also carries.
ReportKind stronger(const ReportKind a, const ReportKind b) {
  if (a == ReportKind::Keyboard || a == ReportKind::Consumer) return a;
  if (b == ReportKind::Keyboard || b == ReportKind::Consumer) return b;
  if (a == ReportKind::Other || b == ReportKind::Other) return ReportKind::Other;
  return ReportKind::Unknown;
}

}  // namespace

ReportKinds parseReportMap(const uint8_t* map, const size_t len) {
  ReportKinds out;
  if (map == nullptr) return out;
  uint32_t usagePage = 0;
  uint32_t localPage = 0;  // from an extended (32-bit) Usage, which names its own page
  uint8_t reportId = 0;
  uint32_t pageStack[4] = {};
  uint8_t idStack[4] = {};
  int depth = 0;

  size_t i = 0;
  while (i < len) {
    const uint8_t prefix = map[i];
    if (prefix == 0xFE) {
      // A long item: skip its declared size and the two header bytes.
      if (i + 1 >= len) break;
      i += 3 + map[i + 1];
      continue;
    }
    size_t size = prefix & 0x03;
    if (size == 3) size = 4;
    const uint8_t type = (prefix >> 2) & 0x03;
    const uint8_t tag = prefix >> 4;
    if (i + 1 + size > len) break;
    uint32_t value = 0;
    for (size_t b = 0; b < size; ++b) value |= static_cast<uint32_t>(map[i + 1 + b]) << (8 * b);

    if (type == 1) {  // global
      if (tag == 0x0) {
        usagePage = value;
      } else if (tag == 0x8) {
        reportId = static_cast<uint8_t>(value);
      } else if (tag == 0xA && depth < 4) {
        pageStack[depth] = usagePage;
        idStack[depth] = reportId;
        ++depth;
      } else if (tag == 0xB && depth > 0) {
        --depth;
        usagePage = pageStack[depth];
        reportId = idStack[depth];
      }
    } else if (type == 2) {  // local
      if (tag == 0x0 && size == 4) localPage = value >> 16;
    } else if (type == 0) {  // main
      if (tag == 0x8) {      // Input
        const ReportKind kind = kindForPage(localPage != 0 ? localPage : usagePage);
        bool found = false;
        for (ReportKinds::Entry& e : out.entries) {
          if (e.id == reportId) {
            e.kind = stronger(e.kind, kind);
            found = true;
          }
        }
        if (!found) out.entries.push_back({reportId, kind});
      }
      // Every main item ends the local state.
      localPage = 0;
    }
    i += 1 + size;
  }
  return out;
}

Turn keyboardTurn(const uint8_t usage) {
  switch (usage) {
    case 0x4F:  // Right arrow
    case 0x51:  // Down arrow
    case 0x4E:  // Page Down
    case 0x2C:  // Space
    case 0x28:  // Enter
    case 0x58:  // Keypad Enter
      return Turn::Next;
    case 0x50:  // Left arrow
    case 0x52:  // Up arrow
    case 0x4B:  // Page Up
    case 0x2A:  // Backspace
      return Turn::Back;
    case 0x3E:  // F5, a presenter's start
    case 0x29:  // Escape, a presenter's stop
    case 0x05:  // B, a presenter's blank screen
      return Turn::Toggle;
    default:
      return Turn::None;
  }
}

Turn consumerTurn(const uint16_t usage) {
  switch (usage) {
    case 0xEA:  // Volume Down: the Kindle convention, next page
    case 0xB5:  // Scan Next Track
      return Turn::Next;
    case 0xE9:  // Volume Up
    case 0xB6:  // Scan Previous Track
      return Turn::Back;
    case 0xCD:  // Play/Pause
    case 0xB0:  // Play
    case 0xB1:  // Pause
      return Turn::Toggle;
    default:
      return Turn::None;
  }
}

Turn applySwap(const Turn turn, const bool swap) {
  if (!swap) return turn;
  if (turn == Turn::Next) return Turn::Back;
  if (turn == Turn::Back) return Turn::Next;
  return turn;
}

void TurnDecoder::reset() {
  keyCount_ = 0;
  consumer_ = 0;
}

Turn TurnDecoder::feed(ReportKind kind, const uint8_t* data, const size_t len) {
  if (data == nullptr || len == 0) return Turn::None;
  if (kind == ReportKind::Unknown) kind = len >= 8 ? ReportKind::Keyboard : len <= 2 ? ReportKind::Consumer : kind;

  if (kind == ReportKind::Keyboard) {
    // A boot keyboard report is modifiers, a reserved byte, six keys; a short
    // one leaves the reserved byte out.
    const size_t first = len >= 8 ? 2 : 1;
    uint8_t now[8] = {};
    size_t count = 0;
    for (size_t i = first; i < len && count < 8; ++i) {
      if (data[i] > 0x03) now[count++] = data[i];  // 1..3 are rollover and error codes
    }
    Turn turn = Turn::None;
    for (size_t i = 0; i < count && turn == Turn::None; ++i) {
      bool held = false;
      for (size_t j = 0; j < keyCount_; ++j) held = held || keys_[j] == now[i];
      if (!held) turn = keyboardTurn(now[i]);
    }
    for (size_t i = 0; i < count; ++i) keys_[i] = now[i];
    keyCount_ = count;
    return turn;
  }

  if (kind == ReportKind::Consumer) {
    const uint16_t usage = len >= 2 ? static_cast<uint16_t>(data[0] | (data[1] << 8)) : data[0];
    const uint16_t before = consumer_;
    consumer_ = usage;
    if (usage == 0 || usage == before) return Turn::None;
    return consumerTurn(usage);
  }
  return Turn::None;
}

}  // namespace prompter
