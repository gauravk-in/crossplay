#include "PrompterScreens.h"

#include <string>

#include "../ui/ToyboxText.h"

namespace prompterui {
namespace {

constexpr int16_t kFooterHeight = toybox::kPillHeight;
constexpr int16_t kStepWidth = 64;
constexpr int16_t kChoiceWidth = 170;

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = maxLines;
  return style;
}

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const fui::FontId font) {
  fui::TextStyle style = plain(font, align);
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

int16_t prose(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
              const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style = plain(font, align);
  const int16_t lineHeight = screen.target().lineHeight(font);
  int lines = lineHeight > 0 ? box.height / lineHeight : 1;
  if (lines < 1) lines = 1;
  if (lines > 8) lines = 8;
  style.maxLines = static_cast<uint8_t>(lines);
  const std::string drawn = toybox::fitLines(screen.target(), text, box.width, style.maxLines, style);
  int used = 1;
  for (const char c : drawn) used += c == '\n' ? 1 : 0;
  screen.target().text(box, drawn.c_str(), style);
  return static_cast<int16_t>(used * lineHeight);
}

fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), kFooterHeight);
}

void button(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
            const bool outlined, const bool enabled = true) {
  fui::ButtonProps props;
  props.label = label;
  props.action = action;
  props.enabled = enabled;
  if (outlined) props.styles = toybox::rowStyles();
  if (!enabled) props.styles = toybox::disabledStepperStyles();
  screen.button(props, box);
}

// Two footer buttons side by side: the outlined one left, the filled one right.
void footerPair(toybox::Screen& screen, const fui::Rect& footer, const char* left, const fui::ActionId leftAction,
                const char* right, const fui::ActionId rightAction) {
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  button(screen, fui::makeRect(footer.x, footer.y, half, footer.height), left, leftAction, true);
  button(screen, fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
         right, rightAction, false);
}

void rule(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t width) {
  screen.target().fill(fui::makeRect(x, y, width, 1), fui::Paint::solid(fui::Color::Black));
}

// A settings row: the label left, a value and its two steps right.
void stepperRow(toybox::Screen& screen, const fui::Rect& row, const char* label, const char* value,
                const fui::ActionId down, const fui::ActionId up, const bool canDown, const bool canUp) {
  const int16_t buttonY = static_cast<int16_t>(row.y + (row.height - toybox::kPillHeight) / 2);
  const int16_t right = static_cast<int16_t>(row.x + row.width);
  const fui::Rect plus =
      fui::makeRect(static_cast<int16_t>(right - kStepWidth), buttonY, kStepWidth, toybox::kPillHeight);
  const fui::Rect valueBox =
      fui::makeRect(static_cast<int16_t>(plus.x - toybox::kGutter - 110), row.y, 110, row.height);
  const fui::Rect minus = fui::makeRect(static_cast<int16_t>(valueBox.x - toybox::kGutter - kStepWidth), buttonY,
                                        kStepWidth, toybox::kPillHeight);
  const fui::Rect labelBox =
      fui::makeRect(row.x, row.y, static_cast<int16_t>(minus.x - row.x - toybox::kGutter), row.height);
  fittedLine(screen, toybox::inkCentred(labelBox, toybox::kTileCut), label, fui::TextAlign::Left, toybox::kTileFont);
  button(screen, minus, "-", down, true, canDown);
  fittedLine(screen, toybox::inkCentred(valueBox, toybox::kUiCut), value, fui::TextAlign::Center, toybox::kUiFont);
  button(screen, plus, "+", up, true, canUp);
}

// A settings row with one button that cycles its value.
void choiceRow(toybox::Screen& screen, const fui::Rect& row, const char* label, const char* value,
               const fui::ActionId action, const bool enabled = true) {
  const int16_t buttonY = static_cast<int16_t>(row.y + (row.height - toybox::kPillHeight) / 2);
  const fui::Rect box =
      fui::makeRect(static_cast<int16_t>(row.x + row.width - kChoiceWidth), buttonY, kChoiceWidth, toybox::kPillHeight);
  const fui::Rect labelBox =
      fui::makeRect(row.x, row.y, static_cast<int16_t>(box.x - row.x - toybox::kGutter), row.height);
  fittedLine(screen, toybox::inkCentred(labelBox, toybox::kTileCut), label, fui::TextAlign::Left, toybox::kTileFont);
  button(screen, box, value, action, true, enabled);
}

}  // namespace

// --- The scripts ----------------------------------------------------------------

