// Weather's freestanding rules. See src/apps_local/weather/WeatherCore.h.

#include <cstdio>
#include <cstring>
#include <string>

#include "WeatherCore.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

void checkStr(const char* got, const char* want, const char* what) {
  ++checks;
  if (std::strcmp(got, want) != 0) {
    ++failures;
    std::printf("FAIL: %s: got \"%s\", want \"%s\"\n", what, got, want);
  }
}

using namespace weather;

// 2026-10-08 00:00 in Berlin (UTC+2) is 2026-10-07 22:00 UTC.
constexpr int64_t kBerlinMidnight = 1791410400;
constexpr int32_t kBerlinOffset = 7200;

Forecast sample() {
  Forecast f;
  f.fetchedAt = kBerlinMidnight + 8 * 3600 + 15 * 60;  // 08:15 local
  f.utcOffset = kBerlinOffset;
  f.lat = 52.52;
  f.lon = 13.405;
  f.temp = 12.3f;
  f.feels = 10.9f;
  f.wind = 14.0f;
  f.humidity = 81;
  f.code = 2;
  f.isDay = true;
  for (int i = 0; i < kHours; ++i) {
    Hour& h = f.hours[f.hourCount++];
    h.time = kBerlinMidnight + (8 + i) * 3600;
    h.temp = 10.0f + static_cast<float>(i % 12);
    h.code = static_cast<int16_t>(i < 6 ? 3 : 61);
    h.rainChance = static_cast<uint8_t>(i * 2);
    h.day = (8 + i) % 24 >= 7 && (8 + i) % 24 < 19;
  }
  for (int i = 0; i < kDays; ++i) {
    Day& d = f.days[f.dayCount++];
    d.time = kBerlinMidnight + i * 86400;
    d.code = static_cast<int16_t>(i % 2 == 0 ? 2 : 63);
    d.high = 15.5f + static_cast<float>(i);
    d.low = 6.0f - static_cast<float>(i) * 0.5f;
    d.rainChance = static_cast<uint8_t>(i * 9);
    d.rainMm = 0.4f * static_cast<float>(i);
    d.windMax = 20.0f;
    d.sunrise = d.time + 7 * 3600 + 25 * 60;
    d.sunset = d.time + 18 * 3600 + 40 * 60;
  }
  return f;
}

void testConfig() {
  Config c;
  c.hasPlace = true;
  std::snprintf(c.name, sizeof(c.name), "Berlin");
  std::snprintf(c.region, sizeof(c.region), "Land Berlin, Germany");
  c.lat = 52.52437;
  c.lon = 13.41053;
  c.temp = TempUnit::Fahrenheit;
  c.wind = WindUnit::Knots;
  c.rain = RainUnit::Inches;
  Config back;
  parseConfig(formatConfig(c).c_str(), back);
  check(back.hasPlace, "config keeps the place");
  checkStr(back.name, "Berlin", "config name");
  checkStr(back.region, "Land Berlin, Germany", "config region");
  check(back.lat > 52.524 && back.lat < 52.525, "config lat");
  check(back.lon > 13.410 && back.lon < 13.411, "config lon");
  check(back.temp == TempUnit::Fahrenheit, "config temp unit");
  check(back.wind == WindUnit::Knots, "config wind unit");
  check(back.rain == RainUnit::Inches, "config rain unit");

  Config units;
  parseConfig("temp=1\nwind=2\n", units);
  check(!units.hasPlace, "units alone are not a place");
  check(units.temp == TempUnit::Fahrenheit && units.wind == WindUnit::Ms, "units without a place still load");

  Config junk;
  parseConfig("lat=200\nlon=10\nname=X\ntemp=9\nfuture=yes\n", junk);
  check(!junk.hasPlace, "an impossible latitude is not a place");
  check(junk.temp == TempUnit::Celsius, "an unknown unit keeps the default");

  Config split;
  std::snprintf(split.name, sizeof(split.name), "A\nB");
  split.hasPlace = true;
  Config splitBack;
  parseConfig(formatConfig(split).c_str(), splitBack);
  checkStr(splitBack.name, "A", "a name with a line break cannot inject a key");
}

