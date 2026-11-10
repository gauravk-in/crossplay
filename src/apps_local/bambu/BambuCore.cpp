#include "BambuCore.h"

#include <StreamingJsonParser.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace bambu {

// --- Settings -------------------------------------------------------------

namespace {

std::string trim(const std::string& text) {
  size_t start = 0;
  size_t end = text.size();
  while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(start, end - start);
}

// Keeps what the printer's own screens can show: printable ASCII, no spaces.
std::string field(const std::string& raw) {
  std::string out;
  for (const char c : trim(raw)) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u > 0x20 && u < 0x7F) out.push_back(c);
    if (out.size() >= kMaxFieldBytes) break;
  }
  return out;
}

bool parseBool(const std::string& value, const bool fallback) {
  if (value == "1" || value == "true" || value == "on" || value == "yes") return true;
  if (value == "0" || value == "false" || value == "off" || value == "no") return false;
  return fallback;
}

// A host typed with a scheme or a port by habit: "mqtts://10.0.0.5:8883/".
std::string hostOnly(std::string host) {
  const size_t scheme = host.find("://");
  if (scheme != std::string::npos) host.erase(0, scheme + 3);
  const size_t slash = host.find('/');
  if (slash != std::string::npos) host.erase(slash);
  const size_t colon = host.find(':');
  if (colon != std::string::npos && host.find(':', colon + 1) == std::string::npos) host.erase(colon);
  return host;
}

}  // namespace

Config parseConfig(const std::string& text) {
  Config config;
  size_t pos = 0;
  while (pos < text.size() && pos < kMaxConfigBytes * 4) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "host") {
      config.host = field(hostOnly(value));
    } else if (key == "serial") {
      config.serial = field(value);
      for (char& c : config.serial) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (key == "access_code") {
      config.accessCode = field(value);
    } else if (key == "keep_awake") {
      config.keepAwake = parseBool(value, config.keepAwake);
    }
  }
  return config;
}

std::string formatConfig(const Config& config) {
  return "host=" + config.host + "\nserial=" + config.serial + "\naccess_code=" + config.accessCode +
         "\nkeep_awake=" + (config.keepAwake ? "1" : "0") + "\n";
}

bool isComplete(const Config& config) {
  return !config.host.empty() && !config.serial.empty() && !config.accessCode.empty();
}

// --- Stage ---------------------------------------------------------------------

Stage stageFor(const std::string& gcodeState) {
  if (gcodeState.empty()) return Stage::Unknown;
  if (gcodeState == "IDLE") return Stage::Idle;
  if (gcodeState == "PREPARE" || gcodeState == "SLICING" || gcodeState == "INIT") return Stage::Preparing;
  if (gcodeState == "RUNNING") return Stage::Printing;
  if (gcodeState == "PAUSE") return Stage::Paused;
  if (gcodeState == "FINISH") return Stage::Finished;
  if (gcodeState == "FAILED") return Stage::Failed;
  return Stage::Unknown;
}

const char* stageLabel(const Stage stage) {
  switch (stage) {
    case Stage::Idle:
      return "IDLE";
    case Stage::Preparing:
      return "PREPARING";
    case Stage::Printing:
      return "PRINTING";
    case Stage::Paused:
      return "PAUSED";
    case Stage::Finished:
      return "FINISHED";
    case Stage::Failed:
      return "FAILED";
    case Stage::Unknown:
      break;
  }
  return "WAITING";
}

// --- The report merge ----------------------------------------------------------

