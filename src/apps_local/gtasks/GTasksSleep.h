#pragma once

// A Google Tasks list on the sleep screen (Settings > Sleep screen > Tasks).
//
// Drawn live when the device goes to sleep, from the card as last synced, in
// the app's own look, so ticking a task and sleeping shows it ticked. The
// choice is /.crosspoint/gtasks/asleep.cfg (gtasks::Asleep), which the app's
// settings write together with the sleep screen setting.

class GfxRenderer;
namespace freeink {
namespace ui {
class GfxRendererTarget;
}
}  // namespace freeink

namespace gtasks {

// Draws the chosen list into the renderer's buffer and returns true; the
// caller puts it on the panel. False, having drawn nothing worth keeping,
// when no list is chosen or the card no longer has it, so the caller can fall
// back to the default sleep screen.
bool drawAsleep(GfxRenderer& renderer);

// Binds gtasksui::kTaskFont, the face a task's title is drawn in. Every target
// that draws the list calls this, awake or asleep.
void bindTaskFont(freeink::ui::GfxRendererTarget& target);

}  // namespace gtasks
