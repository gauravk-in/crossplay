#pragma once

// Today's schedule on the sleep screen (Settings > Sleep screen > Calendar).
//
// Drawn live when the device goes to sleep, from the card as last synced and
// the reader's own clock, so the page always starts on the day it fell asleep.
// The app's settings turn it on together with the sleep screen setting and
// keep what it replaced in /.crosspoint/gcal/asleep.cfg.

class GfxRenderer;

namespace gcal {

// Draws the schedule from today into the renderer's buffer and returns true;
// the caller puts it on the panel. False when the clock is not set, so the
// caller can fall back to the default sleep screen rather than show a page
// that does not know what today is.
bool drawAsleep(GfxRenderer& renderer);

}  // namespace gcal
