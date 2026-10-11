#include "WeatherActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../CrossPointSettings.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxMetrics.h"
#include "../ui/ToyboxTheme.h"
#include "DevMode.h"
#include "WeatherIcons.h"
#include "WeatherScreens.h"
#include "WeatherUiIcons.h"

namespace fui = freeink::ui;
using weather::Forecast;
using weather::Sky;

namespace {

constexpr char kConfigPath[] = "/.crosspoint/weather.cfg";
constexpr char kForecastPath[] = "/.crosspoint/weather.txt";
constexpr const char* kTag = "WEATHER";
constexpr int kMaxPlaces = 8;
constexpr size_t kQueryMax = 40;

// --- Text in, ASCII out ---------------------------------------------------

// The Toybox cuts carry printable ASCII and nothing else, and a glyph a cut
// lacks draws as nothing. Place names arrive in every script, so they are
// folded to their base letters where they have one ("Munchen") at the point
// they enter the app.
void foldToAscii(const char* in, char* out, const size_t cap) {
  if (cap == 0) return;
  const std::string folded = utf8FoldTypography(in != nullptr ? in : "");
  const unsigned char* p = reinterpret_cast<const unsigned char*>(folded.c_str());
  size_t n = 0;
  auto put = [&](const char* s) {
    while (*s != '\0' && n + 1 < cap) out[n++] = *s++;
  };
  while (*p != '\0' && n + 1 < cap) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp >= 0x20 && cp < 0x7F) {
      const char c[2] = {static_cast<char>(cp), '\0'};
      put(c);
      continue;
    }
    switch (cp) {
      case 0xDF:
        put("ss");
        continue;
      case 0xC6:
        put("AE");
        continue;
      case 0xE6:
        put("ae");
        continue;
      case 0xD8:
        put("O");
        continue;
      case 0xF8:
        put("o");
        continue;
      case 0x141:
        put("L");
        continue;
      case 0x142:
        put("l");
        continue;
      case 0x110:
        put("D");
        continue;
      case 0x111:
        put("d");
        continue;
      default:
        break;
    }
    const uint32_t base = utf8DecomposedBase(cp);
    if (base >= 0x20 && base < 0x7F) {
      const char c[2] = {static_cast<char>(base), '\0'};
      put(c);
    }
  }
  out[n] = '\0';
}

void upper(const char* in, char* out, const size_t cap) {
  size_t n = 0;
  for (; in[n] != '\0' && n + 1 < cap; ++n) {
    const char c = in[n];
    out[n] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
  }
  out[n] = '\0';
}

// --- Drawing --------------------------------------------------------------

// A Lucide icon at its own size. Bit 0 is ink (see Icon.h).
void drawIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    for (int col = 0; col < icon.w; ++col) {
      if (((icon.bits[row * rowBytes + (col >> 3)] >> (7 - (col & 7))) & 1) == 0) {
        renderer.drawPixel(x + col, y + row, true);
      }
    }
  }
}

enum class IconSize : uint8_t { Small, Medium, Hero };

const freeink::Icon& skyIcon(const Sky sky, const bool day, const IconSize size) {
  struct Set {
    const freeink::Icon* small;
    const freeink::Icon* medium;
    const freeink::Icon* hero;
  };
#define WX_SET(name) Set{&icon_##name##_32, &icon_##name##_48, &icon_##name##_112}
  Set set = WX_SET(wx_cloud);
  switch (sky) {
    case Sky::Clear:
      set = day ? WX_SET(wx_sun) : WX_SET(wx_moon);
      break;
    case Sky::PartlyCloudy:
      set = day ? WX_SET(wx_cloud_sun) : WX_SET(wx_cloud_moon);
      break;
    case Sky::Cloudy:
      set = WX_SET(wx_cloud);
      break;
    case Sky::Fog:
      set = WX_SET(wx_fog);
      break;
    case Sky::Drizzle:
      set = WX_SET(wx_drizzle);
      break;
    case Sky::Rain:
      set = WX_SET(wx_rain);
      break;
    case Sky::Showers:
      set = day ? WX_SET(wx_shower_sun) : WX_SET(wx_shower_moon);
      break;
    case Sky::Snow:
      set = WX_SET(wx_snow);
      break;
    case Sky::Storm:
      set = WX_SET(wx_storm);
      break;
  }
#undef WX_SET
  switch (size) {
    case IconSize::Small:
      return *set.small;
    case IconSize::Medium:
      return *set.medium;
    case IconSize::Hero:
      break;
  }
  return *set.hero;
}