namespace {

constexpr int kMaxDepth = 10;
constexpr size_t kKeyBytes = 24;

enum class Node : uint8_t {
  Other,     // anything this app does not read
  Root,      // the report itself
  Print,     // print
  AmsBlock,  // print.ams
  AmsList,   // print.ams.ams
  AmsUnit,   // print.ams.ams[i]
  TrayList,  // print.ams.ams[i].tray
  Tray,      // print.ams.ams[i].tray[j]
  External,  // print.vt_tray
};

struct Frame {
  Node node = Node::Other;
  bool array = false;
  int index = 0;  // this frame's position in its parent array
  int count = 0;  // elements seen so far, for an array frame
  int keys = 0;   // keys seen so far, for an object frame
  bool sawType = false;
  bool typeEmpty = false;
};

bool keyIs(const char* key, const char* name) { return std::strcmp(key, name) == 0; }

int toTenths(const char* text, const size_t len) {
  char buf[24];
  const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  std::memcpy(buf, text, n);
  buf[n] = '\0';
  const double v = std::strtod(buf, nullptr);
  return static_cast<int>(v >= 0 ? v * 10.0 + 0.5 : v * 10.0 - 0.5);
}

int toInt(const char* text, const size_t len) {
  char buf[24];
  const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  std::memcpy(buf, text, n);
  buf[n] = '\0';
  return static_cast<int>(std::strtol(buf, nullptr, 10));
}

uint32_t toColor(const char* text, const size_t len, bool& ok) {
  ok = false;
  if (len < 6) return 0;
  uint32_t rgb = 0;
  for (size_t i = 0; i < 6; ++i) {
    const char c = text[i];
    int digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      return 0;
    }
    rgb = (rgb << 4) | static_cast<uint32_t>(digit);
  }
  // An alpha of 00 is how the AMS says "no colour read".
  if (len >= 8 && text[6] == '0' && text[7] == '0') return 0;
  ok = true;
  return rgb;
}

}  // namespace

struct ReportParser::Impl {
  Impl();

  PrinterState* state = nullptr;
  Frame frames[kMaxDepth];
  int depth = 0;
  int overflowDepth = 0;  // levels past kMaxDepth, skipped
  char key[kKeyBytes] = {};
  bool closedRoot = false;
  bool changed = false;
  bool bad = false;
  StreamingJsonParser parser;

  Frame* top() { return depth > 0 ? &frames[depth - 1] : nullptr; }
  Frame* parent() { return depth > 1 ? &frames[depth - 2] : nullptr; }

  Tray* currentTray() {
    if (depth < 2) return nullptr;
    Frame& f = frames[depth - 1];
    if (f.node == Node::External) return &state->external;
    if (f.node != Node::Tray || depth < 3) return nullptr;
    const int unit = frames[depth - 3].index;
    if (unit < 0 || unit >= kMaxAms || f.index < 0 || f.index >= kTraysPerAms) return nullptr;
    return &state->ams[unit].trays[f.index];
  }

  AmsUnit* currentUnit() {
    Frame* f = top();
    if (!f || f->node != Node::AmsUnit || f->index < 0 || f->index >= kMaxAms) return nullptr;
    return &state->ams[f->index];
  }

  // The node a container opening here becomes, from where it opens.
  Node childNode(const bool array) const {
    if (depth == 0) return array ? Node::Other : Node::Root;
    const Frame& f = frames[depth - 1];
    if (f.array) {
      if (f.node == Node::AmsList && !array) return Node::AmsUnit;
      if (f.node == Node::TrayList && !array) return Node::Tray;
      return Node::Other;
    }
    switch (f.node) {
      case Node::Root:
        return !array && keyIs(key, "print") ? Node::Print : Node::Other;
      case Node::Print:
        if (!array && keyIs(key, "ams")) return Node::AmsBlock;
        if (!array && keyIs(key, "vt_tray")) return Node::External;
        return Node::Other;
      case Node::AmsBlock:
        return array && keyIs(key, "ams") ? Node::AmsList : Node::Other;
      case Node::AmsUnit:
        return array && keyIs(key, "tray") ? Node::TrayList : Node::Other;
      default:
        return Node::Other;
    }
  }

  void open(const bool array) {
    if (depth >= kMaxDepth || overflowDepth > 0) {
      ++overflowDepth;
      return;
    }
    Frame frame;
    frame.node = childNode(array);
    frame.array = array;
    if (Frame* up = top(); up && up->array) frame.index = up->count++;
    frames[depth++] = frame;
    key[0] = '\0';
    if (frame.node == Node::External) state->hasExternal = true;
    if (frame.node == Node::AmsUnit && frame.index < kMaxAms) {
      if (!state->ams[frame.index].present) changed = true;
      state->ams[frame.index].present = true;
    }
  }

