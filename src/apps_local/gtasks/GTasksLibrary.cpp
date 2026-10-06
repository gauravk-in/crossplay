#include "GTasksLibrary.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>

namespace gtasks {
namespace {

constexpr const char* kTag = "GTASKS";
constexpr const char* kDir = "/.crosspoint/gtasks";
constexpr const char* kAuth = "/.crosspoint/gtasks/auth.cfg";
constexpr const char* kTasks = "/.crosspoint/gtasks/tasks.tsv";
constexpr const char* kSettings = "/.crosspoint/gtasks/settings.cfg";
constexpr const char* kMeta = "/.crosspoint/gtasks/meta.cfg";
constexpr const char* kBridge = "/.crosspoint/gtasks/bridge.cfg";

// Larger than any of these files has reason to be: five pages of a hundred
// tasks is well under it. A bigger file is not ours, and reading it whole
// into RAM is how a stray file on the card becomes an out-of-memory.
constexpr size_t kReadCap = 256 * 1024;

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

Credentials Library::loadCredentials() const {
  std::string text;
  if (!readWhole(kAuth, text)) return Credentials{};
  const Credentials creds = parseCredentials(text);
  if (!creds.complete()) LOG_ERR(kTag, "auth.cfg is on the card but holds no device token");
  return creds;
}

bool Library::saveCredentials(const Credentials& creds) const {
  return writeAtomically(kAuth, serializeCredentials(creds));
}

std::string Library::loadBridgeHost() const {
  std::string text;
  if (!readWhole(kBridge, text)) return std::string();
  const std::string host = parseBridgeHost(text);
  if (host.empty()) LOG_ERR(kTag, "bridge.cfg has no usable host=; using the built-in one");
  return host;
}

void Library::signOut() const {
  Storage.remove(kAuth);
  Storage.remove(kTasks);
  Storage.remove(kMeta);
}

std::vector<Task> Library::loadTasks() const {
  std::string text;
  if (!readWhole(kTasks, text)) return {};
  std::vector<Task> tasks = parseTasks(text);
  sortForDisplay(tasks);
  return tasks;
}

bool Library::saveTasks(const std::vector<Task>& tasks) const { return writeAtomically(kTasks, serializeTasks(tasks)); }

Settings Library::loadSettings() const {
  std::string text;
  if (!readWhole(kSettings, text)) return Settings{};
  return parseSettings(text);
}

bool Library::saveSettings(const Settings& settings) const {
  return writeAtomically(kSettings, serializeSettings(settings));
}

Meta Library::loadMeta() const {
  Meta meta;
  std::string text;
  if (!readWhole(kMeta, text)) return meta;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    if (line.rfind("title=", 0) == 0) meta.listTitle = line.substr(6);
    if (line.rfind("synced=", 0) == 0) meta.lastSyncAt = std::atoll(line.c_str() + 7);
  }
  return meta;
}

bool Library::saveMeta(const Meta& meta) const {
  std::string title = meta.listTitle;
  for (char& c : title) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return writeAtomically(kMeta, "title=" + title + "\nsynced=" + std::to_string(meta.lastSyncAt) + "\n");
}

}  // namespace gtasks