// Draws `text` with its baseline at `baseline`. Returns its width.
int textAt(const GfxRenderer& renderer, const int font, const int x, const int baseline, const char* text,
           const bool black = true) {
  const auto m = toybox::metricsFor(font);
  renderer.drawText(font, x, baseline - m.ascender, text, black);
  return renderer.getTextWidth(font, text);
}

// The degree sign, which no Toybox cut carries: a ring sized to the cap height
// and sat at the top of the capitals.
int degreeWidth(const int font) {
  const int cap = toybox::metricsFor(font).capHeight;
  const int d = cap < 30 ? cap * 2 / 5 : cap / 4;
  return d + (d < 10 ? 3 : d / 3);
}

void drawDegree(const GfxRenderer& renderer, const int font, const int x, const int baseline, const bool black) {
  const int cap = toybox::metricsFor(font).capHeight;
  const int d = cap < 30 ? cap * 2 / 5 : cap / 4;
  const int weight = d < 10 ? 2 : (d < 16 ? 3 : d / 4);
  const int gap = d < 10 ? 2 : d / 5;
  const int top = baseline - cap;
  renderer.fillRoundedRect(x + gap, top, d, d, d / 2, black ? Black : White);
  if (d > 2 * weight) {
    renderer.fillRoundedRect(x + gap + weight, top + weight, d - 2 * weight, d - 2 * weight, (d - 2 * weight) / 2,
                             black ? White : Black);
  }
}

int tempWidth(const GfxRenderer& renderer, const int font, const int value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%d", value);
  return renderer.getTextWidth(font, text) + degreeWidth(font);
}

// "21" and a degree ring. Returns the width drawn.
int drawTemp(const GfxRenderer& renderer, const int font, const int x, const int baseline, const int value,
             const bool black = true) {
  char text[16];
  std::snprintf(text, sizeof(text), "%d", value);
  const int w = textAt(renderer, font, x, baseline, text, black);
  drawDegree(renderer, font, x + w, baseline, black);
  return w + degreeWidth(font);
}

int drawTempRight(const GfxRenderer& renderer, const int font, const int right, const int baseline, const int value) {
  const int w = tempWidth(renderer, font, value);
  drawTemp(renderer, font, right - w, baseline, value);
  return w;
}

void drawTempCentred(const GfxRenderer& renderer, const int font, const int cx, const int baseline, const int value) {
  drawTemp(renderer, font, cx - tempWidth(renderer, font, value) / 2, baseline, value);
}

void textCentred(const GfxRenderer& renderer, const int font, const int cx, const int baseline, const char* text) {
  textAt(renderer, font, cx - renderer.getTextWidth(font, text) / 2, baseline, text);
}

int64_t nowEpoch() { return static_cast<int64_t>(std::time(nullptr)); }

bool twelveHour() { return SETTINGS.clockFormat == 1; }

}  // namespace

std::unique_ptr<Activity> WeatherActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WeatherActivity>(renderer, mappedInput);
}

// --- Lifecycle -------------------------------------------------------------

void WeatherActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  if (Storage.exists(kConfigPath)) weather::parseConfig(Storage.readFile(kConfigPath).c_str(), config_);

  forecast_ = makeUniqueNoThrow<Forecast>();
  if (!forecast_) {
    LOG_ERR(kTag, "OOM: forecast %u bytes", static_cast<unsigned>(sizeof(Forecast)));
    showNotice("OUT OF MEMORY", "There was not enough memory to hold a forecast.", nullptr, fui::NO_ACTION);
    requestUpdate();
    return;
  }
  haveForecast_ =
      Storage.exists(kForecastPath) && weather::parseForecast(Storage.readFile(kForecastPath).c_str(), *forecast_);

  if (!config_.hasPlace) {
    showNotice("PICK A PLACE",
               "Search for a city to see its weather. The forecast is fetched the first time you look each day; "
               "refresh fetches it again.",
               "SET LOCATION", weatherui::ActionNotice);
    requestUpdate();
    return;
  }

  const weather::Freshness fresh = weather::freshness(haveForecast_ ? forecast_.get() : nullptr, config_, nowEpoch());
  if (haveForecast_ && fresh != weather::Freshness::NewPlace) view_ = View::Forecast;
  if (weather::needsFetch(fresh)) {
    LOG_INF(kTag, "fetching: freshness %d", static_cast<int>(fresh));
    if (fresh == weather::Freshness::NewPlace) haveForecast_ = false;
    request(Pending::Forecast);
    return;
  }
  view_ = View::Forecast;
  requestUpdate();
}

void WeatherActivity::onExit() {
  Activity::onExit();
  if (wifiUsed_ && WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// --- Scheduling the slow parts ---------------------------------------------

void WeatherActivity::request(const Pending what) {
  if (what == Pending::None) return;
  {
    RenderLock lock(*this);
    pending_ = what;
    if (what == Pending::Forecast) {
      if (haveForecast_) {
        view_ = View::Forecast;
        status_ = Status::Updating;
      } else {
        noticeHeadline_ = "FETCHING THE FORECAST";
        noticeMessage_ = config_.name;
        noticeAction_ = nullptr;
        noticeActionId_ = fui::NO_ACTION;
        view_ = View::Notice;
      }
    } else {
      noticeHeadline_ = "SEARCHING";
      noticeMessage_ = query_;
      noticeAction_ = nullptr;
      noticeActionId_ = fui::NO_ACTION;
      view_ = View::Notice;
    }
  }
  requestUpdate();
}

void WeatherActivity::ensureConnected(const Pending what) {
  if (WiFi.status() == WL_CONNECTED) {
    if (what == Pending::Forecast) {
      if (!fetchForecast()) {
        status_ = Status::Failed;
        if (!haveForecast_) {
          showNotice("NO FORECAST", "Open-Meteo did not answer. Check the Wi-Fi and try again.", "TRY AGAIN",
                     weatherui::ActionRefresh);
        }
      }
    } else if (!searchPlaces()) {
      showNotice("SEARCH FAILED", "The place search did not answer. Check the Wi-Fi and try again.", "TRY AGAIN",
                 weatherui::ActionSearchAgain);
    }
    requestUpdate();
    return;
  }

  afterConnect_ = what;
  backPressSeen_ = false;
  wifiUsed_ = true;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           const Pending what = afterConnect_;
                           afterConnect_ = Pending::None;
                           interactionsReady_ = false;
                           if (!result.isCancelled) {
                             pending_ = what;
                             requestUpdate();
                             return;
                           }
                           if (what == Pending::Search) {
                             showNotice("NO WI-FI", "Searching for a place needs the internet.", "TRY AGAIN",
                                        weatherui::ActionSearchAgain);
                           } else if (haveForecast_) {
                             status_ = Status::NoWifi;
                             view_ = View::Forecast;
                           } else {
                             showNotice("NO WI-FI", "The forecast needs the internet the first time.", "TRY AGAIN",
                                        weatherui::ActionRefresh);
                           }
                           requestUpdate();
                         });
}

// --- Network ---------------------------------------------------------------

