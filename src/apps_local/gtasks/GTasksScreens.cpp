#include "GTasksScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"

namespace gtasksui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;
// Notes' box and strike, so a ticked line reads the same in both apps.
constexpr int16_t kBoxSide = 40;
constexpr int16_t kStrike = 2;
// A subtask's indent: one box and its gap, so the child's box sits under the
// parent's text.
constexpr int16_t kIndent = kBoxSide / 2 + toybox::kGutter;
constexpr int16_t kMinRow = 72;  // a finger, with room to miss

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const fui::Color color = fui::Color::Black, const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  style.maxLines = maxLines;
  return style;
}

int16_t pageWidth(const fui::DeviceContext& device) { return static_cast<int16_t>(device.width - 2 * toybox::kMargin); }

// Header band, rule, and the page margin. `status` is paper on the black band:
// a label left at the default colour is black on black and simply not there.
void chrome(toybox::Screen& screen, const char* title, const char* status = nullptr,
            const freeink::Icon* trailing = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = status;
  header.borderEdges = fui::EdgesNone;
  if (status != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.font = toybox::kTileFont;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  if (trailing != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(*trailing);
    header.trailingAction = ActionSettings;
    header.trailingStyles = toybox::rowStyles();
  }
  // The band holds a list's name, which is somebody else's words: fitted here
  // because the Toybox cuts carry no ellipsis and an overflow just stops.
  fui::TextStyle titleStyle = screen.theme().titleText;
  const fui::Rect band = toybox::headerBandRect(screen);
  const std::string fitted =
      toybox::fittedTitle(screen.target(), title, toybox::headerTitleWidth(screen, band, header), titleStyle);
  header.title = fitted.c_str();
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       pageWidth(device), kFooterHeight);
}

// The page between the chrome and the footer, less the page label's strip when
// there is one, so a paged list never draws a row under its own page number.
fui::Rect listBand(const fui::DrawTarget& target, const fui::DeviceContext& device, const bool paged) {
  const fui::Rect footer = footerBand(device);
  const int16_t label = paged ? static_cast<int16_t>(target.lineHeight(toybox::kTileFont) + toybox::kGutter) : 0;
  return fui::makeRect(toybox::kMargin, kBodyTop, footer.width,
                       static_cast<int16_t>(footer.y - toybox::kGutter * 2 - label - kBodyTop));
}

int16_t rowPitch(const fui::DrawTarget& target) {
  const int16_t natural = static_cast<int16_t>(target.lineHeight(toybox::kBodyFont) +
                                               target.lineHeight(toybox::kTileFont) + toybox::kGutter);
  return natural > kMinRow ? natural : kMinRow;
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined, const bool enabled = true) {
  fui::ButtonProps button;
  button.label = label;
  button.action = enabled ? action : fui::NO_ACTION;
  if (outlined) button.styles = toybox::rowStyles();
  // Dimmed rather than removed: a control that vanishes moves its neighbours.
  if (!enabled) button.styles = toybox::disabledButtonStyles();
  screen.button(button, box);
}

// The whole row is the target: a 40px box is a miss waiting to happen.
void rowHit(toybox::Screen& screen, const fui::Rect& row, const int index) {
  fui::ButtonProps hit;
  hit.label = "";
  hit.action = ActionToggle;
  hit.value = static_cast<int16_t>(index);
  hit.styles = toybox::rowStyles();
  hit.styles.normal.background = fui::Paint::none();
  hit.styles.normal.border = fui::Paint::none();
  screen.button(hit, row);
}

void tickBox(toybox::Screen& screen, const fui::Rect& box, const bool checked) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule, 4);
  if (!checked) return;
  const int16_t inset = 9;
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                    static_cast<int16_t>(box.width - 2 * inset), static_cast<int16_t>(box.height - 2 * inset)),
      fui::Paint::solid(fui::Color::Black), 2);
}

void separator(toybox::Screen& screen, const fui::Rect& row) {
  screen.target().fill(
      fui::makeRect(row.x, static_cast<int16_t>(row.y + row.height - toybox::kHairline), row.width, toybox::kHairline),
      fui::Paint::solid(fui::Color::Black));
}

