#include "AppLibrary.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "../../network/HttpDownloader.h"

namespace appstore {
namespace {

constexpr size_t kMaxManifestBytes = 16 * 1024;
constexpr size_t kHashChunk = 1024;
// Kept free beyond the package itself: the reader caches on this card too.
constexpr uint64_t kSpareBytes = 1024 * 1024;

std::string appDir(const std::string& id) { return std::string(kAppsDir) + "/" + id; }
std::string versionDir(const std::string& id, const std::string& version) { return appDir(id) + "/v-" + version; }

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

std::string readSmall(const std::string& path, const size_t cap) {
  std::string out;
  if (!Storage.exists(path.c_str())) return out;
  if (!Storage.readFileToString("APPSTORE", path, cap, out)) out.clear();
  return out;
}

// Beside the file and renamed over it.
bool writeAtomically(const std::string& path, const std::string& text) {
  const std::string part = path + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("APPSTORE", part.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(part.c_str(), path.c_str())) {
    Storage.remove(part.c_str());
    return false;
  }
  return true;
}

bool hashFile(const std::string& path, const PackageFile& expected, std::string& error) {
  HalFile file;
  if (!Storage.openFileForRead("APPSTORE", path.c_str(), file)) {
    error = "A downloaded file went missing.";
    return false;
  }
  if (file.size() != expected.size) {
    error = expected.name + " arrived at the wrong size. Try again.";
    return false;
  }
  auto chunk = makeUniqueNoThrow<uint8_t[]>(kHashChunk);
  if (!chunk) {
    error = "There is not enough memory to check the download.";
    return false;
  }
  Sha256 hash;
  for (;;) {
    const int got = file.read(chunk.get(), kHashChunk);
    if (got < 0) {
      error = "A downloaded file could not be read back.";
      return false;
    }
    if (got == 0) break;
    hash.update(chunk.get(), static_cast<size_t>(got));
  }
  if (hash.hex() != expected.sha256) {
    error = expected.name + " does not match its checksum. The download was damaged or changed. Try again.";
    return false;
  }
  return true;
}

// The version folders of `id` other than `keep`, so an update does not leave
// the old program behind.
void removeOtherVersions(const std::string& id, const std::string& keep) {
  const std::string dir = appDir(id);
  std::vector<std::string> stale;
  {
    HalFile root = Storage.open(dir.c_str());
    if (!root || !root.isDirectory()) return;
    char name[64];
    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      entry.getName(name, sizeof(name));
      const std::string n = name;
      if (entry.isDirectory() && n.rfind("v-", 0) == 0 && n != "v-" + keep) stale.push_back(dir + "/" + n);
    }
  }
  for (const std::string& path : stale) Storage.removeDir(path.c_str());
}

}  // namespace

std::vector<InstalledApp> listInstalled(const bool hasTouch, const bool hasButtons) {
  std::vector<InstalledApp> out;
  std::vector<std::string> ids;
  {
    if (!Storage.exists(kAppsDir)) return out;
    HalFile root = Storage.open(kAppsDir);
    if (!root || !root.isDirectory()) return out;
    char name[64];
    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (entry.isDirectory() && name[0] != '.' && validAppId(name)) ids.push_back(name);
    }
  }
  out.reserve(ids.size());
  for (const std::string& id : ids) {
    const std::string version = trim(readSmall(appDir(id) + "/current", 32));
    AppVersion parsed;
    if (!parseAppVersion(version, parsed)) {
      LOG_ERR("APPSTORE", "%s: no current version", id.c_str());
      continue;
    }
    InstalledApp app;
    app.packageDir = versionDir(id, version);
    app.dataDir = appDir(id) + "/data";
    std::string error;
    if (!parseManifest(readSmall(app.packageDir + "/manifest.json", kMaxManifestBytes), app.manifest, error)) {
      LOG_ERR("APPSTORE", "%s: %s", id.c_str(), error.c_str());
      continue;
    }
    if (app.manifest.id != id || app.manifest.version != version) {
      LOG_ERR("APPSTORE", "%s: manifest names %s %s", id.c_str(), app.manifest.id.c_str(),
              app.manifest.version.c_str());
      continue;
    }
    app.problem = launchProblem(app.manifest, hasTouch, hasButtons);
    out.push_back(std::move(app));
  }
  std::sort(out.begin(), out.end(),
            [](const InstalledApp& a, const InstalledApp& b) { return a.manifest.name < b.manifest.name; });
  return out;
}

std::string installedVersion(const std::vector<InstalledApp>& apps, const std::string& id) {
  for (const InstalledApp& app : apps) {
    if (app.manifest.id == id) return app.manifest.version;
  }
  return "";
}