int buildLibrary(toybox::Screen& screen, const LibraryModel& model) {
  chrome(screen, "PROMPTER");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;
  const int16_t lineHeight = screen.target().lineHeight(toybox::kTileFont);

  fittedLine(screen, fui::makeRect(toybox::kMargin, toybox::kBodyTop, width, lineHeight), model.turner,
             fui::TextAlign::Left, toybox::kTileFont);
  const int16_t top = static_cast<int16_t>(toybox::kBodyTop + lineHeight + toybox::kGutter);
  const int16_t bottom = static_cast<int16_t>(footer.y - toybox::kGutter);

  if (model.count <= 0) {
    prose(screen,
          fui::makeRect(toybox::kMargin, static_cast<int16_t>(top + toybox::kGutter), width,
                        static_cast<int16_t>(bottom - top - toybox::kGutter)),
          "No scripts yet. Tap PHONE to write or paste one on your phone, or copy .txt files into the prompter "
          "folder on the card.",
          toybox::kBodyFont);
    footerPair(screen, footer, "SETTINGS", ActionSettings, "PHONE", ActionPhone);
    return 0;
  }

  // Rows by hand rather than the list component: each is a script name in the
  // UI cut, a whole row tappable, a rule under it.
  const int16_t rowHeight = static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter);
  int fit = (bottom - top) / rowHeight;
  if (fit < 1) fit = 1;
  const bool paged = model.count > fit;
  const int16_t arrowWidth = paged ? static_cast<int16_t>(kStepWidth + toybox::kGutter) : 0;
  const int16_t rowWidth = static_cast<int16_t>(width - arrowWidth);
  rule(screen, toybox::kMargin, top, rowWidth);
  for (int i = 0; i < fit && model.top + i < model.count; ++i) {
    const int index = model.top + i;
    const fui::Rect row =
        fui::makeRect(toybox::kMargin, static_cast<int16_t>(top + i * rowHeight), rowWidth, rowHeight);
    const bool current = index == model.current;
    if (current) {
      screen.target().fill(
          fui::makeRect(row.x, static_cast<int16_t>(row.y + 1), 6, static_cast<int16_t>(row.height - 1)),
          fui::Paint::solid(fui::Color::Black));
    }
    const fui::Rect label = fui::makeRect(static_cast<int16_t>(row.x + (current ? 18 : 6)), row.y,
                                          static_cast<int16_t>(row.width - (current ? 24 : 12)), row.height);
    fittedLine(screen, toybox::inkCentred(label, toybox::kUiCut), model.names[index], fui::TextAlign::Left,
               toybox::kUiFont);
    rule(screen, row.x, static_cast<int16_t>(row.y + row.height), row.width);
    fui::ButtonProps hit;
    hit.label = "";
    hit.action = ActionOpenScript;
    hit.value = static_cast<int16_t>(index);
    hit.styles = toybox::rowStyles();
    hit.styles.normal.background = fui::Paint::none();
    hit.styles.normal.border = fui::Paint::none();
    screen.button(hit, row);
  }
  if (paged) {
    const int16_t x = static_cast<int16_t>(toybox::kMargin + width - kStepWidth);
    button(screen, fui::makeRect(x, top, kStepWidth, toybox::kPillHeight), "UP", ActionListUp, true, model.top > 0);
    button(screen,
           fui::makeRect(x, static_cast<int16_t>(bottom - toybox::kPillHeight), kStepWidth, toybox::kPillHeight), "DN",
           ActionListDown, true, model.top + fit < model.count);
  }
  footerPair(screen, footer, "SETTINGS", ActionSettings, "PHONE", ActionPhone);
  return fit;
}

// --- Settings --------------------------------------------------------------------

fui::Rect buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "SETTINGS");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;
  const int16_t rowHeight = static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter * 2);
  int16_t y = static_cast<int16_t>(toybox::kBodyTop);

  char size[16];
  snprintf(size, sizeof(size), "%d / %d", model.size + 1, model.sizeCount);
  stepperRow(screen, fui::makeRect(toybox::kMargin, y, width, rowHeight), "TEXT SIZE", size, ActionSizeDown,
             ActionSizeUp, model.size > 0, model.size + 1 < model.sizeCount);
  y = static_cast<int16_t>(y + rowHeight);
  // The sample is the caller's: it needs the real cut, which is a renderer font.
  const fui::Rect sample = fui::makeRect(toybox::kMargin, y, width, 96);
  y = static_cast<int16_t>(y + sample.height + toybox::kGutter);
  rule(screen, toybox::kMargin, y, width);

  stepperRow(screen, fui::makeRect(toybox::kMargin, y, width, rowHeight), "PAGE TIMER", model.timer, ActionAutoDown,
             ActionAutoUp, model.canTimerDown, model.canTimerUp);
  y = static_cast<int16_t>(y + rowHeight);
  rule(screen, toybox::kMargin, y, width);
  choiceRow(screen, fui::makeRect(toybox::kMargin, y, width, rowHeight), "SCREEN",
            model.landscape ? "LANDSCAPE" : "PORTRAIT", ActionOrientation);
  y = static_cast<int16_t>(y + rowHeight);
  rule(screen, toybox::kMargin, y, width);
  choiceRow(screen, fui::makeRect(toybox::kMargin, y, width, rowHeight), "COLOURS", model.dark ? "DARK" : "LIGHT",
            ActionColors);
  y = static_cast<int16_t>(y + rowHeight);
  rule(screen, toybox::kMargin, y, width);
  choiceRow(screen, fui::makeRect(toybox::kMargin, y, width, rowHeight), "PAGE TURNER",
            model.bluetooth ? "SET UP" : "NONE", ActionTurner, model.bluetooth);
  y = static_cast<int16_t>(y + rowHeight);
  const int16_t lineHeight = screen.target().lineHeight(toybox::kTileFont);
  fittedLine(screen, fui::makeRect(toybox::kMargin, static_cast<int16_t>(y - toybox::kGutter / 2), width, lineHeight),
             model.turner, fui::TextAlign::Left, toybox::kTileFont);
  y = static_cast<int16_t>(y + lineHeight);
  rule(screen, toybox::kMargin, y, width);
  choiceRow(screen, fui::makeRect(toybox::kMargin, y, width, rowHeight), "SWAP TURNER KEYS",
            model.swap ? "SWAPPED" : "NORMAL", ActionSwap, model.bluetooth);

  button(screen, footer, "DONE", ActionDismiss, false);
  return sample;
}

