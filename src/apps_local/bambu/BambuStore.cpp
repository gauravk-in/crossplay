#include "BambuStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace bambu {
namespace store {

bool begin() {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("BAMBU", "could not make %s", kDir);
    return false;
  }
  return true;
}

bool exists(const char* path) { return Storage.exists(path); }

std::string read(const char* path, const size_t cap) {
  std::string out;
  if (!Storage.exists(path)) return out;
  if (!Storage.readFileToString("BAMBU", path, cap, out)) out.clear();
  return out;
}

bool write(const char* path, const std::string& text) {
  const std::string part = std::string(path) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("BAMBU", part.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(part.c_str(), path)) {
    Storage.remove(part.c_str());
    LOG_ERR("BAMBU", "could not replace %s", path);
    return false;
  }
  return true;
}

bool remove(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

}  // namespace store
}  // namespace bambu
