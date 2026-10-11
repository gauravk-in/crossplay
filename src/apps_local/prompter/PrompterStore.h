#pragma once

// Prompter's files on the card. Scripts are plain .txt files in /prompter, so a
// script can arrive from the phone page or be copied over USB, and both land
// in the same list. The settings are the reader's own and live with the
// others under /.crosspoint.

#include <string>
#include <vector>

namespace prompter {
namespace store {

constexpr const char* kScriptDir = "/prompter";
constexpr const char* kSettingsDir = "/.crosspoint/prompter";
constexpr const char* kSettingsPath = "/.crosspoint/prompter/settings.txt";
constexpr int kMaxScripts = 100;

bool begin();
std::string read(const char* path, size_t cap);
// Written to a .part file and moved over the old one, so a pulled card never
// leaves half a script.
bool write(const char* path, const std::string& text);

std::string scriptPath(const std::string& name);
// Script file names, sorted, at most kMaxScripts.
std::vector<std::string> listScripts();
bool removeScript(const std::string& name);

}  // namespace store
}  // namespace prompter
