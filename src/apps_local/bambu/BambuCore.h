#pragma once

// Bambu Lab printer monitor: the rules, with nothing in them that needs a
// radio, a card or a panel.
//
// The printer runs an MQTT broker on the LAN (TLS on 8883, user "bblp", the
// LAN access code as password). It publishes its state on
// device/<serial>/report: one full "pushall" when asked for it, then either
// full reports (X1) or only the fields that changed (P1, A1). So the state is
// a merge target, never replaced wholesale, and a report carrying three keys
// changes three fields.
//
//   {"print": {"gcode_state": "RUNNING", "mc_percent": 42, ...,
//              "ams": {"ams": [{"humidity": "4", "humidity_raw": "21",
//                               "temp": "24.6", "tray": [{"id": "0",
//                               "tray_type": "PLA", "tray_color": "FFFFFFFF",
//                               "remain": 80}, ...]}],
//                      "tray_now": "0"},
//              "vt_tray": {...}}}
//
// Everything here is host-tested in host-tests/bambu: the settings file, the
// report merge, the MQTT packets, and the words the screens print.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace bambu {

// --- Settings -------------------------------------------------------------

struct Config {
  std::string host;        // the printer's address on the LAN
  std::string serial;      // the printer's serial number, which names its topics
  std::string accessCode;  // the LAN access code from the printer's own screen
  // Keep the reader awake while the monitor is up. Off lets the reader's own
  // sleep timeout end the session like any other app.
  bool keepAwake = true;
};

constexpr size_t kMaxFieldBytes = 64;
constexpr size_t kMaxConfigBytes = 512;
constexpr uint16_t kPort = 8883;
constexpr const char* kUser = "bblp";

// key=value lines. Unknown keys are ignored, values are trimmed and capped, and
// the serial is upper-cased, the way the printer prints it.
Config parseConfig(const std::string& text);
std::string formatConfig(const Config& config);
// All three of host, serial and access code are there.
bool isComplete(const Config& config);

// --- The printer's state ----------------------------------------------------

enum class Stage : uint8_t { Unknown, Idle, Preparing, Printing, Paused, Finished, Failed };
Stage stageFor(const std::string& gcodeState);
// "PRINTING", "PAUSED", ...: the band across the top of the monitor.
const char* stageLabel(Stage stage);

struct Tray {
  bool loaded = false;  // a spool is in the slot
  std::string type;     // "PLA", "PETG-HF"
  std::string brand;    // "PLA Basic", "Bambu PETG HF"
  uint32_t rgb = 0;     // tray_color without its alpha byte
  bool hasColor = false;
  int remain = -1;  // percent, -1 when the AMS cannot tell (not a Bambu spool)
};

constexpr int kTraysPerAms = 4;
constexpr int kMaxAms = 4;
// tray_now values that are not an AMS slot.
constexpr int kTrayExternal = 254;
constexpr int kTrayNone = 255;

struct AmsUnit {
  bool present = false;
  int humidityLevel = 0;  // the AMS's own 1..5 scale; 0 unknown
  int humidityPct = -1;   // humidity_raw, %RH, on firmware that reports it
  int tempTenths = 0;
  bool hasTemp = false;
  Tray trays[kTraysPerAms];
};

struct PrinterState {
  bool seen = false;  // at least one report has arrived
  std::string gcodeState;
  std::string job;   // subtask_name: the plate's name as sliced
  int percent = -1;  // mc_percent
  int remainingMin = -1;
  int layer = -1;
  int totalLayers = -1;
  int nozzleTenths = 0;
  int nozzleTargetTenths = 0;
  int bedTenths = 0;
  int bedTargetTenths = 0;
  int chamberTenths = 0;
  bool hasChamber = false;
  int speedLevel = 0;  // 1 silent, 2 standard, 3 sport, 4 ludicrous
  int printError = 0;
  int amsCount = 0;
  AmsUnit ams[kMaxAms];
  int trayNow = kTrayNone;
  bool hasExternal = false;
  Tray external;  // vt_tray: the spool on the back of the printer

  Stage stage() const { return stageFor(gcodeState); }
};