bool install(const CatalogEntry& entry, const std::string& server, const bool hasTouch, const bool hasButtons,
             const Progress progress, void* ctx, std::string& error) {
  if (!entry.compatible || entry.version.empty()) {
    error = entry.reason.empty() ? "No version of this app runs on this device." : entry.reason;
    return false;
  }

  // The manifest is the authority, not the catalog: it is what gets installed
  // and what every later launch is checked against.
  std::string text;
  if (!HttpDownloader::fetchUrl(resolveUrl(server, entry.manifestUrl), text)) {
    error = "Could not reach the store to fetch the app's manifest.";
    return false;
  }
  if (text.size() > kMaxManifestBytes) {
    error = "The app's manifest is too large.";
    return false;
  }
  Manifest manifest;
  if (!parseManifest(text, manifest, error)) return false;
  if (manifest.id != entry.id || manifest.version != entry.version) {
    error = "The store sent a manifest for a different app or version.";
    return false;
  }
  error = launchProblem(manifest, hasTouch, hasButtons);
  if (!error.empty()) return false;

  uint64_t total = 0;
  std::vector<std::string> urls;
  urls.reserve(manifest.files.size());
  for (const PackageFile& file : manifest.files) {
    total += file.size;
    std::string url;
    for (size_t i = 0; i < entry.files.size() && i < entry.fileUrls.size(); ++i) {
      if (entry.files[i].name == file.name) url = entry.fileUrls[i];
    }
    if (url.empty()) {
      error = "The store did not say where to download " + file.name + ".";
      return false;
    }
    urls.push_back(resolveUrl(server, url));
  }
  uint64_t free = 0;
  if (Storage.freeBytes(free) && free < total + kSpareBytes) {
    error = "There is not enough room on the card.";
    return false;
  }

  const std::string staging = std::string(kStagingDir) + "/" + manifest.id;
  if (Storage.exists(staging.c_str())) Storage.removeDir(staging.c_str());
  if (!Storage.ensureDirectoryExists(staging.c_str())) {
    error = "Could not write to the card.";
    return false;
  }

  struct Pump {
    Progress progress;
    void* ctx;
    uint64_t before;
    uint64_t total;
    bool cancel;
  } pump{progress, ctx, 0, total, false};

  for (size_t i = 0; i < manifest.files.size(); ++i) {
    const PackageFile& file = manifest.files[i];
    const std::string dest = staging + "/" + file.name;
    const auto err = HttpDownloader::downloadToFile(
        urls[i], dest,
        [&pump](const size_t done, const size_t) {
          if (pump.progress != nullptr &&
              !pump.progress(pump.ctx, static_cast<uint32_t>(pump.before + done), static_cast<uint32_t>(pump.total))) {
            pump.cancel = true;
          }
        },
        &pump.cancel);
    if (err != HttpDownloader::OK) {
      Storage.removeDir(staging.c_str());
      error = err == HttpDownloader::ABORTED      ? "Cancelled."
              : err == HttpDownloader::FILE_ERROR ? "Could not write the download to the card."
                                                  : "The download of " + file.name + " failed. Try again.";
      return false;
    }
    if (!hashFile(dest, file, error)) {
      Storage.removeDir(staging.c_str());
      return false;
    }
    pump.before += file.size;
  }
  if (!writeAtomically(staging + "/manifest.json", text)) {
    Storage.removeDir(staging.c_str());
    error = "Could not write to the card.";
    return false;
  }

  const std::string target = versionDir(manifest.id, manifest.version);
  if (!Storage.ensureDirectoryExists(appDir(manifest.id).c_str())) {
    Storage.removeDir(staging.c_str());
    error = "Could not write to the card.";
    return false;
  }
  if (Storage.exists(target.c_str())) Storage.removeDir(target.c_str());
  if (!Storage.rename(staging.c_str(), target.c_str())) {
    Storage.removeDir(staging.c_str());
    error = "Could not move the app into place.";
    return false;
  }
  // The switch: until this lands, the previous version is the one that runs.
  if (!writeAtomically(appDir(manifest.id) + "/current", manifest.version + "\n")) {
    error = "Could not finish the install.";
    return false;
  }
  removeOtherVersions(manifest.id, manifest.version);
  LOG_INF("APPSTORE", "installed %s %s", manifest.id.c_str(), manifest.version.c_str());
  return true;
}

bool uninstall(const std::string& id, const bool keepData, std::string& error) {
  if (!validAppId(id)) {
    error = "That is not an app id.";
    return false;
  }
  const std::string dir = appDir(id);
  if (!Storage.exists(dir.c_str())) return true;
  if (!keepData) {
    if (!Storage.removeDir(dir.c_str())) {
      error = "Could not remove it from the card.";
      return false;
    }
    return true;
  }
  const std::string current = dir + "/current";
  if (Storage.exists(current.c_str())) Storage.remove(current.c_str());
  removeOtherVersions(id, "");
  return true;
}

std::string configuredServer() {
  const std::string configured = serverFromConfig(readSmall(kConfigPath, 1024));
  if (!configured.empty()) return configured;
#ifdef CROSSPLAY_APPSTORE_URL
  return CROSSPLAY_APPSTORE_URL;
#else
  return "";
#endif
}

bool saveServer(const std::string& server) {
  // Refused rather than escaped: no store address needs either character.
  if (server.find_first_of("\"\\") != std::string::npos) return false;
  Storage.ensureDirectoryExists("/.crosspoint");
  return writeAtomically(kConfigPath, "{\"server\": \"" + server + "\"}\n");
}

}  // namespace appstore