bool WeatherActivity::fetchForecast() {
  char url[640];
  if (!weather::forecastUrl(config_.lat, config_.lon, url, sizeof(url))) return false;
  std::string body;
  if (!HttpDownloader::fetchUrl(url, body)) {
    LOG_ERR(kTag, "forecast fetch failed (%d)", HttpDownloader::lastStatus());
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  body.clear();
  body.shrink_to_fit();
  if (err) {
    LOG_ERR(kTag, "forecast parse error: %s", err.c_str());
    return false;
  }

  auto fresh = makeUniqueNoThrow<Forecast>();
  if (!fresh) {
    LOG_ERR(kTag, "OOM: forecast");
    return false;
  }
  Forecast& f = *fresh;
  // The configured place, not the grid point the server snapped it to, so the
  // next open recognises this report as being for the same place.
  f.lat = config_.lat;
  f.lon = config_.lon;
  f.utcOffset = doc["utc_offset_seconds"] | 0;

  const JsonObjectConst cur = doc["current"];
  f.fetchedAt = cur["time"] | static_cast<int64_t>(0);
  f.temp = cur["temperature_2m"] | 0.0f;
  f.feels = cur["apparent_temperature"] | 0.0f;
  f.wind = cur["wind_speed_10m"] | 0.0f;
  f.humidity = static_cast<uint8_t>(cur["relative_humidity_2m"] | 0);
  f.code = static_cast<int16_t>(cur["weather_code"] | 0);
  f.isDay = (cur["is_day"] | 1) != 0;

  const JsonObjectConst hourly = doc["hourly"];
  const JsonArrayConst hTime = hourly["time"];
  const JsonArrayConst hTemp = hourly["temperature_2m"];
  const JsonArrayConst hCode = hourly["weather_code"];
  const JsonArrayConst hRain = hourly["precipitation_probability"];
  const JsonArrayConst hDay = hourly["is_day"];
  for (size_t i = 0; i < hTime.size() && f.hourCount < weather::kHours; ++i) {
    weather::Hour& h = f.hours[f.hourCount++];
    h.time = hTime[i] | static_cast<int64_t>(0);
    h.temp = hTemp[i] | 0.0f;
    h.code = static_cast<int16_t>(hCode[i] | 0);
    h.rainChance = static_cast<uint8_t>(hRain[i] | 0);
    h.day = (hDay[i] | 1) != 0;
  }

  const JsonObjectConst daily = doc["daily"];
  const JsonArrayConst dTime = daily["time"];
  const JsonArrayConst dCode = daily["weather_code"];
  const JsonArrayConst dHigh = daily["temperature_2m_max"];
  const JsonArrayConst dLow = daily["temperature_2m_min"];
  const JsonArrayConst dChance = daily["precipitation_probability_max"];
  const JsonArrayConst dSum = daily["precipitation_sum"];
  const JsonArrayConst dWind = daily["wind_speed_10m_max"];
  const JsonArrayConst dRise = daily["sunrise"];
  const JsonArrayConst dSet = daily["sunset"];
  for (size_t i = 0; i < dTime.size() && f.dayCount < weather::kDays; ++i) {
    weather::Day& d = f.days[f.dayCount++];
    d.time = dTime[i] | static_cast<int64_t>(0);
    d.code = static_cast<int16_t>(dCode[i] | 0);
    d.high = dHigh[i] | 0.0f;
    d.low = dLow[i] | 0.0f;
    d.rainChance = static_cast<uint8_t>(dChance[i] | 0);
    d.rainMm = dSum[i] | 0.0f;
    d.windMax = dWind[i] | 0.0f;
    d.sunrise = dRise[i] | static_cast<int64_t>(0);
    d.sunset = dSet[i] | static_cast<int64_t>(0);
  }

  if (f.fetchedAt == 0 || f.dayCount == 0) {
    LOG_ERR(kTag, "forecast had no current time or no days");
    return false;
  }
  // A device that was never told the date learns it here, so tomorrow's first
  // open can tell it is tomorrow.
  if (nowEpoch() < weather::kClockFloor) LOG_INF(kTag, "device clock unset; server says %lld", f.fetchedAt);

  {
    RenderLock lock(*this);
    forecast_ = std::move(fresh);
    haveForecast_ = true;
    status_ = Status::Idle;
    view_ = View::Forecast;
  }
  const std::string text = weather::formatForecast(*forecast_);
  if (!Storage.writeFile(kForecastPath, String(text.c_str()))) LOG_ERR(kTag, "could not write %s", kForecastPath);
  LOG_INF(kTag, "forecast: %d hours, %d days, offset %ld", forecast_->hourCount, forecast_->dayCount,
          static_cast<long>(forecast_->utcOffset));
  return true;
}

bool WeatherActivity::searchPlaces() {
  char url[320];
  if (!weather::searchUrl(query_.c_str(), url, sizeof(url))) return false;
  std::string body;
  if (!HttpDownloader::fetchUrl(url, body)) {
    LOG_ERR(kTag, "search failed (%d)", HttpDownloader::lastStatus());
    return false;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    LOG_ERR(kTag, "search parse error: %s", err.c_str());
    return false;
  }

  RenderLock lock(*this);
  places_.clear();
  places_.reserve(kMaxPlaces);
  for (JsonObjectConst r : doc["results"].as<JsonArrayConst>()) {
    if (static_cast<int>(places_.size()) >= kMaxPlaces) break;
    weather::Place p;
    foldToAscii(r["name"] | "", p.name, sizeof(p.name));
    if (p.name[0] == '\0') continue;
    char admin[weather::kRegionCap];
    char country[weather::kRegionCap];
    foldToAscii(r["admin1"] | "", admin, sizeof(admin));
    foldToAscii(r["country"] | "", country, sizeof(country));
    weather::joinRegion(admin, country, p.region, sizeof(p.region));
    p.lat = r["latitude"] | 0.0;
    p.lon = r["longitude"] | 0.0;
    places_.push_back(p);
  }
  placeItems_.clear();
  placeItems_.reserve(places_.size());
  for (size_t i = 0; i < places_.size(); ++i) {
    fui::ListItem item;
    item.label = places_[i].name;
    item.subtitle = places_[i].region;
    item.actionValue = static_cast<int16_t>(i);
    placeItems_.push_back(item);
  }
  view_ = View::Places;
  LOG_INF(kTag, "search '%s': %d places", query_.c_str(), static_cast<int>(places_.size()));
  return true;
}

// --- Places and settings ---------------------------------------------------

void WeatherActivity::askForPlace() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "SEARCH A CITY", query_, kQueryMax);
  if (!keyboard) {
    showNotice("OUT OF MEMORY", "There was not enough memory to open the keyboard.", nullptr, fui::NO_ACTION);
    requestUpdate();
    return;
  }
  backPressSeen_ = false;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    interactionsReady_ = false;
    if (!result.isCancelled) {
      const auto& entered = std::get<KeyboardResult>(result.data);
      char query[kQueryMax + 1];
      foldToAscii(entered.text.c_str(), query, sizeof(query));
      query_ = query;
      if (!query_.empty()) {
        request(Pending::Search);
        return;
      }
    }
    if (view_ == View::Notice && !config_.hasPlace) {
      requestUpdate();
      return;
    }
    view_ = View::Settings;
    requestUpdate();
  });
}