// Merges one report into a state, fed in chunks as the bytes arrive, so a
// 30KB pushall never needs a 30KB buffer. Fields the report leaves out are
// left alone.
class ReportParser {
 public:
  ReportParser();
  ~ReportParser();
  ReportParser(const ReportParser&) = delete;
  ReportParser& operator=(const ReportParser&) = delete;

  void begin(PrinterState& state);
  void feed(const char* data, size_t len);
  // True when the report was well-formed and changed something worth drawing.
  bool end();
  bool failed() const;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// The whole state as a report the parser reads back, for the card: opening the
// app shows the last thing the printer said before the radio is up.
std::string formatSnapshot(const PrinterState& state);
// One-shot convenience over ReportParser.
bool mergeReport(PrinterState& state, const std::string& json);

// --- Words ---------------------------------------------------------------------

// "1 h 05 min", "12 min", "under a minute"
std::string durationPhrase(int minutes);
// "220" from tenths, rounded. The Toybox cuts above 10px are ASCII only, so
// the unit is the label's job, not the number's.
std::string wholeDegrees(int tenths);
// The nearest everyday colour name for a tray_color: "White", "Jade", ...
const char* colorName(uint32_t rgb);
// How the swatch is inked on a black-and-white panel: 0 white, 1 light gray
// dither, 2 dark gray dither, 3 black.
int inkLevel(uint32_t rgb);
// "A1", "B3": how Bambu Studio names a slot.
std::string slotName(int ams, int tray);
// "Silent", "Standard", "Sport", "Ludicrous"
const char* speedName(int level);
// "Dry", "OK", "Damp", "Wet" from the AMS's state; empty when unknown.
std::string humidityPhrase(const AmsUnit& unit);

// --- MQTT 3.1.1, just the packets a read-only client needs ---------------------

namespace mqtt {

enum : uint8_t {
  kConnect = 1,
  kConnAck = 2,
  kPublish = 3,
  kSubscribe = 8,
  kSubAck = 9,
  kPingReq = 12,
  kPingResp = 13,
  kDisconnect = 14,
};

std::string connectPacket(const std::string& clientId, const std::string& user, const std::string& password,
                          uint16_t keepAliveSeconds);
std::string subscribePacket(uint16_t packetId, const std::string& topic);
std::string publishPacket(const std::string& topic, const std::string& payload);
std::string pingPacket();
std::string disconnectPacket();

// Appends MQTT's variable-length "remaining length".
void appendLength(std::string& out, uint32_t length);

// Reads a remaining length one byte at a time. Returns 1 when complete, 0 when
// it needs another byte, -1 when the encoding is invalid.
struct LengthReader {
  uint32_t value = 0;
  uint32_t multiplier = 1;
  int bytes = 0;
  int push(uint8_t byte);
};

// Reads the packets the printer sends, a chunk at a time, and streams each
// PUBLISH payload into a ReportParser. Only the report topic is subscribed,
// so a PUBLISH is always a report and its topic name is skipped.
class Reader {
 public:
  void reset();
  // Feeds one chunk. `changed` is set when a report changed the state, `bad`
  // when the stream is not MQTT any more. Returns the bytes it used.
  size_t consume(const uint8_t* data, size_t len, PrinterState& state, bool& changed, bool& bad);
  // Packets seen whole, PUBLISH or not.
  uint32_t packets() const { return packets_; }

 private:
  void finish(bool& changed);

  enum class Step : uint8_t { Header, Length, TopicLength, Topic, PacketId, Payload, Body };
  ReportParser parser_;
  Step step_ = Step::Header;
  uint8_t header_ = 0;
  LengthReader length_;
  uint32_t left_ = 0;  // bytes of the packet still to read
  uint16_t topicLeft_ = 0;
  uint8_t topicHigh_ = 0;
  int idLeft_ = 0;
  uint32_t packets_ = 0;
};

std::string reportTopic(const std::string& serial);
std::string requestTopic(const std::string& serial);
// The request that makes the printer send everything it knows.
std::string pushAllPayload();
// CONNACK return codes, said in words.
const char* connAckReason(uint8_t code);

}  // namespace mqtt

}  // namespace bambu