  void close() {
    if (overflowDepth > 0) {
      --overflowDepth;
      return;
    }
    if (depth == 0) {
      bad = true;
      return;
    }
    Frame frame = frames[--depth];
    key[0] = '\0';
    if (depth == 0) closedRoot = true;
    if (frame.node == Node::Tray || frame.node == Node::External) {
      ++depth;  // look the tray up with its own frame in place
      Tray* tray = currentTray();
      --depth;
      // A slot reported as a bare {"id": n}, or with an empty type, is empty.
      if (tray && ((frame.keys == 1 && !frame.sawType) || (frame.sawType && frame.typeEmpty))) {
        if (tray->loaded) changed = true;
        *tray = Tray{};
      }
    }
    if (frame.node == Node::AmsList) {
      // The list is the whole set of units: any unit past its end is gone.
      const int units = frame.count < kMaxAms ? frame.count : kMaxAms;
      for (int i = units; i < kMaxAms; ++i) {
        if (state->ams[i].present) changed = true;
        state->ams[i] = AmsUnit{};
      }
      if (state->amsCount != units) changed = true;
      state->amsCount = units;
    }
    if (frame.node == Node::TrayList && depth > 0) {
      AmsUnit* unit = currentUnit();
      if (unit) {
        for (int i = frame.count; i < kTraysPerAms; ++i) unit->trays[i] = Tray{};
      }
    }
  }

  template <typename T>
  void set(T& slot, const T value) {
    if (slot != value) {
      slot = value;
      changed = true;
    }
  }

  void value(const char* text, const size_t len, const bool isString) {
    if (overflowDepth > 0) return;
    Frame* f = top();
    if (!f) return;
    if (f->array) {
      ++f->count;
      return;
    }
    switch (f->node) {
      case Node::Print:
        printValue(text, len);
        break;
      case Node::AmsBlock:
        if (keyIs(key, "tray_now")) set(state->trayNow, toInt(text, len));
        break;
      case Node::AmsUnit: {
        AmsUnit* unit = currentUnit();
        if (!unit) break;
        if (keyIs(key, "humidity")) set(unit->humidityLevel, toInt(text, len));
        if (keyIs(key, "humidity_raw")) set(unit->humidityPct, toInt(text, len));
        if (keyIs(key, "temp")) {
          set(unit->tempTenths, toTenths(text, len));
          set(unit->hasTemp, true);
        }
        break;
      }
      case Node::Tray:
      case Node::External: {
        Tray* tray = currentTray();
        if (!tray) break;
        const std::string s(text, len);
        if (keyIs(key, "tray_type")) {
          f->sawType = true;
          f->typeEmpty = s.empty();
          set(tray->type, s);
          if (!s.empty()) set(tray->loaded, true);
        } else if (keyIs(key, "tray_sub_brands")) {
          set(tray->brand, s);
        } else if (keyIs(key, "tray_color")) {
          bool ok = false;
          const uint32_t rgb = toColor(text, len, ok);
          set(tray->rgb, rgb);
          set(tray->hasColor, ok);
        } else if (keyIs(key, "remain")) {
          set(tray->remain, toInt(text, len));
        }
        break;
      }
      default:
        break;
    }
    (void)isString;
    key[0] = '\0';
  }

  void printValue(const char* text, const size_t len) {
    if (keyIs(key, "gcode_state")) {
      set(state->gcodeState, std::string(text, len));
    } else if (keyIs(key, "subtask_name")) {
      set(state->job, std::string(text, len));
    } else if (keyIs(key, "mc_percent")) {
      set(state->percent, toInt(text, len));
    } else if (keyIs(key, "mc_remaining_time")) {
      set(state->remainingMin, toInt(text, len));
    } else if (keyIs(key, "layer_num")) {
      set(state->layer, toInt(text, len));
    } else if (keyIs(key, "total_layer_num")) {
      set(state->totalLayers, toInt(text, len));
    } else if (keyIs(key, "nozzle_temper")) {
      setTemp(state->nozzleTenths, text, len);
    } else if (keyIs(key, "nozzle_target_temper")) {
      setTemp(state->nozzleTargetTenths, text, len);
    } else if (keyIs(key, "bed_temper")) {
      setTemp(state->bedTenths, text, len);
    } else if (keyIs(key, "bed_target_temper")) {
      setTemp(state->bedTargetTenths, text, len);
    } else if (keyIs(key, "chamber_temper")) {
      setTemp(state->chamberTenths, text, len);
      state->hasChamber = true;
    } else if (keyIs(key, "spd_lvl")) {
      set(state->speedLevel, toInt(text, len));
    } else if (keyIs(key, "print_error")) {
      set(state->printError, toInt(text, len));
    }
  }