void drawRow(toybox::Screen& screen, const fui::Rect& row, const Row& r) {
  const int16_t indent = r.child ? kIndent : 0;
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(row.x + indent),
                                      static_cast<int16_t>(row.y + (row.height - kBoxSide) / 2), kBoxSide, kBoxSide);
  tickBox(screen, box, r.checked);

  const int16_t textX = static_cast<int16_t>(box.x + kBoxSide + toybox::kGutter);
  const int16_t textW = static_cast<int16_t>(row.x + row.width - textX);
  const fui::TextStyle title = plain(toybox::kBodyFont);
  const fui::TextStyle due = plain(toybox::kTileFont, fui::TextAlign::Left, fui::Color::DarkGray);
  const int16_t titleH = screen.target().lineHeight(title.font);
  const int16_t dueH = r.due != nullptr ? screen.target().lineHeight(due.font) : 0;
  const int16_t top = static_cast<int16_t>(row.y + (row.height - titleH - dueH) / 2);

  const std::string line = toybox::fitLines(screen.target(), r.title, textW, 1, title);
  screen.target().text(fui::makeRect(textX, top, textW, titleH), line.c_str(), title);
  if (r.checked && !line.empty()) {
    // A strike as wide as the ink, not the row: a tick that is waiting to go
    // up still reads as done, which is what the person did.
    const int16_t inkW = screen.target().measureText(title.font, line.c_str(), title).width;
    screen.target().fill(fui::makeRect(textX, static_cast<int16_t>(top + titleH / 2), inkW, kStrike),
                         fui::Paint::solid(fui::Color::Black));
  }
  if (r.due != nullptr) {
    screen.target().text(fui::makeRect(textX, static_cast<int16_t>(top + titleH), textW, dueH), r.due, due);
  }
}

}  // namespace

// --- The list --------------------------------------------------------------

int listCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, const bool paged) {
  const int capacity = listBand(target, device, paged).height / rowPitch(target);
  return capacity < 1 ? 1 : capacity;
}

void buildList(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, model.title, model.status, model.settingsIcon);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const bool paged = model.pageLabel != nullptr;

  // REFRESH keeps the left edge and most of the bar, the fork-wide home of the
  // primary action. The page arrows appear only when there is a second page,
  // and they take the right, where a thumb pages the shelf too.
  if (paged) {
    const int16_t arrow = static_cast<int16_t>(kFooterHeight + toybox::kGutter);
    const int16_t refreshW = static_cast<int16_t>(footer.width - 2 * (arrow + toybox::kGutter));
    footerButton(screen, fui::makeRect(footer.x, footer.y, refreshW, footer.height), "REFRESH", ActionRefresh, false);
    const int16_t prevX = static_cast<int16_t>(footer.x + refreshW + toybox::kGutter);
    footerButton(screen, fui::makeRect(prevX, footer.y, arrow, footer.height), "<", ActionPagePrev, true,
                 model.canPagePrev);
    footerButton(screen,
                 fui::makeRect(static_cast<int16_t>(prevX + arrow + toybox::kGutter), footer.y, arrow, footer.height),
                 ">", ActionPageNext, true, model.canPageNext);
  } else {
    footerButton(screen, footer, "REFRESH", ActionRefresh, false);
  }

  const fui::Rect band = listBand(screen.target(), device, paged);
  if (model.count <= 0 || model.rows == nullptr) {
    const int16_t headlineH = screen.target().lineHeight(toybox::kDisplayFont);
    const int16_t bodyH = screen.target().lineHeight(toybox::kUiFont);
    const int16_t top = static_cast<int16_t>(band.y + toybox::kMargin * 2);
    screen.target().text(fui::makeRect(band.x, top, band.width, headlineH), model.emptyHeadline,
                         plain(toybox::kDisplayFont, fui::TextAlign::Center));
    screen.target().text(fui::makeRect(band.x, static_cast<int16_t>(top + headlineH + toybox::kGutter), band.width,
                                       static_cast<int16_t>(bodyH * 3)),
                         model.emptyMessage, plain(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray, 3));
    return;
  }

  const int16_t pitch = rowPitch(screen.target());
  for (int i = 0; i < model.count; ++i) {
    const fui::Rect row = fui::makeRect(band.x, static_cast<int16_t>(band.y + i * pitch), band.width, pitch);
    if (row.y + row.height > band.y + band.height) break;
    drawRow(screen, row, model.rows[i]);
    if (i + 1 < model.count) separator(screen, row);
    rowHit(screen, row, model.firstIndex + i);
  }

  if (paged) {
    const fui::TextStyle style = plain(toybox::kTileFont, fui::TextAlign::Center);
    const int16_t h = screen.target().lineHeight(style.font);
    screen.target().text(
        fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height + toybox::kGutter), band.width, h),
        model.pageLabel, style);
  }
}

