#include "GTasksSleep.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../../components/themes/BaseTheme.h"  // Rect, which ToyboxTheme.h uses and does not include
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxTheme.h"
#include "GTasksCore.h"
#include "GTasksLibrary.h"
#include "GTasksScreens.h"

namespace fui = freeink::ui;

namespace gtasks {

bool drawAsleep(GfxRenderer& renderer) {
  Library library;
  Asleep choice;
  if (!library.loadAsleep(choice)) {
    LOG_INF("GTASKS", "sleep screen: no list chosen");
    return false;
  }
  std::string title;
  for (const TaskList& l : library.loadLists()) {
    if (l.id == choice.listId) title = l.title;
  }
  if (title.empty()) {
    LOG_INF("GTASKS", "sleep screen: list %s is no longer on the card", choice.listId.c_str());
    return false;
  }
  for (char& c : title) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  const std::vector<Task> tasks = library.loadTasks(choice.listId);

  toybox::ensureFonts(renderer);
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();

  // A sleep screen cannot turn a page, so what does not fit is counted in the
  // page label's place, as Notes does: "1 / 3" says there is more than this.
  const int count = static_cast<int>(tasks.size());
  const int single = gtasksui::listCapacity(target, device, false, true);
  const bool paged = count > single;
  const int perPage = paged ? gtasksui::listCapacity(target, device, true, true) : single;
  const int shown = count < perPage ? count : perPage;

  std::vector<std::string> dues;
  dues.reserve(static_cast<size_t>(shown));
  for (int i = 0; i < shown; ++i) dues.push_back(dueLabel(tasks[static_cast<size_t>(i)].due));
  std::vector<gtasksui::Row> rows;
  rows.reserve(static_cast<size_t>(shown));
  for (int i = 0; i < shown; ++i) {
    const Task& t = tasks[static_cast<size_t>(i)];
    gtasksui::Row row;
    row.title = t.title.c_str();
    row.due = dues[static_cast<size_t>(i)].empty() ? nullptr : dues[static_cast<size_t>(i)].c_str();
    row.checked = t.pending;
    row.child = isChild(t, tasks);
    rows.push_back(row);
  }

  char status[16] = "";
  const int open = count - pendingCount(tasks);
  std::snprintf(status, sizeof(status), "%d OPEN", open);
  char label[16] = "";
  if (paged) std::snprintf(label, sizeof(label), "1 / %d", (count + perPage - 1) / perPage);

  gtasksui::ListModel model;
  model.title = title.c_str();
  model.status = status;
  model.rows = rows.empty() ? nullptr : rows.data();
  model.count = static_cast<int>(rows.size());
  model.pageLabel = paged ? label : nullptr;
  model.asleep = true;

  const fui::InputSnapshot noInput{};
  toybox::Interactions interactions;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);
  gtasksui::buildList(screen, model);
  LOG_INF("GTASKS", "sleep screen: '%s', %d of %d task(s)", title.c_str(), shown, count);
  return true;
}

}  // namespace gtasks
