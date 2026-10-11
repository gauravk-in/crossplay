#pragma once

// The CrossPlay app API: everything a downloadable app may ask of the firmware.
//
// A downloadable app is a WebAssembly module. It imports the functions below
// from the module "crossplay" and exports the four cp_app_* entry points at the
// bottom of this file. Nothing else crosses the boundary: no firmware headers,
// no C++ types, no pointers into firmware memory. That is what lets one app
// file run on any firmware whose API is compatible, whichever apps that
// firmware was built with.
//
// VERSIONING. This header describes API CP_API_VERSION_MAJOR.CP_API_VERSION_MINOR.
// An app records the version it was built against in its manifest ("api").
// A firmware runs an app when
//
//     firmware.compatibleFrom <= app.api <= firmware.api
//
// compared as (major, minor). Additions bump the minor and keep compatibleFrom;
// anything that changes or removes existing behaviour raises compatibleFrom.
// docs/app-api/compat.json is the record of every version and every break.
//
// RULES FOR CHANGING THIS FILE (see docs/app-api/README.md):
//   - Never change the signature or meaning of a function that has shipped.
//     Add a new function (cp_foo2) and bump the minor instead.
//   - Enum values are append-only. A firmware that meets a value it does not
//     know treats it as the documented fallback, never as an error.
//   - Every change gets a compat.json entry; host-tests/appapi checks that the
//     firmware's import table, this header and compat.json agree.

#include <stddef.h>
#include <stdint.h>

#define CP_API_VERSION_MAJOR 1
#define CP_API_VERSION_MINOR 0
// The oldest API whose apps a firmware with this API still runs. Raised only by
// a breaking change, and recorded in compat.json when it is.
#define CP_API_COMPATIBLE_FROM_MAJOR 1
#define CP_API_COMPATIBLE_FROM_MINOR 0

#if defined(__wasm__)
#define CP_IMPORT(name) __attribute__((import_module("crossplay"), import_name(#name)))
#define CP_EXPORT(name) __attribute__((export_name(#name)))
#else
#define CP_IMPORT(name)
#define CP_EXPORT(name)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Values ----------------------------------------------------------------

// Colours, as FreeInkUI names them. The panel is black and white; the greys
// are dithers.
enum {
  CP_COLOR_TRANSPARENT = 0,
  CP_COLOR_WHITE = 1,
  CP_COLOR_LIGHT_GRAY = 2,
  CP_COLOR_DARK_GRAY = 3,
  CP_COLOR_BLACK = 4,
};

// How a colour is laid down. Unknown kinds draw nothing.
enum {
  CP_PAINT_NONE = 0,
  CP_PAINT_SOLID = 1,
  CP_PAINT_DITHER = 2,
};

// A paint is (kind << 8) | colour.
#define CP_PAINT(kind, color) (((kind) << 8) | (color))

// Typefaces, by role. The firmware maps each to whatever font it carries for
// that role; an id it does not know falls back to CP_FACE_UI.
enum {
  CP_FACE_DISPLAY = 0,       // header bands and big labels (Jersey 30)
  CP_FACE_UI = 1,            // rows and labels (Jersey 20)
  CP_FACE_TILE = 2,          // dense tile text
  CP_FACE_BUTTON = 3,        // button labels, footnotes
  CP_FACE_SERIF_TILE = 4,    // Instrument Serif, tile size
  CP_FACE_SERIF_TITLE = 5,   // Instrument Serif, title size
  CP_FACE_SERIF_SMALL = 6,   // Instrument Serif, small
  CP_FACE_READING = 7,       // reading serif, body
  CP_FACE_READING_BOLD = 8,  // reading serif, bold
  CP_FACE_READING_SMALL = 9,
  CP_FACE_READING_BOLD_SMALL = 10,
  CP_FACE_HUGE = 11,   // a word read across a room
  CP_FACE_LARGE = 12,  // one step down from huge
};