  // Temperatures wobble by a tenth every second; a change under a whole degree
  // is not worth a refresh of the panel.
  void setTemp(int& slot, const char* text, const size_t len) {
    const int tenths = toTenths(text, len);
    if (std::abs(tenths - slot) >= 10 || (tenths == 0) != (slot == 0)) changed = true;
    slot = tenths;
  }

  void onKey(const char* text, const size_t len) {
    if (overflowDepth > 0) return;
    const size_t n = len < kKeyBytes - 1 ? len : kKeyBytes - 1;
    std::memcpy(key, text, n);
    key[n] = '\0';
    if (Frame* f = top()) ++f->keys;
  }
};

namespace {

void cbKey(void* ctx, const char* text, size_t len) { static_cast<ReportParser::Impl*>(ctx)->onKey(text, len); }
void cbString(void* ctx, const char* text, size_t len) {
  static_cast<ReportParser::Impl*>(ctx)->value(text, len, true);
}
void cbNumber(void* ctx, const char* text, size_t len) {
  static_cast<ReportParser::Impl*>(ctx)->value(text, len, false);
}
void cbBool(void* ctx, bool v) { static_cast<ReportParser::Impl*>(ctx)->value(v ? "1" : "0", 1, false); }
void cbNull(void* ctx) { static_cast<ReportParser::Impl*>(ctx)->value("", 0, false); }
void cbObjectStart(void* ctx) { static_cast<ReportParser::Impl*>(ctx)->open(false); }
void cbObjectEnd(void* ctx) { static_cast<ReportParser::Impl*>(ctx)->close(); }
void cbArrayStart(void* ctx) { static_cast<ReportParser::Impl*>(ctx)->open(true); }
void cbArrayEnd(void* ctx) { static_cast<ReportParser::Impl*>(ctx)->close(); }

JsonCallbacks callbacksFor(void* ctx) {
  JsonCallbacks cb{};
  cb.ctx = ctx;
  cb.onKey = cbKey;
  cb.onString = cbString;
  cb.onNumber = cbNumber;
  cb.onBool = cbBool;
  cb.onNull = cbNull;
  cb.onObjectStart = cbObjectStart;
  cb.onObjectEnd = cbObjectEnd;
  cb.onArrayStart = cbArrayStart;
  cb.onArrayEnd = cbArrayEnd;
  return cb;
}

}  // namespace

ReportParser::Impl::Impl() : parser(callbacksFor(this)) {}

// Nothrow: with -fno-exceptions a bare new that fails aborts. A parser that
// could not be made reads every report as malformed.
ReportParser::ReportParser() : impl_(new (std::nothrow) Impl()) {}
ReportParser::~ReportParser() = default;

void ReportParser::begin(PrinterState& state) {
  if (!impl_) return;
  impl_->state = &state;
  impl_->depth = 0;
  impl_->overflowDepth = 0;
  impl_->key[0] = '\0';
  impl_->closedRoot = false;
  impl_->changed = false;
  impl_->bad = false;
  impl_->parser.reset();
}

void ReportParser::feed(const char* data, const size_t len) {
  if (!impl_ || !impl_->state) return;
  impl_->parser.feed(data, len);
}

bool ReportParser::end() {
  if (!impl_ || !impl_->state) return false;
  const bool ok = !failed();
  if (ok) {
    if (!impl_->state->seen) impl_->changed = true;
    impl_->state->seen = true;
  }
  impl_->state = nullptr;
  return ok && impl_->changed;
}

bool ReportParser::failed() const {
  if (!impl_) return true;
  return impl_->bad || impl_->parser.hasError() || !impl_->closedRoot || impl_->depth != 0;
}

bool mergeReport(PrinterState& state, const std::string& json) {
  ReportParser parser;
  parser.begin(state);
  parser.feed(json.data(), json.size());
  return parser.end();
}

namespace {

void appendEscaped(std::string& out, const std::string& text) {
  out.push_back('"');
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (u < 0x20) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "\\u%04x", u);
      out += buf;
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
}

void appendNumber(std::string& out, const char* key, const int value) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), ",\"%s\":%d", key, value);
  out += buf;
}

void appendTenths(std::string& out, const char* key, const int tenths) {
  char buf[48];
  const int whole = tenths / 10;
  const int frac = std::abs(tenths % 10);
  std::snprintf(buf, sizeof(buf), ",\"%s\":%s%d.%d", key, tenths < 0 && whole == 0 ? "-" : "", whole, frac);
  out += buf;
}