void WeatherActivity::pickPlace(const int index) {
  if (index < 0 || index >= static_cast<int>(places_.size())) return;
  const weather::Place& p = places_[static_cast<size_t>(index)];
  std::snprintf(config_.name, sizeof(config_.name), "%s", p.name);
  std::snprintf(config_.region, sizeof(config_.region), "%s", p.region);
  config_.lat = p.lat;
  config_.lon = p.lon;
  config_.hasPlace = true;
  saveConfig();
  haveForecast_ = false;
  status_ = Status::Idle;
  request(Pending::Forecast);
}

void WeatherActivity::saveConfig() {
  const std::string text = weather::formatConfig(config_);
  if (!Storage.writeFile(kConfigPath, String(text.c_str()))) LOG_ERR(kTag, "could not write %s", kConfigPath);
}

void WeatherActivity::showNotice(const char* headline, const char* message, const char* actionLabel,
                                 const fui::ActionId action) {
  RenderLock lock(*this);
  noticeHeadline_ = headline;
  noticeMessage_ = message != nullptr ? message : "";
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
}

void WeatherActivity::openForecast() {
  if (!config_.hasPlace) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (!haveForecast_) {
    request(Pending::Forecast);
    return;
  }
  view_ = View::Forecast;
  requestUpdate();
}

// --- Input -----------------------------------------------------------------

void WeatherActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen_ = true;

  if (pending_ != Pending::None) {
    const Pending what = pending_;
    pending_ = Pending::None;
    ensureConnected(what);
    return;
  }

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    switch (view_) {
      case View::Forecast:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Settings:
        openForecast();
        return;
      case View::Places:
        view_ = View::Settings;
        requestUpdate();
        return;
      case View::Notice:
        if (config_.hasPlace && haveForecast_) {
          view_ = View::Forecast;
          requestUpdate();
        } else {
          shelf::leave(renderer, mappedInput);
        }
        return;
    }
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);

  switch (event.action) {
    case weatherui::ActionSettings:
      view_ = View::Settings;
      requestUpdate();
      break;
    case weatherui::ActionRefresh:
      if (config_.hasPlace) request(Pending::Forecast);
      break;
    case weatherui::ActionSettingsDone:
      openForecast();
      break;
    case weatherui::ActionSettingsRow:
      switch (static_cast<weatherui::SettingsRow>(event.value)) {
        case weatherui::SettingsRow::Location:
          askForPlace();
          return;
        case weatherui::SettingsRow::Temperature:
          config_.temp = weather::nextTemp(config_.temp);
          break;
        case weatherui::SettingsRow::Wind:
          config_.wind = weather::nextWind(config_.wind);
          break;
        case weatherui::SettingsRow::Rain:
          config_.rain = weather::nextRain(config_.rain);
          break;
        case weatherui::SettingsRow::Count:
          return;
      }
      saveConfig();
      requestUpdate();
      break;
    case weatherui::ActionPickPlace:
      pickPlace(event.value);
      break;
    case weatherui::ActionSearchAgain:
    case weatherui::ActionNotice:
      askForPlace();
      break;
    default:
      break;
  }
}

// --- Drawing ---------------------------------------------------------------

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);
  const char* what = "Weather";

  switch (view_) {
    case View::Forecast: {
      char title[weather::kNameCap];
      upper(config_.name, title, sizeof(title));
      const fui::Rect body = weatherui::buildForecastChrome(screen, title);
      drawForecast(screen, body);
      what = "Weather forecast";
      break;
    }
    case View::Settings: {
      settingsLocation_ = config_.hasPlace ? config_.name : "";
      weatherui::SettingsModel model;
      model.location = config_.hasPlace ? settingsLocation_.c_str() : nullptr;
      model.temperature = weather::tempUnitName(config_.temp);
      model.wind = weather::windUnitLabel(config_.wind);
      model.rain = weather::rainUnitLabel(config_.rain);
      weatherui::buildSettings(screen, model);
      what = "Weather settings";
      break;
    }
    case View::Places: {
      weatherui::PlacesModel model;
      model.items = placeItems_.empty() ? nullptr : placeItems_.data();
      model.count = static_cast<int>(placeItems_.size());
      weatherui::buildPlaces(screen, model);
      what = "Weather places";
      break;
    }
    case View::Notice: {
      weatherui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.actionLabel = noticeAction_;
      model.action = noticeActionId_;
      weatherui::buildNotice(screen, model);
      what = "Weather notice";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);
  renderer.displayBuffer();
}