// Text style, packed into one int. Bits:
//   0-1 align (0 left, 1 centre, 2 right)   2-4 colour   5-12 max lines (0 = 1)
//   13 bold   14 inverted   15-16 rotation (0 none, 1 cw, 2 180, 3 ccw)
#define CP_TEXT_STYLE(align, color, maxLines, bold, inverted, rotation)                                     \
  ((int32_t)(((align) & 3) | (((color) & 7) << 2) | (((maxLines) & 0xFF) << 5) | (((bold) ? 1 : 0) << 13) | \
             (((inverted) ? 1 : 0) << 14) | (((rotation) & 3) << 15)))

// Bitmaps. MASK1 and BW1 are one bit per pixel, rows padded to whole bytes,
// most significant bit first; a set bit is ink in MASK1, white in BW1.
enum {
  CP_BITMAP_MASK1 = 0,
  CP_BITMAP_BW1 = 1,
};
enum {
  CP_FIT_CENTER = 0,
  CP_FIT_STRETCH = 1,
  CP_FIT_CONTAIN = 2,
  CP_FIT_COVER = 3,
};

// What the next display update should be. A fast update ghosts; a half or full
// one flashes. Unknown values are a fast update.
enum {
  CP_REFRESH_FAST = 0,
  CP_REFRESH_HALF = 1,
  CP_REFRESH_FULL = 2,
};

// Events delivered to cp_app_event.
enum {
  CP_EVENT_BACK = 1,    // Back button or the back swipe. Return 1 to keep the app open.
  CP_EVENT_TAP = 2,     // a = x, b = y, in logical pixels
  CP_EVENT_BUTTON = 3,  // a = CP_BUTTON_*, released
  CP_EVENT_SWIPE = 4,   // a = CP_SWIPE_*
  CP_EVENT_TIMER = 5,   // the cp_set_timer deadline passed
  CP_EVENT_RESUME = 6,  // back from sleep; the screen was redrawn by someone else
  CP_EVENT_SHOWN = 7,   // the frame the last cp_app_render drew is now on the panel
};
enum {
  CP_BUTTON_UP = 1,
  CP_BUTTON_DOWN = 2,
  CP_BUTTON_LEFT = 3,
  CP_BUTTON_RIGHT = 4,
  CP_BUTTON_CONFIRM = 5,
  CP_BUTTON_PAGE_BACK = 6,
  CP_BUTTON_PAGE_FORWARD = 7,
};
enum {
  CP_SWIPE_LEFT = 1,
  CP_SWIPE_RIGHT = 2,
  CP_SWIPE_UP = 3,
  CP_SWIPE_DOWN = 4,
};

// Filled by cp_device_info. Fields are append-only; the firmware writes at most
// `size` bytes, so an app built against an older struct still works.
typedef struct {
  int32_t size;   // the app sets this to sizeof(cp_device_info_t)
  int32_t width;  // logical pixels, current orientation
  int32_t height;
  int32_t hasTouch;    // 0 or 1
  int32_t hasButtons;  // 0 or 1
  int32_t safeTop;     // insets the bezel hides
  int32_t safeRight;
  int32_t safeBottom;
  int32_t safeLeft;
  int32_t minTouchSize;  // smallest comfortable tap target, in pixels
} cp_device_info_t;

typedef struct {
  int32_t width;
  int32_t height;
} cp_size_t;

typedef struct {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} cp_rect_t;

// --- System ----------------------------------------------------------------

// The firmware's API version, (major << 16) | minor.
CP_IMPORT(api_version) int32_t cp_api_version(void);
CP_IMPORT(device_info) void cp_device_info(cp_device_info_t* out);
// level: 0 error, 1 info, 2 debug.
CP_IMPORT(log) void cp_log(int32_t level, const char* text, int32_t len);
// Milliseconds since boot.
CP_IMPORT(now_ms) int64_t cp_now_ms(void);
// Seconds since 1970 UTC, or 0 when the clock has never been set.
CP_IMPORT(unix_time) int64_t cp_unix_time(void);
// The local offset from UTC in minutes, for turning cp_unix_time into a day.
CP_IMPORT(utc_offset_minutes) int32_t cp_utc_offset_minutes(void);
CP_IMPORT(random) uint32_t cp_random(void);

