#pragma once

// Prompter's rules, with no hardware: the settings file, a script cut into
// pages, the page timer, and what a Bluetooth page turner's key means.
//
// Everything here runs on the host (host-tests/prompter), so the paging that a
// person reads from across a room is tested against a measuring function rather
// than eyeballed on a panel.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace prompter {

// --- Settings ----------------------------------------------------------------

// The text sizes the reader offers, smallest first. The index is what is saved.
constexpr int kSizeCount = 6;
// The default, 26 px.
constexpr int kDefaultSize = 3;
// Seconds a page stays up before the timer turns it; 0 is off.
constexpr int kAutoChoices[] = {0, 5, 10, 15, 20, 30, 45, 60, 90, 120, 180};
constexpr int kAutoChoiceCount = static_cast<int>(sizeof(kAutoChoices) / sizeof(kAutoChoices[0]));
constexpr size_t kMaxSettingsBytes = 1024;
// A script is read whole into memory; this is about two hours of speech.
constexpr size_t kMaxScriptBytes = 128 * 1024;
constexpr size_t kMaxNameBytes = 48;

struct Settings {
  int size = kDefaultSize;
  int autoSeconds = 0;
  bool landscape = false;
  // White words on black, the way a studio prompter reads.
  bool dark = false;
  // The page turner's two buttons, the other way round.
  bool swapTurner = false;
  // The paired page turner, empty when there is none.
  std::string turnerAddress;
  // BLE address type: 0 public, 1 random. Most remotes are random.
  int turnerType = 0;
  std::string turnerName;
  // The script opened last, by file name.
  std::string script;
  // Where it was left, so a rehearsal resumes on the right page.
  int page = 0;
};

Settings parseSettings(const std::string& text);
std::string formatSettings(const Settings& settings);

// The next and previous timer choice, clamped at both ends.
int nextAuto(int seconds);
int prevAuto(int seconds);
// "OFF", "5 S", "1 MIN", "1:30".
std::string autoLabel(int seconds);

// --- Scripts -----------------------------------------------------------------

// A file name a phone can send and the card can keep: letters, digits, space,
// dash, underscore and dot, ending in .txt, never empty, never a path.
std::string safeName(const std::string& wanted);
// Any .txt file name the card can hold in the script folder, including ones
// copied over USB that safeName() would have spelled differently.
bool isScriptName(const std::string& name);
// The name without .txt, for a list row.
std::string displayName(const std::string& name);

// Line endings and tabs made plain, a byte-order mark dropped, runs of blank
// lines collapsed to one, and trailing space trimmed from every line. Latin-1
// accents fold to their bare letters and a no-break space to a space: the cuts
// carry ASCII and the typographic punctuation, and a glyph a cut lacks draws as
// nothing at all.
std::string cleanScript(const std::string& raw);

// --- Pages -------------------------------------------------------------------

// Width in pixels of `len` bytes of UTF-8 at `text`.
using MeasureFn = int (*)(void* context, const char* text, size_t len);

struct Line {
  uint32_t start = 0;
  uint16_t length = 0;
};

struct Paged {
  std::vector<Line> lines;
  // Index into `lines` of each page's first line.
  std::vector<uint32_t> pages;
  int linesPerPage = 1;

  int pageCount() const { return static_cast<int>(pages.size()); }
  // [first, last) lines of `page`.
  uint32_t firstLine(int page) const;
  uint32_t endLine(int page) const;
};

// Greedy word wrap into `width`, then `linesPerPage` lines a page. A blank line
// between paragraphs is kept, except at the top of a page, where it would only
// push the words down. A word wider than the line is broken between characters.
Paged paginate(const std::string& text, int width, int linesPerPage, MeasureFn measure, void* context);

// The page holding byte `offset`, so a size change keeps the reader's place.
int pageOfOffset(const Paged& paged, uint32_t offset);

// --- The page timer ----------------------------------------------------------

class PageTimer {
 public:
  void setSeconds(int seconds) { seconds_ = seconds; }
  int seconds() const { return seconds_; }
  bool running() const { return running_; }
  // Starts counting a fresh page from `now`. Does nothing with the timer off.
  void start(uint32_t now);
  void stop() { running_ = false; }
  // A page turned by hand restarts the count, so the timer never turns a page
  // somebody has only just reached.
  void restart(uint32_t now);
  // True once when the page's time is up; the count starts again from `now`.
  bool due(uint32_t now);
  // Whole seconds left on this page, for the countdown.
  int secondsLeft(uint32_t now) const;
  // 0..1000 of the page's time gone.
  int permille(uint32_t now) const;

 private:
  int seconds_ = 0;
  bool running_ = false;
  uint32_t pageStart_ = 0;
};

// --- Page turners ------------------------------------------------------------

enum class Turn : uint8_t { None, Next, Back, Toggle };

// What a HID report carries, from its report map.
enum class ReportKind : uint8_t { Unknown, Keyboard, Consumer, Other };

// Consumer keys sent as one bit each rather than as a usage code, as camera
// shutter remotes do: where each key's bit sits in the report.
struct ConsumerBits {
  static constexpr int kMax = 16;
  uint8_t count = 0;
  uint16_t offset[kMax] = {};
  uint16_t usage[kMax] = {};
};

// The kind of each report id declared in a HID report map, read from the usage
// page in force at each Input item. Id 0 stands for a map with no report ids.
struct ReportKinds {
  struct Entry {
    uint8_t id;
    ReportKind kind;
    ConsumerBits bits;
    uint16_t size = 0;  // input bits declared so far
  };
  std::vector<Entry> entries;
  ReportKind kindOf(uint8_t id) const;
  const Entry* find(uint8_t id) const;
};
ReportKinds parseReportMap(const uint8_t* map, size_t len);

// Turns raw reports into page turns, once per press: holding a key, or a
// remote that repeats its report until the key is let go, turns one page.
class TurnDecoder {
 public:
  // `kind` comes from the report map; Unknown guesses from the length, since a
  // boot keyboard report is eight bytes and a consumer report two.
  // `bits`, when the report map gave any, reads consumer keys as one bit each.
  Turn feed(ReportKind kind, const uint8_t* data, size_t len, const ConsumerBits* bits = nullptr);
  void reset();
  // The last feed was a new key press that maps to no turn.
  bool unknownPress() const { return unknown_; }

 private:
  uint8_t keys_[8] = {};
  size_t keyCount_ = 0;
  uint16_t consumer_ = 0;
  uint32_t consumerBits_ = 0;
  bool unknown_ = false;
};

Turn keyboardTurn(uint8_t usage);
Turn consumerTurn(uint16_t usage);
// The swap setting applied.
Turn applySwap(Turn turn, bool swap);

}  // namespace prompter