void testForecastFile() {
  const Forecast f = sample();
  Forecast back;
  check(parseForecast(formatForecast(f).c_str(), back), "forecast parses back");
  check(back.fetchedAt == f.fetchedAt && back.utcOffset == f.utcOffset, "forecast fetch time and offset");
  check(back.hourCount == kHours && back.dayCount == kDays, "forecast row counts");
  check(back.days[3].time == f.days[3].time && back.days[3].code == 63, "forecast day row");
  check(back.days[10].sunset == f.days[10].sunset, "forecast last day sunset");
  check(back.hours[47].time == f.hours[47].time && back.hours[47].rainChance == 94, "forecast last hour");
  check(back.humidity == 81 && back.code == 2, "forecast now line");
  check(back.days[2].low > 4.9f && back.days[2].low < 5.1f, "forecast keeps a decimal");

  Forecast none;
  check(!parseForecast("", none), "empty file is no forecast");
  check(!parseForecast("WX0\nat 1 2 3 4\n", none), "wrong version is no forecast");
  std::string cut = formatForecast(f);
  cut.resize(cut.find("\nd "));
  check(!parseForecast(cut.c_str(), none), "a file with no days is no forecast");
  std::string torn = formatForecast(f);
  torn.resize(torn.size() - 20);
  check(parseForecast(torn.c_str(), none) && none.dayCount == kDays - 1, "a torn last line is dropped, not misread");
}

void testFreshness() {
  const Forecast f = sample();
  Config c;
  c.hasPlace = true;
  c.lat = 52.52;
  c.lon = 13.405;
  check(freshness(nullptr, c, f.fetchedAt) == Freshness::NoForecast, "nothing cached");
  check(freshness(&f, c, f.fetchedAt + 3600) == Freshness::Fresh, "an hour later is the same report");
  check(freshness(&f, c, kBerlinMidnight + 86400 - 1) == Freshness::Fresh, "23:59 local is still today");
  check(freshness(&f, c, kBerlinMidnight + 86400) == Freshness::NewDay, "local midnight starts a new day");
  check(freshness(&f, c, 1000) == Freshness::NoClock, "no clock asks the server");
  Config moved = c;
  moved.lat = 48.137;
  check(freshness(&f, moved, f.fetchedAt) == Freshness::NewPlace, "a new place fetches");

  // 23:30 UTC is still the 7th in UTC but already the 8th in Berlin.
  Forecast late = f;
  late.fetchedAt = kBerlinMidnight - 1800;
  check(freshness(&late, c, kBerlinMidnight + 60) == Freshness::NewDay, "the place's midnight, not UTC's");
}

void testIndices() {
  const Forecast f = sample();
  check(hourIndexAt(f, f.fetchedAt) == 0, "first hour at fetch");
  check(hourIndexAt(f, f.hours[5].time + 1799) == 5, "inside hour five");
  check(hourIndexAt(f, f.hours[47].time + 99999) == 47, "past the end clamps");
  check(hourIndexAt(f, 5) == 0, "no clock is the first hour");
  check(dayIndexAt(f, f.fetchedAt) == 0, "today is row zero");
  check(dayIndexAt(f, kBerlinMidnight + 86400 + 60) == 1, "yesterday's report makes today row one");

  const Now same = nowFor(f, f.fetchedAt + 600);
  check(same.code == 2 && same.temp > 12.2f && same.temp < 12.4f, "inside the fetch hour, now is the measurement");
  const Now later = nowFor(f, f.hours[10].time + 60);
  check(later.code == 61 && later.temp > 19.9f && later.temp < 20.1f, "later, now is the hourly forecast");
}

