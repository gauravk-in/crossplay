#pragma once

// Downloadable apps on the card: what is installed, installing, updating and
// removing. The rules (what a valid manifest is, what runs here) are in
// AppStoreCore; this file is the storage and the network around them.
//
//   /.crosspoint/apps/<id>/current          the installed version, e.g. "1.2.0"
//   /.crosspoint/apps/<id>/v-<version>/     manifest.json and the package files
//   /.crosspoint/apps/<id>/data/            the app's own files, kept on update
//   /.crosspoint/apps/.staging/             a download in progress
//
// An install downloads into staging, checks every file against the manifest's
// size and SHA-256, moves the folder into place and only then rewrites
// `current`. A power cut at any point leaves the old version running, or no
// version and a leftover staging folder that the next install clears.

#include <string>
#include <vector>

#include "AppStoreCore.h"

namespace appstore {

constexpr const char* kAppsDir = "/.crosspoint/apps";
constexpr const char* kStagingDir = "/.crosspoint/apps/.staging";
constexpr const char* kConfigPath = "/.crosspoint/appstore.json";

struct InstalledApp {
  Manifest manifest;
  std::string packageDir;
  std::string dataDir;
  // Why it will not start on this device, or "" when it will. Set for an app
  // whose API this firmware no longer runs, which is what a firmware update
  // with a break does to apps that were fine yesterday.
  std::string problem;
};

// Every installed app, sorted by name. An app folder whose manifest is missing
// or broken is skipped and logged rather than listed: there is nothing to say
// about it that the person could act on, short of reinstalling it.
std::vector<InstalledApp> listInstalled(bool hasTouch, bool hasButtons);

// The installed version of `id`, or "".
std::string installedVersion(const std::vector<InstalledApp>& apps, const std::string& id);

// Called as bytes arrive; return false to cancel.
using Progress = bool (*)(void* ctx, uint32_t done, uint32_t total);

// Installs or updates to `entry.version`. On failure `error` is a sentence for
// the person holding the device, and whatever was installed before is untouched.
bool install(const CatalogEntry& entry, const std::string& server, bool hasTouch, bool hasButtons,
             Progress progress, void* ctx, std::string& error);

// Removes the app, and its data unless `keepData`.
bool uninstall(const std::string& id, bool keepData, std::string& error);

// The store server's address: /.crosspoint/appstore.json, else the
// CROSSPLAY_APPSTORE_URL build flag, else "".
std::string configuredServer();
bool saveServer(const std::string& server);

}  // namespace appstore
