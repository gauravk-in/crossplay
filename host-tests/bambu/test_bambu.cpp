// The Bambu monitor's rules, checked without a radio: the settings file, the
// report merge (full and delta), the MQTT packets, and the words on screen.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "BambuCore.h"

using namespace bambu;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

std::string readFile(const char* path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void testConfig() {
  Config config = parseConfig(
      "# hand written\nhost = mqtts://192.168.1.42:8883/\nserial= 01p00a123456789 \naccess_code=12345678\n"
      "keep_awake=0\nunknown=1\n");
  CHECK(config.host == "192.168.1.42");
  CHECK(config.serial == "01P00A123456789");
  CHECK(config.accessCode == "12345678");
  CHECK(!config.keepAwake);
  CHECK(isComplete(config));
  const Config back = parseConfig(formatConfig(config));
  CHECK(back.host == config.host && back.serial == config.serial && back.accessCode == config.accessCode);
  CHECK(back.keepAwake == config.keepAwake);
  CHECK(!isComplete(parseConfig("host=1.2.3.4\nserial=X\n")));
  CHECK(parseConfig("host=" + std::string(200, 'a')).host.size() == kMaxFieldBytes);
  CHECK(parseConfig("access_code=ab cd\n").accessCode == "abcd");
}

void checkFull(const PrinterState& s) {
  CHECK(s.seen);
  CHECK(s.stage() == Stage::Printing);
  CHECK(s.job == "Benchy \"v2\"");
  CHECK(s.percent == 42);
  CHECK(s.remainingMin == 83);
  CHECK(s.layer == 118 && s.totalLayers == 290);
  CHECK(s.nozzleTenths == 2196 && s.nozzleTargetTenths == 2200);
  CHECK(s.bedTenths == 548 && s.bedTargetTenths == 550);
  CHECK(s.hasChamber && s.chamberTenths == 310);
  CHECK(s.speedLevel == 2);
  CHECK(s.amsCount == 2);
  CHECK(s.ams[0].present && s.ams[1].present && !s.ams[2].present);
  CHECK(s.ams[0].humidityLevel == 4 && s.ams[0].humidityPct == 21);
  CHECK(s.ams[0].hasTemp && s.ams[0].tempTenths == 246);
  CHECK(s.ams[1].humidityPct == -1 && s.ams[1].humidityLevel == 2);
  const Tray& a1 = s.ams[0].trays[0];
  CHECK(a1.loaded && a1.type == "PLA" && a1.brand == "PLA Basic" && a1.rgb == 0xFFFFFF && a1.hasColor);
  CHECK(a1.remain == 82);
  CHECK(s.ams[0].trays[1].rgb == 0x000000 && s.ams[0].trays[1].remain == 45);
  CHECK(s.ams[0].trays[2].remain == -1 && s.ams[0].trays[2].type == "PETG");
  CHECK(!s.ams[0].trays[3].loaded);
  CHECK(s.ams[1].trays[0].loaded && s.ams[1].trays[0].remain == 10);
  CHECK(!s.ams[1].trays[1].loaded);
  CHECK(s.trayNow == 1);
  CHECK(s.hasExternal && s.external.loaded && s.external.type == "PETG" && s.external.rgb == 0xFF6A13);
}

void testFullReport() {
  const std::string json = readFile("pushall.json");
  CHECK(!json.empty());
  PrinterState s;
  CHECK(mergeReport(s, json));
  checkFull(s);

  // Fed one byte at a time, as a slow TLS record would deliver it.
  PrinterState t;
  ReportParser parser;
  parser.begin(t);
  for (const char c : json) parser.feed(&c, 1);
  CHECK(parser.end());
  checkFull(t);

  // The same report again changes nothing worth a refresh.
  CHECK(!mergeReport(s, json));
}

void testDeltas() {
  PrinterState s;
  mergeReport(s, readFile("pushall.json"));
  // P1/A1: only what changed.
  CHECK(mergeReport(s, "{\"print\":{\"mc_percent\":43,\"command\":\"push_status\",\"msg\":1}}"));
  CHECK(s.percent == 43);
  CHECK(s.layer == 118);
  CHECK(s.ams[0].trays[0].remain == 82);
  // A tenth of a degree is not news.
  CHECK(!mergeReport(s, "{\"print\":{\"nozzle_temper\":219.7}}"));
  CHECK(s.nozzleTenths == 2197);
  CHECK(mergeReport(s, "{\"print\":{\"nozzle_temper\":205.0}}"));
  // A spool pulled out of A1, everything else as it was.
  CHECK(mergeReport(s,
                    "{\"print\":{\"ams\":{\"ams\":[{\"id\":\"0\",\"tray\":[{\"id\":\"0\"},{\"id\":\"1\",\"remain\":44,"
                    "\"tray_type\":\"PLA\",\"tray_color\":\"000000FF\"},{\"id\":\"2\",\"tray_type\":\"PETG\"},"
                    "{\"id\":\"3\"}]},{\"id\":\"1\",\"tray\":[{\"id\":\"0\",\"tray_type\":\"PLA\"}]}]}}}"));
  CHECK(!s.ams[0].trays[0].loaded);
  CHECK(s.ams[0].trays[1].remain == 44);
  CHECK(s.ams[0].humidityPct == 21);  // left alone
  // An AMS unplugged: the list is shorter.
  CHECK(mergeReport(s, "{\"print\":{\"ams\":{\"ams\":[{\"id\":\"0\",\"tray\":[]}]}}}"));
  CHECK(s.amsCount == 1 && !s.ams[1].present);
  // Finished.
  CHECK(mergeReport(s, "{\"print\":{\"gcode_state\":\"FINISH\",\"mc_percent\":100,\"mc_remaining_time\":0}}"));
  CHECK(s.stage() == Stage::Finished);
  // Other topics' traffic is not the print.
  CHECK(!mergeReport(s, "{\"info\":{\"command\":\"get_version\",\"module\":[{\"name\":\"ota\"}]}}"));
  CHECK(!mergeReport(s, "{\"print\":{\"upgrade_state\":{\"mc_percent\":7}}}"));
  CHECK(s.percent == 100);
}

void testBadInput() {
  PrinterState s;
  CHECK(!mergeReport(s, "{\"print\":{\"mc_percent\":4"));
  CHECK(!mergeReport(s, "not json"));
  CHECK(!mergeReport(s, ""));
  // Nesting past the parser's depth is skipped, not trusted.
  std::string deep = "{\"print\":{\"x\":";
  for (int i = 0; i < 20; ++i) deep += "[";
  for (int i = 0; i < 20; ++i) deep += "]";
  deep += ",\"mc_percent\":9}}";
  CHECK(mergeReport(s, deep));
  CHECK(s.percent == 9);
  // Five AMS units: only four have room.
  std::string many = "{\"print\":{\"ams\":{\"ams\":[";
  for (int i = 0; i < 6; ++i)
    many += std::string(i ? "," : "") + "{\"humidity\":\"3\",\"tray\":[{\"tray_type\":\"PLA\"}]}";
  many += "]}}}";
  CHECK(mergeReport(s, many));
  CHECK(s.amsCount == kMaxAms);
}

void testSnapshot() {
  PrinterState s;
  mergeReport(s, readFile("pushall.json"));
  PrinterState back;
  CHECK(mergeReport(back, formatSnapshot(s)));
  checkFull(back);
  CHECK(formatSnapshot(back) == formatSnapshot(s));
  PrinterState empty;
  PrinterState emptyBack;
  CHECK(mergeReport(emptyBack, formatSnapshot(empty)));
  CHECK(emptyBack.amsCount == 0 && !emptyBack.hasExternal && emptyBack.percent == -1);
}

void testWords() {
  CHECK(durationPhrase(0) == "under a minute");
  CHECK(durationPhrase(12) == "12 min");
  CHECK(durationPhrase(65) == "1 h 05 min");
  CHECK(wholeDegrees(2196) == "220");
  CHECK(wholeDegrees(548) == "55");
  CHECK(std::string(colorName(0xFFFFFF)) == "White");
  CHECK(std::string(colorName(0x000000)) == "Black");
  CHECK(std::string(colorName(0x00AE42)) == "Green");
  CHECK(std::string(colorName(0xC02F20)) == "Red");
  CHECK(std::string(colorName(0x0B2A8A)) == "Blue");
  CHECK(inkLevel(0xFFFFFF) == 0 && inkLevel(0x000000) == 3);
  CHECK(inkLevel(0xF4EE2A) <= 1);
  CHECK(inkLevel(0x0A2989) >= 2);
  CHECK(slotName(0, 0) == "A1" && slotName(1, 3) == "B4" && slotName(-1, 0) == "EXT");
  CHECK(std::string(speedName(4)) == "Ludicrous");
  AmsUnit unit;
  CHECK(humidityPhrase(unit).empty());
  unit.humidityLevel = 3;
  CHECK(humidityPhrase(unit) == "Level 3 of 5");
  unit.humidityPct = 18;
  CHECK(humidityPhrase(unit) == "18% RH");
  CHECK(stageFor("PAUSE") == Stage::Paused && stageFor("PREPARE") == Stage::Preparing);
  CHECK(stageFor("SOMETHING_NEW") == Stage::Unknown);
}

void testMqtt() {
  std::string len;
  mqtt::appendLength(len, 0);
  CHECK(len == std::string(1, '\0'));
  len.clear();
  mqtt::appendLength(len, 321);
  CHECK(len.size() == 2 && static_cast<uint8_t>(len[0]) == 0xC1 && len[1] == 0x02);
  mqtt::LengthReader reader;
  CHECK(reader.push(0xC1) == 0);
  CHECK(reader.push(0x02) == 1);
  CHECK(reader.value == 321);
  mqtt::LengthReader big;
  big.push(0xFF);
  big.push(0xFF);
  big.push(0xFF);
  CHECK(big.push(0xFF) == -1);

  const std::string connect = mqtt::connectPacket("cp", "bblp", "12345678", 60);
  const std::string expected = std::string(
      "\x10\x1E\x00\x04MQTT\x04\xC2\x00\x3C\x00\x02"
      "cp\x00\x04"
      "bblp\x00\x08"
      "12345678",
      32);
  CHECK(connect == expected);
  const std::string sub = mqtt::subscribePacket(1, "device/X/report");
  CHECK(static_cast<uint8_t>(sub[0]) == 0x82);
  CHECK(static_cast<uint8_t>(sub[1]) == sub.size() - 2);
  CHECK(sub.back() == '\0');
  const std::string pub = mqtt::publishPacket(mqtt::requestTopic("X"), mqtt::pushAllPayload());
  CHECK(static_cast<uint8_t>(pub[0]) == 0x30);
  CHECK(pub.find("pushall") != std::string::npos);
  CHECK(mqtt::pingPacket() == std::string("\xC0\x00", 2));
  CHECK(std::string(mqtt::connAckReason(5)).find("access code") != std::string::npos);
}

void testReader() {
  const std::string report = readFile("pushall.json");
  // What the printer sends: the SUBACK, a full report, a delta, a PINGRESP.
  std::string stream = std::string("\x90\x03\x00\x01\x00", 5);
  stream += mqtt::publishPacket(mqtt::reportTopic("01P00A123456789"), report);
  stream += mqtt::publishPacket(mqtt::reportTopic("01P00A123456789"), "{\"print\":{\"mc_percent\":43}}");
  stream += std::string("\xD0\x00", 2);
  CHECK(report.size() > 1000);  // a two-byte remaining length at least
  for (const size_t chunk : {size_t(1), size_t(7), size_t(300), size_t(100000)}) {
    mqtt::Reader reader;
    PrinterState s;
    bool changed = false;
    bool bad = false;
    size_t used = 0;
    for (size_t at = 0; at < stream.size(); at += chunk) {
      const size_t n = stream.size() - at < chunk ? stream.size() - at : chunk;
      used += reader.consume(reinterpret_cast<const uint8_t*>(stream.data() + at), n, s, changed, bad);
    }
    CHECK(!bad);
    CHECK(changed);
    CHECK(used == stream.size());
    CHECK(reader.packets() == 4);
    CHECK(s.percent == 43);
    CHECK(s.amsCount == 2 && s.ams[0].trays[0].remain == 82);
  }
  // A QoS 1 publish carries a packet id before its payload.
  {
    std::string body;
    body += std::string("\x00\x01t\x00\x07", 5);
    body += "{\"print\":{\"layer_num\":5}}";
    std::string packet(1, static_cast<char>(0x32));
    mqtt::appendLength(packet, static_cast<uint32_t>(body.size()));
    packet += body;
    mqtt::Reader reader;
    PrinterState s;
    bool changed = false;
    bool bad = false;
    reader.consume(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(), s, changed, bad);
    CHECK(!bad && changed && s.layer == 5);
  }
  // A remaining length longer than MQTT allows is not MQTT.
  {
    const uint8_t junk[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    mqtt::Reader reader;
    PrinterState s;
    bool changed = false;
    bool bad = false;
    reader.consume(junk, sizeof(junk), s, changed, bad);
    CHECK(bad);
  }
}

}  // namespace

int main() {
  testReader();
  testConfig();
  testFullReport();
  testDeltas();
  testBadInput();
  testSnapshot();
  testWords();
  testMqtt();
  std::printf("bambu: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