void appendTray(std::string& out, const Tray& tray, const int id) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "{\"id\":\"%d\"", id);
  out += buf;
  if (tray.loaded) {
    out += ",\"tray_type\":";
    appendEscaped(out, tray.type);
    out += ",\"tray_sub_brands\":";
    appendEscaped(out, tray.brand);
    std::snprintf(buf, sizeof(buf), ",\"tray_color\":\"%06X%s\"", static_cast<unsigned>(tray.rgb & 0xFFFFFF),
                  tray.hasColor ? "FF" : "00");
    out += buf;
    appendNumber(out, "remain", tray.remain);
  }
  out += "}";
}

}  // namespace

std::string formatSnapshot(const PrinterState& state) {
  std::string out;
  out.reserve(1024);
  out += "{\"print\":{\"gcode_state\":";
  appendEscaped(out, state.gcodeState);
  out += ",\"subtask_name\":";
  appendEscaped(out, state.job);
  appendNumber(out, "mc_percent", state.percent);
  appendNumber(out, "mc_remaining_time", state.remainingMin);
  appendNumber(out, "layer_num", state.layer);
  appendNumber(out, "total_layer_num", state.totalLayers);
  appendTenths(out, "nozzle_temper", state.nozzleTenths);
  appendTenths(out, "nozzle_target_temper", state.nozzleTargetTenths);
  appendTenths(out, "bed_temper", state.bedTenths);
  appendTenths(out, "bed_target_temper", state.bedTargetTenths);
  if (state.hasChamber) appendTenths(out, "chamber_temper", state.chamberTenths);
  appendNumber(out, "spd_lvl", state.speedLevel);
  appendNumber(out, "print_error", state.printError);
  out += ",\"ams\":{\"ams\":[";
  for (int i = 0; i < state.amsCount && i < kMaxAms; ++i) {
    const AmsUnit& unit = state.ams[i];
    if (i > 0) out += ",";
    char buf[48];
    std::snprintf(buf, sizeof(buf), "{\"id\":\"%d\"", i);
    out += buf;
    if (unit.humidityLevel > 0) appendNumber(out, "humidity", unit.humidityLevel);
    if (unit.humidityPct >= 0) appendNumber(out, "humidity_raw", unit.humidityPct);
    if (unit.hasTemp) appendTenths(out, "temp", unit.tempTenths);
    out += ",\"tray\":[";
    for (int t = 0; t < kTraysPerAms; ++t) {
      if (t > 0) out += ",";
      appendTray(out, unit.trays[t], t);
    }
    out += "]}";
  }
  out += "]";
  appendNumber(out, "tray_now", state.trayNow);
  out += "}";
  if (state.hasExternal) {
    out += ",\"vt_tray\":";
    appendTray(out, state.external, kTrayExternal);
  }
  out += "}}";
  return out;
}

// --- Words ---------------------------------------------------------------------

std::string durationPhrase(const int minutes) {
  if (minutes <= 0) return "under a minute";
  char buf[32];
  if (minutes < 60) {
    std::snprintf(buf, sizeof(buf), "%d min", minutes);
  } else {
    std::snprintf(buf, sizeof(buf), "%d h %02d min", minutes / 60, minutes % 60);
  }
  return buf;
}

std::string wholeDegrees(const int tenths) {
  char buf[16];
  const int whole = tenths >= 0 ? (tenths + 5) / 10 : (tenths - 5) / 10;
  std::snprintf(buf, sizeof(buf), "%d", whole);
  return buf;
}

namespace {

struct NamedColor {
  const char* name;
  uint32_t rgb;
};

// Bambu's own filament colours where they have one, so a Bambu spool reads the
// way the box does. Names are short: the swatch row has a 100px column.
constexpr NamedColor kColors[] = {
    {"White", 0xFFFFFF},  {"Ivory", 0xF7F3E3},      {"Beige", 0xF7E6DE},      {"Light Gray", 0xD1D3D5},
    {"Silver", 0xA6A9AA}, {"Gray", 0x8E9089},       {"Dark Gray", 0x545454},  {"Black", 0x000000},
    {"Red", 0xC12E1F},    {"Maroon", 0x9D2235},     {"Pink", 0xF55A74},       {"Magenta", 0xEC008C},
    {"Orange", 0xFF6A13}, {"Brown", 0x9D432C},      {"Gold", 0xE4BD68},       {"Yellow", 0xF4EE2A},
    {"Lime", 0xBECF00},   {"Green", 0x00AE42},      {"Dark Green", 0x164B35}, {"Teal", 0x00897B},
    {"Cyan", 0x0086D6},   {"Light Blue", 0x8ACBEE}, {"Blue", 0x0A2989},       {"Navy", 0x042F56},
    {"Purple", 0x5E43B7}, {"Lilac", 0xAE96D4},
};

int channel(const uint32_t rgb, const int shift) { return static_cast<int>((rgb >> shift) & 0xFF); }

}  // namespace

