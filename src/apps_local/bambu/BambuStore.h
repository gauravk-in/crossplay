#pragma once

// The Bambu monitor's files on the card. Hidden under /.crosspoint, because one
// of them holds the printer's access code.

#include <cstddef>
#include <string>

namespace bambu {
namespace store {

constexpr const char* kDir = "/.crosspoint/bambu";
constexpr const char* kConfigPath = "/.crosspoint/bambu/config.txt";
// The Bambu account's token, for the cloud. Never served to the phone.
constexpr const char* kCloudPath = "/.crosspoint/bambu/cloud.txt";
// The last state the printer reported, as a report, so the monitor opens on it.
constexpr const char* kSnapshotPath = "/.crosspoint/bambu/last.json";
constexpr size_t kSnapshotMax = 8 * 1024;

bool begin();
bool exists(const char* path);
// Empty when the file is missing or past `cap`.
std::string read(const char* path, size_t cap);
// Through a .part file, so a cut-off write never replaces a good one.
bool write(const char* path, const std::string& text);
bool remove(const char* path);

}  // namespace store
}  // namespace bambu