void testCodesAndUnits() {
  check(skyFor(0) == Sky::Clear && skyFor(2) == Sky::PartlyCloudy && skyFor(45) == Sky::Fog, "sky groups");
  check(skyFor(81) == Sky::Showers && skyFor(86) == Sky::Snow && skyFor(99) == Sky::Storm, "sky groups, wet");
  check(skyFor(1234) == Sky::Cloudy, "unknown code is cloudy");
  for (int code = 0; code < 100; ++code) {
    check(std::strlen(conditionLabel(code)) <= 16, "condition labels fit the hero");
  }

  check(toTemp(0, TempUnit::Fahrenheit) == 32 && toTemp(-40, TempUnit::Fahrenheit) == -40, "fahrenheit");
  check(toTemp(21.6f, TempUnit::Celsius) == 22 && toTemp(-0.4f, TempUnit::Celsius) == 0, "celsius rounds");
  check(toWind(36, WindUnit::Ms) == 10 && toWind(100, WindUnit::Mph) == 62 && toWind(37, WindUnit::Knots) == 20,
        "wind conversions");
  check(nextWind(WindUnit::Knots) == WindUnit::Kmh, "wind cycles back");

  char rain[16];
  formatRain(0, RainUnit::Mm, rain, sizeof(rain));
  checkStr(rain, "0 MM", "dry day");
  formatRain(2.44f, RainUnit::Mm, rain, sizeof(rain));
  checkStr(rain, "2.4 MM", "rain mm");
  formatRain(25.4f, RainUnit::Inches, rain, sizeof(rain));
  checkStr(rain, "1.00 IN", "rain inches");
}

void testText() {
  checkStr(weekday(kBerlinMidnight, kBerlinOffset), "THU", "2026-10-08 is a Thursday");
  checkStr(weekday(kBerlinMidnight + 3 * 86400, kBerlinOffset), "SUN", "three days on");
  check(dayOfMonth(kBerlinMidnight, kBerlinOffset) == 8, "day of month");
  check(dayOfMonth(kBerlinMidnight + 24 * 86400, kBerlinOffset) == 1, "month rolls");

  char text[12];
  formatHour(kBerlinMidnight + 14 * 3600, kBerlinOffset, false, text, sizeof(text));
  checkStr(text, "14", "24h hour");
  formatHour(kBerlinMidnight + 14 * 3600, kBerlinOffset, true, text, sizeof(text));
  checkStr(text, "2PM", "12h hour");
  formatHour(kBerlinMidnight, kBerlinOffset, true, text, sizeof(text));
  checkStr(text, "12AM", "12h midnight");
  formatClock(kBerlinMidnight + 7 * 3600 + 5 * 60, kBerlinOffset, true, text, sizeof(text));
  checkStr(text, "7:05AM", "12h clock");
  formatClock(kBerlinMidnight + 19 * 3600 + 41 * 60, kBerlinOffset, false, text, sizeof(text));
  checkStr(text, "19:41", "24h clock");

  char region[64];
  joinRegion("Bavaria", "Germany", region, sizeof(region));
  checkStr(region, "Bavaria, Germany", "region joined");
  joinRegion("Singapore", "Singapore", region, sizeof(region));
  checkStr(region, "Singapore", "region not doubled");
  joinRegion("", "France", region, sizeof(region));
  checkStr(region, "France", "country alone");
}

void testUrls() {
  char url[600];
  check(forecastUrl(52.52, -13.405, url, sizeof(url)), "forecast url fits");
  check(std::strstr(url, "latitude=52.5200&longitude=-13.4050") != nullptr, "forecast url coordinates");
  check(std::strstr(url, "forecast_days=11") != nullptr && std::strstr(url, "timeformat=unixtime") != nullptr,
        "forecast url shape");
  check(!forecastUrl(1, 2, url, 40), "a short buffer refuses");

  char search[200];
  check(searchUrl("New York", search, sizeof(search)), "search url");
  check(std::strstr(search, "name=New%20York") != nullptr, "search escapes a space");
  check(searchUrl("M\xC3\xBCnchen", search, sizeof(search)) && std::strstr(search, "M%C3%BCnchen") != nullptr,
        "search escapes UTF-8");
  check(!searchUrl("   ", search, sizeof(search)), "blank search refuses");
}

}  // namespace

int main() {
  testConfig();
  testForecastFile();
  testFreshness();
  testIndices();
  testCodesAndUnits();
  testText();
  testUrls();
  std::printf("weather: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
