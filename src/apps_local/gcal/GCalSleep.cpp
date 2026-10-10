#include "GCalSleep.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../../components/themes/BaseTheme.h"  // Rect, which ToyboxTheme.h uses and does not include
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxTheme.h"
#include "GCalCore.h"
#include "GCalLibrary.h"
#include "GCalScreens.h"

namespace fui = freeink::ui;

namespace gcal {

bool drawAsleep(GfxRenderer& renderer) {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  if (now < 1700000000) {
    LOG_INF("GCAL", "sleep screen: the clock is not set");
    return false;
  }
  Library library;
  const std::vector<Event> events = library.loadEvents();
  const int64_t today = toLocal(now).day;
  const std::vector<Item> items = buildSchedule(events, today, today + kDaysAhead, today);

  toybox::ensureFonts(renderer);
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();

  const std::vector<int> heights = gcalui::itemHeights(target, items);
  const int shown = fitFrom(heights, 0, gcalui::pageHeight(device, true));

  // When it was last synced, so a stale page says so.
  char status[16] = "";
  const Meta meta = library.loadMeta();
  if (meta.lastSyncAt > 0) {
    const time_t when = static_cast<time_t>(meta.lastSyncAt);
    struct tm parts{};
    localtime_r(&when, &parts);
    std::snprintf(status, sizeof(status), "%02d:%02d", parts.tm_hour, parts.tm_min);
  }
  const std::string title = dayLabel(today);

  gcalui::ScheduleModel model;
  model.title = title.c_str();
  model.status = status[0] != '\0' ? status : nullptr;
  model.items = shown > 0 ? items.data() : nullptr;
  model.heights = shown > 0 ? heights.data() : nullptr;
  model.count = shown;
  model.events = events.empty() ? nullptr : events.data();
  model.eventCount = static_cast<int>(events.size());
  model.today = today;
  model.asleep = true;
  model.emptyHeadline = "NOTHING PLANNED";
  model.emptyMessage = "Nothing on the calendar as last synced.";

  const fui::InputSnapshot noInput{};
  toybox::Interactions interactions;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);
  gcalui::buildSchedule(screen, model);
  LOG_INF("GCAL", "sleep screen: %d of %d rows from %s", shown, static_cast<int>(items.size()), title.c_str());
  return true;
}

}  // namespace gcal
