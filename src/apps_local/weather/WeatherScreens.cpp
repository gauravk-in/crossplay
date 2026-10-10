#include "WeatherScreens.h"

#include "../ui/ToyboxMetrics.h"
#include "WeatherUiIcons.h"

namespace weatherui {

namespace {

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

const char* settingsLabel(const SettingsRow row) {
  switch (row) {
    case SettingsRow::Location:
      return "LOCATION";
    case SettingsRow::Temperature:
      return "TEMPERATURE";
    case SettingsRow::Wind:
      return "WIND";
    case SettingsRow::Rain:
      return "RAIN";
    case SettingsRow::Count:
      break;
  }
  return "";
}

}  // namespace

fui::Rect buildForecastChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.centered = true;
  header.borderEdges = fui::EdgesNone;
  header.leadingIcon = fui::bitmapFromIcon(icon_wx_settings_32);
  header.leadingAction = ActionSettings;
  header.leadingStyles = toybox::bandOutlineStyles();
  header.leadingRadius = toybox::kPillRadius / 2;
  header.trailingIcon = fui::bitmapFromIcon(icon_wx_refresh_32);
  header.trailingAction = ActionRefresh;
  header.trailingStyles = toybox::bandOutlineStyles();
  header.trailingRadius = toybox::kPillRadius / 2;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  return screen.body();
}

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "SETTINGS");

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionSettingsDone;
  done.borderEdges = fui::EdgesNone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight));

  constexpr int kCount = static_cast<int>(SettingsRow::Count);
  fui::ListItem rows[kCount] = {};
  const char* values[kCount] = {model.location != nullptr ? model.location : "SET ONE", model.temperature, model.wind,
                                model.rain};
  for (int i = 0; i < kCount; ++i) {
    rows[i].label = settingsLabel(static_cast<SettingsRow>(i));
    rows[i].value = values[i];
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(kCount);
  list.action = ActionSettingsRow;
  screen.list(list);
}

void buildPlaces(toybox::Screen& screen, const PlacesModel& model) {
  chrome(screen, "PICK A PLACE");

  fui::ButtonProps again;
  again.label = "SEARCH AGAIN";
  again.action = ActionSearchAgain;
  again.borderEdges = fui::EdgesNone;
  screen.button(again, screen.takeBottom(toybox::kPillHeight));

  if (model.count <= 0) {
    screen.centeredText("NO PLACE BY THAT NAME", screen.theme().bodyText);
    return;
  }
  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.action = ActionPickPlace;
  list.labelText = screen.theme().bodyText;
  list.subtitleText = screen.theme().smallText;
  list.subtitleText.font = toybox::kTileFont;
  list.subtitleText.align = fui::TextAlign::Left;
  list.rowHeight = static_cast<int16_t>(screen.target().lineHeight(screen.theme().bodyText.font) +
                                        screen.target().lineHeight(toybox::kTileFont) + toybox::kGutter);
  screen.list(list);
}

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "WEATHER");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);

  const bool hasAction = model.actionLabel != nullptr && model.action != fui::NO_ACTION;
  if (hasAction) {
    fui::ButtonProps action;
    action.label = model.actionLabel;
    action.action = model.action;
    screen.button(action, screen.takeBottom(toybox::kPillHeight));
  }

  const fui::Rect body = screen.body();
  int16_t y = body.y;
  const int16_t mark = toybox::kIconSize * 2;
  screen.target().bitmap(fui::makeRect(toybox::kMargin, y, mark, mark), fui::bitmapFromIcon(icon_wx_pin_32),
                         fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + mark + toybox::kGutter);

  fui::TextStyle headline = screen.theme().titleText;
  headline.color = fui::Color::Black;
  headline.align = fui::TextAlign::Left;
  headline.maxLines = 2;
  const int16_t headlineHeight = static_cast<int16_t>(2 * screen.target().lineHeight(headline.font));
  screen.target().text(fui::makeRect(toybox::kMargin, y, width, headlineHeight), model.headline, headline);
  y = static_cast<int16_t>(y + headlineHeight + toybox::kGutter);
  screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);

  if (model.message != nullptr && model.message[0] != '\0') {
    fui::TextAreaProps message;
    message.text = model.message;
    message.showCaret = false;
    message.style = screen.theme().bodyText;
    const int16_t bottom = static_cast<int16_t>(body.y + body.height);
    fui::textArea(screen.frame(), fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(bottom - y)), message);
  }
}

}  // namespace weatherui