// --- The page turner -------------------------------------------------------------

void buildTurner(toybox::Screen& screen, const TurnerModel& model) {
  chrome(screen, "PAGE TURNER");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;
  int16_t y = toybox::kBodyTop;
  y = static_cast<int16_t>(
      y + prose(screen, fui::makeRect(toybox::kMargin, y, width, 140), model.status, toybox::kBodyFont));
  y = static_cast<int16_t>(y + toybox::kGutter);

  const int16_t rowHeight = static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter);
  const int16_t bottom = static_cast<int16_t>(footer.y - toybox::kGutter);
  if (model.foundCount > 0) rule(screen, toybox::kMargin, y, width);
  for (int i = 0; i < model.foundCount && y + rowHeight <= bottom; ++i) {
    const fui::Rect row = fui::makeRect(toybox::kMargin, y, width, rowHeight);
    fittedLine(screen,
               toybox::inkCentred(fui::makeRect(static_cast<int16_t>(row.x + 6), row.y,
                                                static_cast<int16_t>(row.width - 140), row.height),
                                  toybox::kUiCut),
               model.found[i], fui::TextAlign::Left, toybox::kUiFont);
    fui::ButtonProps pair;
    pair.label = "PAIR";
    pair.action = ActionPair;
    pair.value = static_cast<int16_t>(i);
    pair.styles = toybox::rowStyles();
    screen.button(pair, fui::makeRect(static_cast<int16_t>(row.x + row.width - 120),
                                      static_cast<int16_t>(row.y + (row.height - toybox::kPillHeight) / 2), 120,
                                      toybox::kPillHeight));
    rule(screen, row.x, static_cast<int16_t>(row.y + row.height), row.width);
    y = static_cast<int16_t>(y + rowHeight);
  }

  if (model.paired) {
    const int16_t third = static_cast<int16_t>((footer.width - 2 * toybox::kGutter) / 3);
    button(screen, fui::makeRect(footer.x, footer.y, third, footer.height), "FORGET", ActionForget, true);
    button(screen,
           fui::makeRect(static_cast<int16_t>(footer.x + third + toybox::kGutter), footer.y, third, footer.height),
           model.scanning ? "LOOKING" : "SCAN", ActionScan, true, !model.scanning);
    button(
        screen,
        fui::makeRect(static_cast<int16_t>(footer.x + 2 * (third + toybox::kGutter)), footer.y, third, footer.height),
        "DONE", ActionDismiss, false);
    return;
  }
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  button(screen, fui::makeRect(footer.x, footer.y, half, footer.height), model.scanning ? "LOOKING" : "SCAN",
         ActionScan, true, !model.scanning);
  button(screen, fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
         "DONE", ActionDismiss, false);
}

// --- The phone -------------------------------------------------------------------

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, "PROMPTER");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;

  const int16_t lineHeight = screen.target().lineHeight(toybox::kTileFont);
  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop + toybox::kGutter), width, lineHeight);
  fittedLine(screen, caption, "POINT YOUR PHONE CAMERA HERE", fui::TextAlign::Center, toybox::kTileFont);

  const int16_t top = static_cast<int16_t>(caption.y + caption.height + toybox::kGutter * 2);
  const int16_t room = static_cast<int16_t>(footer.y - toybox::kGutter * 2 - top - lineHeight * 5);
  int16_t side = room < width ? room : width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), top, side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, url, model.readable, fui::TextAlign::Center, toybox::kTileFont);
  const fui::Rect state =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, lineHeight);
  const std::string saved = model.saved[0] != '\0' ? std::string("RECEIVED ") + model.saved : "WAITING FOR YOUR PHONE";
  fittedLine(screen, state, saved.c_str(), fui::TextAlign::Center, toybox::kTileFont);

  button(screen, footer, "DONE", ActionDismiss, false);
  return qr;
}

void buildNotice(toybox::Screen& screen, const char* text) {
  chrome(screen, "PROMPTER");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  prose(screen,
        fui::makeRect(footer.x, toybox::kBodyTop, footer.width,
                      static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop)),
        text, toybox::kBodyFont);
  button(screen, footer, "BACK", ActionDismiss, false);
}

}  // namespace prompterui
