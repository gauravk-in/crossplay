#pragma once

// Google Tasks on the card, under /.crosspoint/gtasks/:
//
//   auth.cfg      the Google refresh token and address (the reader writes it)
//   client.cfg    the Google Cloud desktop client to sign in as, unless the
//                 build carries one (GTASKS_CLIENT_ID / GTASKS_CLIENT_SECRET)
//   lists.tsv     every list on the account, as last synced
//   list-<id>.tsv one list's tasks as last synced, plus ticks not yet sent
//   settings.cfg  how often to poll on the charger
//   meta.cfg      which list is open, and when the lists last synced
//   asleep.cfg    the list on the sleep screen, if any
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
  std::string currentList;  // "" until the first sync names one
  int64_t lastSyncAt = 0;   // epoch seconds, 0 when never or the clock was unset
};

class Library {
 public:
  Credentials loadCredentials() const;
  bool saveCredentials(const Credentials& creds) const;
  // client.cfg's, else the one built in, else an incomplete Client.
  Client loadClient() const;
  // Forgets the account: the token, every cached list and the sleep-screen
  // choice. Settings stay, because they describe this reader rather than the
  // account.
  void signOut() const;

  std::vector<TaskList> loadLists() const;
  bool saveLists(const std::vector<TaskList>& lists) const;

  std::vector<Task> loadTasks(const std::string& listId) const;
  bool saveTasks(const std::string& listId, const std::vector<Task>& tasks) const;
  void removeTasks(const std::string& listId) const;

  bool loadAsleep(Asleep& out) const;
  bool saveAsleep(const Asleep& asleep) const;
  void clearAsleep() const;

  Settings loadSettings() const;
  bool saveSettings(const Settings& settings) const;

  Meta loadMeta() const;
  bool saveMeta(const Meta& meta) const;
};

}  // namespace gtasks