// --- Settings --------------------------------------------------------------

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "SETTINGS");
  const fui::DeviceContext& device = screen.device();
  footerButton(screen, footerBand(device), "BACK TO TASKS", ActionCloseSettings, false);

  // A list, not a stack of settingRows: Screen::list() themes the rows the way
  // every other settings screen in the fork is themed.
  fui::ListItem rows[2] = {};
  rows[0].label = "SYNC ON CHARGER";
  rows[0].value = model.pollLabel;
  rows[0].actionValue = static_cast<int16_t>(SettingRow::Poll);
  int count = 1;
  if (model.signedIn) {
    rows[1].label = "SIGN OUT";
    rows[1].actionValue = static_cast<int16_t>(SettingRow::SignOut);
    count = 2;
  }
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = -1;
  list.action = ActionSettingRow;
  screen.list(list);

  // What the first row means, under the rows, because "on charger" is the
  // whole condition and nothing else on the screen says so.
  const fui::Rect footer = footerBand(device);
  const fui::TextStyle note = plain(toybox::kUiFont, fui::TextAlign::Left, fui::Color::DarkGray, 4);
  const int16_t h = static_cast<int16_t>(screen.target().lineHeight(note.font) * 4);
  const int16_t width = pageWidth(device);
  const std::string text = toybox::fitLines(
      screen.target(), "While this app is open on the charger, it checks Google this often. REFRESH syncs any time.",
      width, 4, note);
  screen.target().text(
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(footer.y - toybox::kGutter * 2 - h), width, h), text.c_str(),
      note);
}

// --- Sign out confirm ------------------------------------------------------

void buildSignOutConfirm(toybox::Screen& screen, const int pendingCount) {
  chrome(screen, "SIGN OUT?");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  char body[200];
  if (pendingCount > 0) {
    std::snprintf(body, sizeof(body),
                  "This signs the reader out of Google. %d tick%s not sent yet will be lost. Signing in again "
                  "takes a code and your phone.",
                  pendingCount, pendingCount == 1 ? " that was" : "s that were");
  } else {
    std::snprintf(body, sizeof(body),
                  "This signs the reader out of Google and removes the list from the card. Signing in again takes a "
                  "code and your phone.");
  }
  const fui::TextStyle prose = plain(toybox::kBodyFont, fui::TextAlign::Left, fui::Color::Black, 8);
  const int16_t h = static_cast<int16_t>(footerBand(device).y - toybox::kGutter * 2 - kBodyTop);
  const int lines = h / screen.target().lineHeight(prose.font);
  const std::string drawn = toybox::fitLines(screen.target(), body, width, lines < 1 ? 1 : lines, prose);
  screen.target().text(fui::makeRect(toybox::kMargin, kBodyTop, width, h), drawn.c_str(), prose);

  const fui::Rect footer = footerBand(device);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "KEEP IT", ActionKeepSignedIn, false);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
               "SIGN OUT", ActionSignOut, true);
}

// --- Not signed in ---------------------------------------------------------

void buildSignIn(toybox::Screen& screen, const char* reason) {
  chrome(screen, "TASKS");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  footerButton(screen, footerBand(device), "GET A CODE", ActionGetCode, false);

  int16_t y = kBodyTop;
  const int16_t headlineH = screen.target().lineHeight(toybox::kDisplayFont);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, headlineH), "SIGN IN",
                       plain(toybox::kDisplayFont, fui::TextAlign::Left));
  y = static_cast<int16_t>(y + headlineH + toybox::kGutter);
  screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);

  const char* text = reason != nullptr && reason[0] != '\0'
                         ? reason
                         : "Your Google Tasks list, on this reader. Get a code here, then sign in with Google on "
                           "your phone. You do this once.";
  // A text area rather than a text run: it sits under the rule instead of
  // centring in the space, like the notice's message.
  fui::TextAreaProps message;
  message.text = text;
  message.showCaret = false;
  message.style = screen.theme().bodyText;
  fui::textArea(
      screen.frame(),
      fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(footerBand(device).y - toybox::kGutter - y)),
      message);
}

