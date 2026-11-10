#pragma once

// Google Calendar on the card, under /.crosspoint/gcal/:
//
//   events.tsv    every event in the synced window, as last synced
//   settings.cfg  how often to poll on the charger
//   meta.cfg      when the card last synced
//   asleep.cfg    present while Calendar is the sleep screen: what it replaced
//
// The Google account itself is Tasks' auth.cfg and client.cfg (gtasks::Library).
// Every write goes through a .part and a rename, so a reset mid-write leaves
// the previous file rather than half of a new one.

#include <vector>

#include "GCalCore.h"

namespace gcal {

class Library {
 public:
  std::vector<Event> loadEvents() const;
  bool saveEvents(const std::vector<Event>& events) const;

  Settings loadSettings() const;
  bool saveSettings(const Settings& settings) const;

  Meta loadMeta() const;
  bool saveMeta(const Meta& meta) const;

  bool loadAsleep(Asleep& out) const;
  bool saveAsleep(const Asleep& asleep) const;
  void clearAsleep() const;

  // The calendar and its sync time. Settings stay: they describe this reader.
  void forget() const;
};

}  // namespace gcal
