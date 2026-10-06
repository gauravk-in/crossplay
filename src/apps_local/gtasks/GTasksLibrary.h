#pragma once

// Google Tasks on the card, under /.crosspoint/gtasks/:
//
//   auth.cfg      the sign-in service's device token and the Google address
//   bridge.cfg    optional: host=<sign-in service>, overriding the built-in one
//   tasks.tsv     the list as last synced, plus ticks not yet sent
//   settings.cfg  how often to poll on the charger
//   meta.cfg      the list's title and when it last synced
//
// Beside the reader's own state, so clearing `.crosspoint/` clears this too.
// Every write goes through a .part and a rename, so a reset mid-write leaves
// the previous file rather than half of a new one.

#include <cstdint>
#include <string>
#include <vector>

#include "GTasksCore.h"

namespace gtasks {

struct Meta {
  std::string listTitle;
  int64_t lastSyncAt = 0;  // epoch seconds, 0 when never or the clock was unset
};

class Library {
 public:
  Credentials loadCredentials() const;
  bool saveCredentials(const Credentials& creds) const;
  // "" when bridge.cfg is absent or unusable.
  std::string loadBridgeHost() const;
  // Forgets the account: the token, the cached list and its title. Settings
  // stay, because they describe this reader rather than the account.
  void signOut() const;

  std::vector<Task> loadTasks() const;
  bool saveTasks(const std::vector<Task>& tasks) const;

  Settings loadSettings() const;
  bool saveSettings(const Settings& settings) const;

  Meta loadMeta() const;
  bool saveMeta(const Meta& meta) const;
};

}  // namespace gtasks