void WeatherActivity::drawForecast(toybox::Screen& screen, const fui::Rect& body) {
  (void)screen;
  const Forecast& f = *forecast_;
  const int64_t now = nowEpoch();
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  const int left = toybox::kMargin;
  const int right = W - toybox::kMargin;
  const weather::TempUnit tu = config_.temp;

  const int today = weather::dayIndexAt(f, now);
  const weather::Day& d0 = f.days[today];
  const weather::Now nowWx = weather::nowFor(f, now);

  // --- Footer: when this report is from, and whether it is being replaced.
  const int footFont = toybox::kButtonFontId;
  const int footBase = H - toybox::kMargin;
  {
    char when[12];
    weather::formatClock(f.fetchedAt, f.utcOffset, twelveHour(), when, sizeof(when));
    const char* day =
        weather::localDay(now, f.utcOffset) == weather::localDay(f.fetchedAt, f.utcOffset) || now < weather::kClockFloor
            ? "TODAY"
            : weather::weekday(f.fetchedAt, f.utcOffset);
    char line[64];
    switch (status_) {
      case Status::Updating:
        std::snprintf(line, sizeof(line), "UPDATING...");
        break;
      case Status::Failed:
        std::snprintf(line, sizeof(line), "COULD NOT UPDATE. FROM %s %s", day, when);
        break;
      case Status::NoWifi:
        std::snprintf(line, sizeof(line), "NO WI-FI. FROM %s %s", day, when);
        break;
      case Status::Idle:
        std::snprintf(line, sizeof(line), "UPDATED %s %s", day, when);
        break;
    }
    textAt(renderer, footFont, left, footBase, line);
    const char* credit = "OPEN-METEO";
    textAt(renderer, footFont, right - renderer.getTextWidth(footFont, credit), footBase, credit);
  }
  const int footTop = footBase - toybox::metricsFor(footFont).capHeight - toybox::kGutter;

  // --- Today.
  int y = body.y + toybox::kGutter * 2;
  const freeink::Icon& hero = skyIcon(weather::skyFor(nowWx.code), nowWx.isDay, IconSize::Hero);
  drawIcon(renderer, hero, left - 4, y);

  const int colX = left + hero.w + toybox::kGutter;
  const int hugeCap = toybox::metricsFor(toybox::kHugeFontId).capHeight;
  const int tempBase = y + 8 + hugeCap;
  drawTemp(renderer, toybox::kHugeFontId, colX, tempBase, weather::toTemp(nowWx.temp, tu));

  const int uiCap = toybox::metricsFor(toybox::kUiFontId).capHeight;
  int lineBase = tempBase + toybox::kGutter + uiCap + 6;
  textAt(renderer, toybox::kUiFontId, colX, lineBase, weather::conditionLabel(nowWx.code));
  lineBase += uiCap + toybox::kGutter;
  {
    int x = colX;
    x += textAt(renderer, toybox::kUiFontId, x, lineBase, "H ");
    x += drawTemp(renderer, toybox::kUiFontId, x, lineBase, weather::toTemp(d0.high, tu));
    x += textAt(renderer, toybox::kUiFontId, x, lineBase, "  L ");
    drawTemp(renderer, toybox::kUiFontId, x, lineBase, weather::toTemp(d0.low, tu));
  }
  y = std::max(y + static_cast<int>(hero.h), lineBase) + toybox::kGutter * 2;

  // Four facts about today, each behind its own mark, spread so the gaps
  // between them are equal whatever the units make them.
  {
    const int font = toybox::kButtonFontId;
    const int cap = toybox::metricsFor(font).capHeight;
    constexpr int kMark = 24;
    constexpr int kMarkGap = 5;
    const int iconY = y;
    const int base = iconY + kMark / 2 + cap / 2;
    char text[4][16];
    std::snprintf(text[0], sizeof(text[0]), "%d%%", d0.rainChance);
    std::snprintf(text[1], sizeof(text[1]), "%d %s", weather::toWind(d0.windMax, config_.wind),
                  weather::windUnitLabel(config_.wind));
    weather::formatClock(d0.sunrise, f.utcOffset, twelveHour(), text[2], sizeof(text[2]));
    weather::formatClock(d0.sunset, f.utcOffset, twelveHour(), text[3], sizeof(text[3]));
    const freeink::Icon* marks[4] = {&icon_wx_drop_24, &icon_wx_wind_24, &icon_wx_sunrise_24, &icon_wx_sunset_24};
    int widths[4];
    int total = 0;
    for (int i = 0; i < 4; ++i) {
      widths[i] = kMark + kMarkGap + renderer.getTextWidth(font, text[i]);
      total += widths[i];
    }
    const int gap = std::max(4, (right - left - total) / 3);
    int x = left;
    for (int i = 0; i < 4; ++i) {
      drawIcon(renderer, *marks[i], x, iconY);
      textAt(renderer, font, x + kMark + kMarkGap, base, text[i]);
      x += widths[i] + gap;
    }
    y = iconY + kMark + toybox::kGutter;
  }
  renderer.fillRect(left, y, right - left, toybox::kRule, true);
  y += toybox::kRule + toybox::kGutter;

  // --- The next 24 hours, every three.
  if (f.hourCount > 0) {
    constexpr int kColumns = 8;
    const int colW = (right - left) / kColumns;
    const int first = weather::hourIndexAt(f, now);
    const int labelCap = toybox::metricsFor(toybox::kButtonFontId).capHeight;
    const int labelBase = y + labelCap;
    const int iconY = labelBase + 8;
    const int tempBase = iconY + 48 + 6 + uiCap;
    for (int c = 0; c < kColumns; ++c) {
      const int i = first + c * 3;
      if (i >= f.hourCount) break;
      const weather::Hour& h = f.hours[i];
      const int cx = left + c * colW + colW / 2;
      char label[8];
      if (c == 0) {
        std::snprintf(label, sizeof(label), "NOW");
      } else {
        weather::formatHour(h.time, f.utcOffset, twelveHour(), label, sizeof(label));
      }
      textCentred(renderer, toybox::kButtonFontId, cx, labelBase, label);
      const freeink::Icon& icon = skyIcon(weather::skyFor(h.code), h.day, IconSize::Medium);
      drawIcon(renderer, icon, cx - icon.w / 2, iconY);
      drawTempCentred(renderer, toybox::kUiFontId, cx, tempBase, weather::toTemp(h.temp, tu));
    }
    y = tempBase + toybox::kGutter;
    renderer.fillRect(left, y, right - left, toybox::kHairline * 2, true);
    y += toybox::kGutter;
  }

  // --- The ten days after today.
  const int firstDay = today + 1;
  const int dayCount = std::min(10, f.dayCount - firstDay);
  if (dayCount <= 0) return;
  float lo = f.days[firstDay].low;
  float hi = f.days[firstDay].high;
  for (int i = firstDay; i < firstDay + dayCount; ++i) {
    lo = std::min(lo, f.days[i].low);
    hi = std::max(hi, f.days[i].high);
  }
  const float span = hi - lo > 0.5f ? hi - lo : 0.5f;
  const int areaH = footTop - y;

  // One row a day: name, sky, rain chance, then low and high either side of
  // a bar placed on the ten days' shared scale.
  const int rowH = areaH / dayCount;
  const int nameFont = toybox::kButtonFontId;
  const int nameCap = toybox::metricsFor(nameFont).capHeight;
  const int smallFont = toybox::kButtonFontId;
  const int smallCap = toybox::metricsFor(smallFont).capHeight;
  const int iconX = left + renderer.getTextWidth(nameFont, "WED") + toybox::kGutter;
  const int rainX = iconX + 32 + 10;
  const int loRight = rainX + 46 + 52;
  const int barX = loRight + 12;
  const int hiX = right - 52;
  const int barW = hiX - 12 - barX;
  for (int k = 0; k < dayCount; ++k) {
    const weather::Day& d = f.days[firstDay + k];
    const int ry = y + k * rowH;
    const int mid = ry + rowH / 2;
    const int base = mid + nameCap / 2;
    textAt(renderer, nameFont, left, base, weather::weekday(d.time, f.utcOffset));
    const freeink::Icon& icon = skyIcon(weather::skyFor(d.code), true, IconSize::Small);
    drawIcon(renderer, icon, iconX, mid - icon.h / 2);
    if (d.rainChance >= 10) {
      char rain[16];
      std::snprintf(rain, sizeof(rain), "%d%%", d.rainChance);
      textAt(renderer, smallFont, rainX, mid + smallCap / 2, rain);
    }
    drawTempRight(renderer, nameFont, loRight, base, weather::toTemp(d.low, tu));
    const int x0 = barX + static_cast<int>((d.low - lo) / span * static_cast<float>(barW));
    const int x1 = barX + static_cast<int>((d.high - lo) / span * static_cast<float>(barW));
    const int barH = 10;
    renderer.fillRoundedRect(barX, mid - 2, barW, 4, 2, LightGray);
    renderer.fillRoundedRect(x0, mid - barH / 2, std::max(x1 - x0, barH), barH, barH / 2, Black);
    drawTemp(renderer, nameFont, hiX, base, weather::toTemp(d.high, tu));
    if (k > 0) renderer.fillRect(left, ry, right - left, toybox::kHairline, true);
  }
}