fui::Rect buildPairQr(toybox::Screen& screen, const char* code, const char* address) {
  chrome(screen, "SIGN IN");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const fui::Rect footer = footerBand(device);
  footerButton(screen, footer, "CANCEL", ActionCancelPair, true);

  // Bottom up: the caption and the address hug the footer, the code sits on
  // them, and the QR takes what is left up to a size a camera reads at arm's
  // length.
  const fui::TextStyle caption = plain(toybox::kTileFont, fui::TextAlign::Center, fui::Color::DarkGray);
  const int16_t captionH = screen.target().lineHeight(caption.font);
  const fui::TextStyle prose = plain(toybox::kUiFont, fui::TextAlign::Center, fui::Color::DarkGray);
  const int16_t proseH = screen.target().lineHeight(prose.font);
  const int16_t codeH = screen.target().lineHeight(toybox::kDisplayFont);

  int16_t y = static_cast<int16_t>(footer.y - toybox::kGutter * 2 - captionH);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, captionH), "CODE LASTS 5 MIN", caption);
  // The address on a line of its own, full width: it is one unbreakable token,
  // and a cut one is an address that does not exist.
  y = static_cast<int16_t>(y - toybox::kGutter - proseH);
  fui::TextStyle addressStyle = prose;
  addressStyle.color = fui::Color::Black;
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, proseH), address, addressStyle);
  y = static_cast<int16_t>(y - proseH);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, proseH), "Scan it, or type the code at", prose);
  y = static_cast<int16_t>(y - toybox::kGutter - codeH);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, codeH), code,
                       plain(toybox::kDisplayFont, fui::TextAlign::Center));

  const int16_t top = kBodyTop;
  int16_t side = static_cast<int16_t>(y - toybox::kGutter * 2 - top);
  if (side > 260) side = 260;
  if (side < 0) side = 0;
  const int16_t qrY = static_cast<int16_t>(top + (y - toybox::kGutter - top - side) / 2);
  return fui::makeRect(static_cast<int16_t>((device.width - side) / 2), qrY, side, side);
}

void buildPairConfirm(toybox::Screen& screen, const char* account) {
  chrome(screen, "SIGN IN");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);

  int16_t y = kBodyTop;
  const int16_t headlineH = screen.target().lineHeight(toybox::kDisplayFont);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, headlineH), "IS THIS YOU?",
                       plain(toybox::kDisplayFont, fui::TextAlign::Left));
  y = static_cast<int16_t>(y + headlineH + toybox::kGutter);
  screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);

  // An address is one long unbreakable token: two lines of the reading cut
  // hold every real one, and fitLines cuts mid-token rather than vanishing.
  const fui::TextStyle name = plain(toybox::kBodyFont, fui::TextAlign::Left, fui::Color::Black, 2);
  const int16_t nameH = static_cast<int16_t>(screen.target().lineHeight(name.font) * 2);
  const char* shownAccount = account != nullptr && account[0] != '\0' ? account : "a Google account";
  const std::string fitted = toybox::fitLines(screen.target(), shownAccount, width, 2, name);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, nameH), fitted.c_str(), name);
  y = static_cast<int16_t>(y + nameH + toybox::kGutter * 2);

  const fui::TextStyle prose = plain(toybox::kUiFont, fui::TextAlign::Left, fui::Color::DarkGray, 4);
  const int16_t proseH = static_cast<int16_t>(screen.target().lineHeight(prose.font) * 4);
  const std::string note = toybox::fitLines(
      screen.target(), "This reader shows this account's tasks once you say yes. Nothing is kept before that.", width,
      4, prose);
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, proseH), note.c_str(), prose);

  const fui::Rect footer = footerBand(device);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "YES, USE IT", ActionPairYes, false);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
               "NOT ME", ActionPairNo, true);
}

// --- Notices ---------------------------------------------------------------

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "TASKS");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  if (model.actionLabel != nullptr) footerButton(screen, footerBand(device), model.actionLabel, ActionNotice, false);

  int16_t y = kBodyTop;
  if (model.headline != nullptr && model.headline[0] != '\0') {
    fui::TextStyle headline = screen.theme().titleText;
    headline.color = fui::Color::Black;  // off the band, so it has to be ink
    headline.align = fui::TextAlign::Left;
    headline.maxLines = 2;
    const int16_t h = static_cast<int16_t>(2 * screen.target().lineHeight(headline.font));
    screen.target().text(fui::makeRect(toybox::kMargin, y, width, h), model.headline, headline);
    y = static_cast<int16_t>(y + h + toybox::kGutter);
    screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);
  }
  if (model.message != nullptr && model.message[0] != '\0') {
    const int16_t reserved = model.actionLabel != nullptr ? static_cast<int16_t>(kFooterHeight + toybox::kGutter) : 0;
    const int16_t bottom = static_cast<int16_t>(device.height - toybox::kMargin - reserved);
    fui::TextAreaProps message;
    message.text = model.message;
    message.showCaret = false;
    message.style = screen.theme().bodyText;
    fui::textArea(screen.frame(), fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(bottom - y)), message);
  }
}

}  // namespace gtasksui
