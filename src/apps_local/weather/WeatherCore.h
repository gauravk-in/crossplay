#pragma once

// Weather: the rules, with no radio, no card and no panel.
//
// The forecast comes from Open-Meteo (free, no key) and is kept on the card as
// one small text file. The app fetches once per local day, the first time it
// is opened that day, and shows that report until the user taps refresh. Which
// day it is belongs to the forecast's own place, not to the reader's clock
// setting: a forecast for Tokyo turns over at Tokyo's midnight.
//
// Everything is stored metric and converted when drawn, so changing a unit is
// a repaint and never a fetch.
//
// Freestanding: host-tests/weather builds this file with nothing but the
// standard library.

#include <cstddef>
#include <cstdint>
#include <string>

namespace weather {

enum class TempUnit : uint8_t { Celsius, Fahrenheit };
enum class WindUnit : uint8_t { Kmh, Mph, Ms, Knots };
enum class RainUnit : uint8_t { Mm, Inches };

constexpr size_t kNameCap = 48;
constexpr size_t kRegionCap = 64;

// Where the forecast is for, and how it is shown. /.crosspoint/weather.cfg.
struct Config {
  bool hasPlace = false;
  char name[kNameCap] = "";
  char region[kRegionCap] = "";
  double lat = 0;
  double lon = 0;
  TempUnit temp = TempUnit::Celsius;
  WindUnit wind = WindUnit::Kmh;
  RainUnit rain = RainUnit::Mm;
};

std::string formatConfig(const Config& config);
// Unknown keys and bad values are skipped, so a file written by a later
// build still loads what this one understands.
void parseConfig(const char* text, Config& config);

// One place a search returned.
struct Place {
  char name[kNameCap] = "";
  char region[kRegionCap] = "";
  double lat = 0;
  double lon = 0;
};

// --- The forecast ---------------------------------------------------------

constexpr int kHours = 48;
// Today and the ten after it.
constexpr int kDays = 11;

struct Hour {
  int64_t time = 0;  // UTC epoch of the hour's start
  float temp = 0;    // C
  int16_t code = 0;  // WMO weather code
  uint8_t rainChance = 0;
  bool day = true;
};

struct Day {
  int64_t time = 0;  // UTC epoch of the place's local midnight
  int16_t code = 0;
  float high = 0;
  float low = 0;
  uint8_t rainChance = 0;
  float rainMm = 0;
  float windMax = 0;  // km/h
  int64_t sunrise = 0;
  int64_t sunset = 0;
};

struct Forecast {
  int64_t fetchedAt = 0;  // UTC epoch, from the server's own "current" time
  int32_t utcOffset = 0;  // seconds, the place's offset when fetched
  double lat = 0;
  double lon = 0;
  // Conditions when fetched.
  float temp = 0;
  float feels = 0;
  float wind = 0;  // km/h
  uint8_t humidity = 0;
  int16_t code = 0;
  bool isDay = true;
  Hour hours[kHours];
  int hourCount = 0;
  Day days[kDays];
  int dayCount = 0;
};

std::string formatForecast(const Forecast& forecast);
bool parseForecast(const char* text, Forecast& forecast);

// Below this, time(nullptr) is a device that has never been told the date.
constexpr int64_t kClockFloor = 1700000000;

// Days since 1970-01-01 in the place's own time.
int64_t localDay(int64_t epoch, int32_t utcOffset);

enum class Freshness : uint8_t {
  Fresh,       // fetched today, for this place
  NewDay,      // the place has passed midnight since
  NewPlace,    // the location was changed since
  NoClock,     // the device cannot say what day it is, so ask the server
  NoForecast,  // nothing on the card
};

Freshness freshness(const Forecast* cached, const Config& config, int64_t now);
inline bool needsFetch(const Freshness f) { return f != Freshness::Fresh; }

// The hour row covering `now`, or 0 when the clock is unset or before the data.
int hourIndexAt(const Forecast& forecast, int64_t now);
// The day row covering `now`: 0 is the day it was fetched, and a report from
// yesterday makes today row 1. Clamped to the rows that exist.
int dayIndexAt(const Forecast& forecast, int64_t now);

// What "now" looks like: the fetch-time conditions while still inside the
// hour they were measured in, the hourly forecast after that.
struct Now {
  float temp = 0;
  int16_t code = 0;
  bool isDay = true;
};
Now nowFor(const Forecast& forecast, int64_t now);

// --- WMO weather codes ----------------------------------------------------

enum class Sky : uint8_t { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Showers, Snow, Storm };

Sky skyFor(int code);
// Upper case, at most 15 characters: drawn in the display cut beside today.
const char* conditionLabel(int code);

// --- Units and text -------------------------------------------------------

int toTemp(float celsius, TempUnit unit);
int toWind(float kmh, WindUnit unit);
const char* tempUnitLetter(TempUnit unit);  // "C" / "F"
const char* tempUnitName(TempUnit unit);    // "CELSIUS"
const char* windUnitLabel(WindUnit unit);   // "KM/H"
const char* rainUnitLabel(RainUnit unit);   // "MM"
TempUnit nextTemp(TempUnit unit);
WindUnit nextWind(WindUnit unit);
RainUnit nextRain(RainUnit unit);
// "2.4 MM", "0.09 IN"; "0 MM" for a dry day.
void formatRain(float mm, RainUnit unit, char* out, size_t cap);

// "MON".."SUN" for a local midnight.
const char* weekday(int64_t localMidnight, int32_t utcOffset);
int dayOfMonth(int64_t epoch, int32_t utcOffset);
// "14" or "2PM", the way a forecast strip labels its hours; `out` needs 6 bytes.
void formatHour(int64_t epoch, int32_t utcOffset, bool twelveHour, char* out, size_t cap);
// "14:05" or "2:05PM"; `out` needs 8 bytes.
void formatClock(int64_t epoch, int32_t utcOffset, bool twelveHour, char* out, size_t cap);

// --- Requests -------------------------------------------------------------

// Writes the request URL. Returns false if it did not fit.
bool forecastUrl(double lat, double lon, char* out, size_t cap);
bool searchUrl(const char* query, char* out, size_t cap);

// The second line of a place: "Bavaria, Germany", or whichever half exists.
void joinRegion(const char* admin, const char* country, char* out, size_t cap);

}  // namespace weather
