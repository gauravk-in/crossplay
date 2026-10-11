#include "PrompterStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "PrompterCore.h"

namespace prompter {
namespace store {

bool begin() {
  if (!Storage.ensureDirectoryExists(kSettingsDir) || !Storage.ensureDirectoryExists(kScriptDir)) {
    LOG_ERR("PROMPT", "could not make the prompter folders");
    return false;
  }
  return true;
}

std::string read(const char* path, const size_t cap) {
  std::string out;
  if (!Storage.exists(path)) return out;
  if (!Storage.readFileToString("PROMPT", path, cap, out)) out.clear();
  return out;
}

bool write(const char* path, const std::string& text) {
  const std::string part = std::string(path) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("PROMPT", part.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(part.c_str(), path)) {
    Storage.remove(part.c_str());
    LOG_ERR("PROMPT", "could not replace %s", path);
    return false;
  }
  return true;
}

std::string scriptPath(const std::string& name) { return std::string(kScriptDir) + "/" + name; }

std::vector<std::string> listScripts() {
  std::vector<std::string> out;
  const std::vector<String> files = Storage.listFiles(kScriptDir, kMaxScripts * 2);
  out.reserve(files.size());
  for (const String& f : files) {
    std::string name(f.c_str());
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    // Only names the phone page could also have made, so every row can be
    // opened, replaced and deleted by name.
    if (isScriptName(name)) out.push_back(name);
    if (static_cast<int>(out.size()) >= kMaxScripts) break;
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](const char x, const char y) {
      const char lx = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
      const char ly = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
      return lx < ly;
    });
  });
  return out;
}

bool removeScript(const std::string& name) {
  if (!isScriptName(name)) return false;
  return Storage.remove(scriptPath(name).c_str());
}

}  // namespace store
}  // namespace prompter
