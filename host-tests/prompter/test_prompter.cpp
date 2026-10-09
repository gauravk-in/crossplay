#include <cstdio>
#include <cstring>
#include <string>

#include "PrompterCore.h"

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// Every byte is 10px wide, so a 100px line holds ten characters.
int fixedWidth(void*, const char*, const size_t len) { return static_cast<int>(len) * 10; }

std::string lineText(const std::string& text, const prompter::Line& line) {
  return text.substr(line.start, line.length);
}

void testSettingsRoundTrip() {
  prompter::Settings s;
  s.size = 3;
  s.autoSeconds = 30;
  s.landscape = true;
  s.dark = true;
  s.swapTurner = true;
  s.turnerAddress = "aa:bb:cc:dd:ee:ff";
  s.turnerName = "Page Turner";
  s.turnerType = 1;
  s.script = "Keynote.txt";
  s.page = 7;
  const prompter::Settings r = prompter::parseSettings(prompter::formatSettings(s));
  check(r.size == 3 && r.autoSeconds == 30 && r.landscape && r.dark && r.swapTurner, "flags round-trip");
  check(r.turnerAddress == s.turnerAddress && r.turnerName == s.turnerName && r.turnerType == 1, "turner round-trips");
  check(r.script == "Keynote.txt" && r.page == 7, "place round-trips");

  const prompter::Settings bad = prompter::parseSettings("size=99\nauto=7\nscript=../etc.txt\npage=-3\n");
  check(bad.size == prompter::kSizeCount - 1, "size clamps");
  check(bad.autoSeconds == 0, "an unknown timer reads as off");
  check(bad.script.empty(), "a path is not a script name");
  check(bad.page == 0, "page clamps at zero");

  check(prompter::parseSettings("size=1\n").size == 3, "an old saved size keeps its cut");

  const prompter::Settings defaults = prompter::parseSettings("");
  check(defaults.size == prompter::kDefaultSize && defaults.autoSeconds == 0 && !defaults.landscape,
        "empty file is defaults");
}

void testAutoChoices() {
  check(prompter::nextAuto(0) == 5, "off steps up to 5 s");
  check(prompter::prevAuto(5) == 0, "5 s steps down to off");
  check(prompter::nextAuto(180) == 180, "top clamps");
  check(prompter::prevAuto(0) == 0, "bottom clamps");
  check(prompter::nextAuto(7) == 10, "between choices steps to the next one");
  check(prompter::autoLabel(0) == "OFF", "off label");
  check(prompter::autoLabel(45) == "45 S", "seconds label");
  check(prompter::autoLabel(120) == "2 MIN", "minutes label");
  check(prompter::autoLabel(90) == "1:30", "mixed label");
}

void testNames() {
  check(prompter::safeName("My Talk.txt") == "My Talk.txt", "a plain name is kept");
  check(prompter::safeName("../../x/Keynote") == "Keynote.txt", "a path is reduced to its name");
  check(prompter::safeName("  Q&A: round 2!  ") == "Q A round 2.txt", "odd characters become single spaces");
  check(prompter::safeName("") == "Script.txt", "empty gets a name");
  check(prompter::safeName(".hidden") == "hidden.txt", "no hidden files");
  check(prompter::safeName(std::string(200, 'a')).size() <= prompter::kMaxNameBytes, "long names are capped");
  check(prompter::isScriptName("Talk.txt") && !prompter::isScriptName("a/b.txt"), "isScriptName");
  check(prompter::isScriptName("My Speech (v2).TXT"), "a USB-copied name is a script");
  check(!prompter::isScriptName(".txt") && !prompter::isScriptName("._Talk.txt"), "no hidden files");
  check(!prompter::isScriptName("notes.md"), "only .txt");
  check(prompter::displayName("Talk.txt") == "Talk", "display drops .txt");
}

void testClean() {
  const std::string raw = "\xEF\xBB\xBFHello  \r\nworld\tthere\r\r\n\n\n\nNext\x01 para   \n";
  check(prompter::cleanScript(raw) == "Hello\nworld there\n\nNext para", "clean normalises endings, tabs, blanks");
  check(prompter::cleanScript("") == "", "empty stays empty");
  check(prompter::cleanScript("Caf\xC3\xA9 na\xC3\xAFve Stra\xC3\x9F"
                              "e") == "Cafe naive Strasse",
        "accents fold");
  check(prompter::cleanScript("a\xC2\xA0"
                              "b") == "a b",
        "no-break space folds");
  check(prompter::cleanScript("\xE2\x80\x9CHi\xE2\x80\x9D") == "\xE2\x80\x9CHi\xE2\x80\x9D", "curly quotes stay");
}

