#include "GCalLibrary.h"

#include <HalStorage.h>
#include <Logging.h>

namespace gcal {
namespace {

constexpr const char* kTag = "GCAL";
constexpr const char* kDir = "/.crosspoint/gcal";
constexpr const char* kEvents = "/.crosspoint/gcal/events.tsv";
constexpr const char* kSettings = "/.crosspoint/gcal/settings.cfg";
constexpr const char* kMeta = "/.crosspoint/gcal/meta.cfg";
constexpr const char* kAsleep = "/.crosspoint/gcal/asleep.cfg";

// Larger than the events file has reason to be: GCalApi stops at 1500 events,
// a little over 100KB. A bigger file is not ours.
constexpr size_t kReadCap = 512 * 1024;

bool readWhole(const char* path, std::string& out) {
  if (!Storage.exists(path)) return false;
  return Storage.readFileToString(kTag, path, kReadCap, out);
}

bool writeAtomically(const char* path, const std::string& text) {
  Storage.ensureDirectoryExists(kDir);
  const std::string temp = std::string(path) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite(kTag, temp.c_str(), file)) {
      LOG_ERR(kTag, "cannot write %s", temp.c_str());
      return false;
    }
    if (file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) != text.size()) {
      LOG_ERR(kTag, "short write to %s", temp.c_str());
      file.close();
      Storage.remove(temp.c_str());
      return false;
    }
  }
  Storage.remove(path);
  if (!Storage.rename(temp.c_str(), path)) {
    LOG_ERR(kTag, "cannot rename %s into place", temp.c_str());
    return false;
  }
  return true;
}

}  // namespace

std::vector<Event> Library::loadEvents() const {
  std::string text;
  if (!readWhole(kEvents, text)) return {};
  std::vector<Event> events = parseEvents(text);
  sortEvents(events);
  return events;
}

bool Library::saveEvents(const std::vector<Event>& events) const {
  return writeAtomically(kEvents, serializeEvents(events));
}

Settings Library::loadSettings() const {
  std::string text;
  if (!readWhole(kSettings, text)) return Settings{};
  return parseSettings(text);
}

bool Library::saveSettings(const Settings& settings) const {
  return writeAtomically(kSettings, serializeSettings(settings));
}

Meta Library::loadMeta() const {
  std::string text;
  if (!readWhole(kMeta, text)) return Meta{};
  return parseMeta(text);
}

bool Library::saveMeta(const Meta& meta) const { return writeAtomically(kMeta, serializeMeta(meta)); }

bool Library::loadAsleep(Asleep& out) const {
  std::string text;
  if (!readWhole(kAsleep, text)) return false;
  return parseAsleep(text, out);
}

bool Library::saveAsleep(const Asleep& asleep) const { return writeAtomically(kAsleep, serializeAsleep(asleep)); }

void Library::clearAsleep() const { Storage.remove(kAsleep); }

void Library::forget() const {
  Storage.remove(kEvents);
  Storage.remove(kMeta);
}

}  // namespace gcal