// Ask for cp_app_render to be called. Drawing happens only inside it.
CP_IMPORT(request_render) void cp_request_render(int32_t refresh);
// One-shot timer: CP_EVENT_TIMER arrives after `ms`. 0 cancels. A new call
// replaces the pending one. The device may sleep through a timer; it is
// delivered late rather than never.
CP_IMPORT(set_timer) void cp_set_timer(int32_t ms);
// Close the app after this call returns.
CP_IMPORT(exit) void cp_exit(void);

// --- Storage ---------------------------------------------------------------
// Each app has a private folder. Names are bare file names: up to 32
// characters of [A-Za-z0-9._-], no leading dot. Anything else fails.

// Bytes read (at most cap), or -1 if the file does not exist or cannot be read.
CP_IMPORT(file_read) int32_t cp_file_read(const char* name, int32_t nameLen, void* buf, int32_t cap);
// Size in bytes, or -1.
CP_IMPORT(file_size) int32_t cp_file_size(const char* name, int32_t nameLen);
// Replaces the whole file atomically: a power cut leaves the old or the new
// contents, never half. 0 on success, -1 on failure (including over quota).
CP_IMPORT(file_write) int32_t cp_file_write(const char* name, int32_t nameLen, const void* data, int32_t len);
CP_IMPORT(file_remove) int32_t cp_file_remove(const char* name, int32_t nameLen);

// --- Drawing ---------------------------------------------------------------
// Valid only inside cp_app_render; elsewhere they do nothing. Coordinates are
// logical pixels in the current orientation. These mirror FreeInkUI's
// DrawTarget, so the SDK's FreeInkUI components run on top of them unchanged.

CP_IMPORT(measure_text)
void cp_measure_text(int32_t face, const char* text, int32_t len, int32_t style, cp_size_t* out);
CP_IMPORT(line_height) int32_t cp_line_height(int32_t face);
// The visible ink of `text` relative to its line box.
CP_IMPORT(ink_bounds) void cp_ink_bounds(int32_t face, const char* text, int32_t len, int32_t style, cp_rect_t* out);
// corners: bit 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
CP_IMPORT(fill)
void cp_fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t paint, int32_t radius, int32_t corners);
CP_IMPORT(stroke)
void cp_stroke(int32_t x, int32_t y, int32_t w, int32_t h, int32_t paint, int32_t width, int32_t radius,
               int32_t corners);
CP_IMPORT(line) void cp_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width, int32_t paint);
CP_IMPORT(triangle)
void cp_triangle(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy, int32_t paint);
// Word-wrapped to the rect, up to the style's max lines, with an ellipsis when
// it does not fit; vertically centred in the rect.
CP_IMPORT(text)
void cp_text(int32_t x, int32_t y, int32_t w, int32_t h, const char* text, int32_t len, int32_t face, int32_t style);
// `data` holds ceil(bw / 8) * bh bytes in `format`.
CP_IMPORT(bitmap)
void cp_bitmap(int32_t x, int32_t y, int32_t w, int32_t h, const void* data, int32_t bw, int32_t bh, int32_t format,
               int32_t fit, int32_t paint);
// Restrict drawing to a rect; w <= 0 clears it. Returns 0 if unsupported.
CP_IMPORT(set_clip) int32_t cp_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);

// --- What the app exports ----------------------------------------------------
//
//   int32_t cp_app_api_version(void); the API this app was built against,
//                                     (major << 16) | minor; must match the manifest
//   int32_t cp_app_init(void);        once, before anything else; nonzero aborts
//   void    cp_app_render(void);      draw the whole screen
//   int32_t cp_app_event(int32_t type, int32_t a, int32_t b, int32_t c);
//                                     1 = handled (for BACK: stay open)
//   void    cp_app_suspend(void);     about to close or sleep; save now
//
// An app built with the SDK gets these from appsdk/cpp/CrossPlayApp.cpp.

#ifdef __cplusplus
}
#endif