void testPaginate() {
  const std::string text = prompter::cleanScript("one two three four five six\n\nseven eight\nnine");
  const prompter::Paged p = prompter::paginate(text, 100, 3, &fixedWidth, nullptr);
  // "one two" (7) + " three" = 13 > 10.
  check(p.lines.size() == 7, "line count");
  check(lineText(text, p.lines[0]) == "one two", "first line wraps before overflowing");
  check(lineText(text, p.lines[1]) == "three four", "exactly ten fits");
  check(lineText(text, p.lines[2]) == "five six", "third line");
  check(p.lines[3].length == 0, "paragraph gap is kept");
  check(p.pageCount() == 2, "two pages");
  check(p.firstLine(1) == 4, "a page never starts on the paragraph gap");
  check(lineText(text, p.lines[p.firstLine(1)]) == "seven", "second page starts with words");
  check(p.endLine(1) == 7, "last page ends at the last line");

  const prompter::Paged wide = prompter::paginate("abcdefghijklmnopqrstuvwxyz", 100, 5, &fixedWidth, nullptr);
  check(wide.lines.size() == 3, "a long word is broken");
  check(wide.lines[0].length == 10 && wide.lines[2].length == 6, "broken at the width");

  const prompter::Paged utf =
      prompter::paginate("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 40, 5, &fixedWidth, nullptr);
  bool whole = true;
  for (const auto& l : utf.lines) whole = whole && l.length % 2 == 0;
  check(whole, "a broken word never splits a UTF-8 character");

  const prompter::Paged empty = prompter::paginate("", 100, 5, &fixedWidth, nullptr);
  check(empty.pageCount() == 1 && empty.lines.empty(), "an empty script is one blank page");

  const prompter::Paged tiny = prompter::paginate("abc", 1, 5, &fixedWidth, nullptr);
  check(tiny.lines.size() == 3, "a glyph wider than the line still advances");

  check(prompter::pageOfOffset(p, 0) == 0, "offset 0 is page 0");
  check(prompter::pageOfOffset(p, static_cast<uint32_t>(text.find("nine"))) == 1, "offset finds its page");
}

void testTimer() {
  prompter::PageTimer t;
  t.start(0);
  check(!t.running(), "an off timer does not run");
  t.setSeconds(10);
  t.start(1000);
  check(t.running() && t.secondsLeft(1000) == 10, "starts full");
  check(t.secondsLeft(1500) == 10, "a started second still counts");
  check(t.secondsLeft(2000) == 9, "counts down");
  check(!t.due(10999), "not early");
  check(t.due(11000), "due on time");
  check(!t.due(11001), "due once");
  check(t.secondsLeft(11000) == 10, "counts the next page from the turn");
  t.restart(15000);
  check(!t.due(24999) && t.due(25000), "a hand turn restarts the count");
  check(t.permille(25000) == 0 && t.permille(30000) == 500, "progress");
  t.stop();
  check(!t.due(999999), "stopped never turns");
  check(t.permille(30000) == 0, "stopped shows no progress");
}

void testReportMap() {
  // A typical page turner: report 1 a keyboard, report 2 consumer control.
  const uint8_t map[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
                         0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
                         0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0, 0x05,
                         0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF,
                         0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x03,
                         0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x81, 0x02, 0xC0, 0xC0};
  const prompter::ReportKinds kinds = prompter::parseReportMap(map, sizeof(map));
  check(kinds.kindOf(1) == prompter::ReportKind::Keyboard, "report 1 is a keyboard");
  check(kinds.kindOf(2) == prompter::ReportKind::Consumer, "report 2 is consumer control");
  check(kinds.kindOf(3) == prompter::ReportKind::Other, "report 3 is a mouse");
  check(kinds.kindOf(9) == prompter::ReportKind::Unknown, "an undeclared report is unknown");
  const uint8_t truncated[] = {0x05, 0x07, 0x85};
  check(prompter::parseReportMap(truncated, sizeof(truncated)).entries.empty(), "a cut-off map is survived");
  check(prompter::parseReportMap(nullptr, 4).entries.empty(), "no map");
}

void testShutterRemote() {
  using prompter::ReportKind;
  using prompter::Turn;
  // A camera shutter remote: report 2 is two one-bit consumer keys and padding.
  const uint8_t map[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
                         0x95, 0x02, 0x09, 0xE9, 0x09, 0xEA, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0xC0};
  const prompter::ReportKinds kinds = prompter::parseReportMap(map, sizeof(map));
  const prompter::ReportKinds::Entry* e = kinds.find(2);
  check(e != nullptr && e->kind == ReportKind::Consumer, "report 2 is consumer control");
  check(e != nullptr && e->bits.count == 2, "two key bits, padding left out");
  check(e != nullptr && e->bits.usage[0] == 0xE9 && e->bits.offset[1] == 1, "bits keep their usage and place");

  prompter::TurnDecoder d;
  const uint8_t up[1] = {0x01};
  const uint8_t down[1] = {0x02};
  const uint8_t none[1] = {0x00};
  check(d.feed(ReportKind::Consumer, up, 1, &e->bits) == Turn::Back, "the shutter's volume up");
  check(d.feed(ReportKind::Consumer, up, 1, &e->bits) == Turn::None, "held is one turn");
  check(d.feed(ReportKind::Consumer, none, 1, &e->bits) == Turn::None, "release");
  check(d.feed(ReportKind::Consumer, down, 1, &e->bits) == Turn::Next, "volume down");

  // The same keys declared as a usage range.
  const uint8_t ranged[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95,
                            0x02, 0x19, 0xE9, 0x29, 0xEA, 0x81, 0x02, 0x95, 0x06, 0x81, 0x01, 0xC0};
  const prompter::ReportKinds rk = prompter::parseReportMap(ranged, sizeof(ranged));
  const prompter::ReportKinds::Entry* r = rk.find(0);
  check(r != nullptr && r->bits.count == 2 && r->bits.usage[1] == 0xEA, "a usage range names each bit");
}

void testDecoder() {
  using prompter::ReportKind;
  using prompter::Turn;
  prompter::TurnDecoder d;
  const uint8_t right[8] = {0, 0, 0x4F, 0, 0, 0, 0, 0};
  const uint8_t none[8] = {};
  const uint8_t left[8] = {0, 0, 0x50, 0, 0, 0, 0, 0};
  check(d.feed(ReportKind::Keyboard, right, 8) == Turn::Next, "right arrow is next");
  check(d.feed(ReportKind::Keyboard, right, 8) == Turn::None, "held is one turn");
  check(d.feed(ReportKind::Keyboard, none, 8) == Turn::None, "release turns nothing");
  check(d.feed(ReportKind::Keyboard, left, 8) == Turn::Back, "left arrow is back");
  check(d.feed(ReportKind::Unknown, none, 8) == Turn::None, "unknown eight bytes reads as a keyboard");
  check(d.feed(ReportKind::Unknown, right, 8) == Turn::Next, "and decodes");

  const uint8_t volDown[2] = {0xEA, 0x00};
  const uint8_t volUp[2] = {0xE9, 0x00};
  const uint8_t zero[2] = {};
  check(d.feed(ReportKind::Consumer, volDown, 2) == Turn::Next, "volume down is next");
  check(d.feed(ReportKind::Consumer, volDown, 2) == Turn::None, "a repeated report is one turn");
  check(d.feed(ReportKind::Consumer, zero, 2) == Turn::None, "release");
  check(d.feed(ReportKind::Unknown, volUp, 2) == Turn::Back, "unknown two bytes reads as consumer");
  const uint8_t play[2] = {0xCD, 0x00};
  check(d.feed(ReportKind::Consumer, play, 2) == Turn::Toggle, "play/pause toggles the timer");
  check(d.feed(ReportKind::Other, right, 8) == Turn::None, "a mouse report turns nothing");

  const uint8_t shortKey[3] = {0, 0x4E, 0};
  d.reset();
  check(d.feed(ReportKind::Keyboard, shortKey, 3) == Turn::Next, "a short keyboard report skips no reserved byte");

  const uint8_t f13[8] = {0, 0, 0x68, 0, 0, 0, 0, 0};
  d.reset();
  check(d.feed(ReportKind::Keyboard, f13, 8) == Turn::None && d.unknownPress(), "an unmapped key is reported");
  check(d.feed(ReportKind::Keyboard, f13, 8) == Turn::None && !d.unknownPress(), "but only when pressed");

  check(prompter::applySwap(Turn::Next, true) == Turn::Back, "swap");
  check(prompter::applySwap(Turn::Toggle, true) == Turn::Toggle, "swap leaves toggle");
  check(prompter::applySwap(Turn::Next, false) == Turn::Next, "no swap");
}

}  // namespace

int main() {
  testSettingsRoundTrip();
  testAutoChoices();
  testNames();
  testClean();
  testPaginate();
  testTimer();
  testReportMap();
  testShutterRemote();
  testDecoder();
  if (failures != 0) {
    std::printf("prompter: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("prompter: all passed\n");
  return 0;
}