const char* colorName(const uint32_t rgb) {
  const int r = channel(rgb, 16);
  const int g = channel(rgb, 8);
  const int b = channel(rgb, 0);
  const char* best = kColors[0].name;
  long bestDistance = -1;
  for (const NamedColor& c : kColors) {
    const int dr = r - channel(c.rgb, 16);
    const int dg = g - channel(c.rgb, 8);
    const int db = b - channel(c.rgb, 0);
    // "Redmean": cheap and close enough to how the eye ranks colour distance.
    const int rmean = (r + channel(c.rgb, 16)) / 2;
    const long distance = static_cast<long>((512 + rmean) * dr * dr / 256) + 4L * dg * dg +
                          static_cast<long>((767 - rmean) * db * db / 256);
    if (bestDistance < 0 || distance < bestDistance) {
      bestDistance = distance;
      best = c.name;
    }
  }
  return best;
}

int inkLevel(const uint32_t rgb) {
  const int luma = (299 * channel(rgb, 16) + 587 * channel(rgb, 8) + 114 * channel(rgb, 0)) / 1000;
  if (luma >= 200) return 0;
  if (luma >= 130) return 1;
  if (luma >= 50) return 2;
  return 3;
}

std::string slotName(const int ams, const int tray) {
  if (ams < 0) return "EXT";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%c%d", static_cast<char>('A' + ams), tray + 1);
  return buf;
}

const char* speedName(const int level) {
  switch (level) {
    case 1:
      return "Silent";
    case 2:
      return "Standard";
    case 3:
      return "Sport";
    case 4:
      return "Ludicrous";
    default:
      return "";
  }
}

std::string humidityPhrase(const AmsUnit& unit) {
  char buf[24];
  if (unit.humidityPct >= 0) {
    std::snprintf(buf, sizeof(buf), "%d%% RH", unit.humidityPct);
    return buf;
  }
  if (unit.humidityLevel > 0) {
    std::snprintf(buf, sizeof(buf), "Level %d of 5", unit.humidityLevel);
    return buf;
  }
  return "";
}

// --- MQTT ---------------------------------------------------------------------

namespace mqtt {

namespace {

void appendU16(std::string& out, const uint16_t value) {
  out.push_back(static_cast<char>(value >> 8));
  out.push_back(static_cast<char>(value & 0xFF));
}

void appendString(std::string& out, const std::string& text) {
  appendU16(out, static_cast<uint16_t>(text.size()));
  out += text;
}

std::string packet(const uint8_t header, const std::string& body) {
  std::string out;
  out.reserve(body.size() + 5);
  out.push_back(static_cast<char>(header));
  appendLength(out, static_cast<uint32_t>(body.size()));
  out += body;
  return out;
}

}  // namespace

void appendLength(std::string& out, uint32_t length) {
  do {
    uint8_t byte = length % 128;
    length /= 128;
    if (length > 0) byte |= 0x80;
    out.push_back(static_cast<char>(byte));
  } while (length > 0);
}

int LengthReader::push(const uint8_t byte) {
  if (bytes >= 4) return -1;
  value += static_cast<uint32_t>(byte & 0x7F) * multiplier;
  multiplier *= 128;
  ++bytes;
  if ((byte & 0x80) == 0) return 1;
  return bytes >= 4 ? -1 : 0;
}

std::string connectPacket(const std::string& clientId, const std::string& user, const std::string& password,
                          const uint16_t keepAliveSeconds) {
  std::string body;
  appendString(body, "MQTT");
  body.push_back(4);  // protocol level 3.1.1
  // Username, password, clean session.
  body.push_back(static_cast<char>(0xC2));
  appendU16(body, keepAliveSeconds);
  appendString(body, clientId);
  appendString(body, user);
  appendString(body, password);
  return packet(kConnect << 4, body);
}

std::string subscribePacket(const uint16_t packetId, const std::string& topic) {
  std::string body;
  appendU16(body, packetId);
  appendString(body, topic);
  body.push_back(0);  // QoS 0
  return packet((kSubscribe << 4) | 0x02, body);
}

std::string publishPacket(const std::string& topic, const std::string& payload) {
  std::string body;
  appendString(body, topic);
  body += payload;
  return packet(kPublish << 4, body);
}

std::string pingPacket() { return packet(kPingReq << 4, ""); }
std::string disconnectPacket() { return packet(kDisconnect << 4, ""); }

size_t Reader::consume(const uint8_t* data, const size_t len, PrinterState& state, bool& changed, bool& bad) {
  size_t i = 0;
  while (i < len && !bad) {
    switch (step_) {
      case Step::Header:
        header_ = data[i++];
        length_ = LengthReader{};
        step_ = Step::Length;
        break;
      case Step::Length: {
        const int r = length_.push(data[i++]);
        if (r < 0) {
          bad = true;
          break;
        }
        if (r == 0) break;
        left_ = length_.value;
        if ((header_ >> 4) == kPublish) {
          idLeft_ = ((header_ >> 1) & 0x03) ? 2 : 0;
          step_ = Step::TopicLength;
          topicLeft_ = 2;
        } else {
          step_ = Step::Body;
        }
        if (left_ == 0) finish(changed);
        break;
      }
      case Step::TopicLength:
        // Two bytes, big-endian; topicLeft_ counts them down first.
        if (topicLeft_ == 2) {
          topicHigh_ = data[i++];
          topicLeft_ = 1;
        } else {
          topicLeft_ = static_cast<uint16_t>((topicHigh_ << 8) | data[i++]);
          step_ = topicLeft_ > 0 ? Step::Topic : (idLeft_ > 0 ? Step::PacketId : Step::Payload);
          if (step_ == Step::Payload) parser_.begin(state);
        }
        if (--left_ == 0) finish(changed);
        break;
      case Step::Topic: {
        // Only the report topic is subscribed, so the name is skipped.
        size_t n = len - i;
        if (n > topicLeft_) n = topicLeft_;
        if (n > left_) n = left_;
        i += n;
        topicLeft_ = static_cast<uint16_t>(topicLeft_ - n);
        left_ -= static_cast<uint32_t>(n);
        if (topicLeft_ == 0) {
          step_ = idLeft_ > 0 ? Step::PacketId : Step::Payload;
          if (step_ == Step::Payload) parser_.begin(state);
        }
        if (left_ == 0) finish(changed);
        break;
      }
      case Step::PacketId:
        ++i;
        --left_;
        if (--idLeft_ == 0) {
          step_ = Step::Payload;
          parser_.begin(state);
        }
        if (left_ == 0) finish(changed);
        break;
      case Step::Payload: {
        size_t n = len - i;
        if (n > left_) n = left_;
        parser_.feed(reinterpret_cast<const char*>(data + i), n);
        i += n;
        left_ -= static_cast<uint32_t>(n);
        if (left_ == 0) finish(changed);
        break;
      }
      case Step::Body: {
        size_t n = len - i;
        if (n > left_) n = left_;
        i += n;
        left_ -= static_cast<uint32_t>(n);
        if (left_ == 0) finish(changed);
        break;
      }
    }
  }
  return i;
}

void Reader::finish(bool& changed) {
  if (step_ == Step::Payload && parser_.end()) changed = true;
  ++packets_;
  reset();
}

void Reader::reset() {
  step_ = Step::Header;
  length_ = LengthReader{};
  left_ = 0;
  topicLeft_ = 0;
  idLeft_ = 0;
}

std::string reportTopic(const std::string& serial) { return "device/" + serial + "/report"; }
std::string requestTopic(const std::string& serial) { return "device/" + serial + "/request"; }

std::string pushAllPayload() {
  return "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}";
}

const char* connAckReason(const uint8_t code) {
  switch (code) {
    case 0:
      return "";
    case 1:
      return "The printer refused the MQTT version.";
    case 2:
      return "The printer refused this reader's client id.";
    case 3:
      return "The printer's MQTT server is not running. Is LAN mode on?";
    case 4:
    case 5:
      return "The printer refused the access code. Check it on the printer's screen.";
    default:
      return "The printer refused the connection.";
  }
}

}  // namespace mqtt

}  // namespace bambu
