#include "WeatherCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace weather {

namespace {

// Copies a value into a fixed field, dropping line breaks so a name can never
// split the file it is written into.
void copyField(char* out, const size_t cap, const char* text, const size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < len && n + 1 < cap; ++i) {
    const char c = text[i];
    if (c == '\n' || c == '\r') continue;
    out[n++] = c;
  }
  out[n] = '\0';
}

bool sameKey(const char* key, const size_t keyLen, const char* want) {
  return std::strlen(want) == keyLen && std::strncmp(key, want, keyLen) == 0;
}

// Reads whitespace-separated numbers off a line, in order. A short line leaves
// `ok` false so the caller can drop it.
struct Fields {
  const char* at;
  bool ok = true;

  explicit Fields(const char* text) : at(text) {}

  long long integer() {
    char* end = nullptr;
    const long long v = std::strtoll(at, &end, 10);
    if (end == at) ok = false;
    at = end;
    return v;
  }
  double real() {
    char* end = nullptr;
    const double v = std::strtod(at, &end);
    if (end == at) ok = false;
    at = end;
    return v;
  }
};

int64_t floorDiv(const int64_t a, const int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

bool placeMatches(const Forecast& f, const Config& c) {
  return std::fabs(f.lat - c.lat) < 0.01 && std::fabs(f.lon - c.lon) < 0.01;
}

int roundToInt(const double v) { return static_cast<int>(std::lround(v)); }

}  // namespace

// --- Config ---------------------------------------------------------------

std::string formatConfig(const Config& config) {
  char line[160];
  std::string out;
  out.reserve(256);
  if (config.hasPlace) {
    std::snprintf(line, sizeof(line), "name=%s\nregion=%s\nlat=%.4f\nlon=%.4f\n", config.name, config.region,
                  config.lat, config.lon);
    out += line;
  }
  std::snprintf(line, sizeof(line), "temp=%d\nwind=%d\nrain=%d\n", static_cast<int>(config.temp),
                static_cast<int>(config.wind), static_cast<int>(config.rain));
  out += line;
  return out;
}

void parseConfig(const char* text, Config& config) {
  if (text == nullptr) return;
  bool haveLat = false;
  bool haveLon = false;
  const char* at = text;
  while (*at != '\0') {
    const char* end = std::strchr(at, '\n');
    const size_t len = end != nullptr ? static_cast<size_t>(end - at) : std::strlen(at);
    const char* eq = static_cast<const char*>(std::memchr(at, '=', len));
    if (eq != nullptr) {
      const size_t keyLen = static_cast<size_t>(eq - at);
      const char* value = eq + 1;
      const size_t valueLen = len - keyLen - 1;
      char number[32];
      copyField(number, sizeof(number), value, valueLen);
      if (sameKey(at, keyLen, "name")) {
        copyField(config.name, sizeof(config.name), value, valueLen);
      } else if (sameKey(at, keyLen, "region")) {
        copyField(config.region, sizeof(config.region), value, valueLen);
      } else if (sameKey(at, keyLen, "lat")) {
        char* stop = nullptr;
        const double v = std::strtod(number, &stop);
        if (stop != number && v >= -90 && v <= 90) {
          config.lat = v;
          haveLat = true;
        }
      } else if (sameKey(at, keyLen, "lon")) {
        char* stop = nullptr;
        const double v = std::strtod(number, &stop);
        if (stop != number && v >= -180 && v <= 180) {
          config.lon = v;
          haveLon = true;
        }
      } else if (sameKey(at, keyLen, "temp")) {
        const int v = std::atoi(number);
        if (v >= 0 && v <= 1) config.temp = static_cast<TempUnit>(v);
      } else if (sameKey(at, keyLen, "wind")) {
        const int v = std::atoi(number);
        if (v >= 0 && v <= 3) config.wind = static_cast<WindUnit>(v);
      } else if (sameKey(at, keyLen, "rain")) {
        const int v = std::atoi(number);
        if (v >= 0 && v <= 1) config.rain = static_cast<RainUnit>(v);
      }
    }
    if (end == nullptr) break;
    at = end + 1;
  }
  config.hasPlace = haveLat && haveLon && config.name[0] != '\0';
}

// --- Forecast file --------------------------------------------------------

std::string formatForecast(const Forecast& f) {
  std::string out;
  out.reserve(static_cast<size_t>(160 + f.hourCount * 40 + f.dayCount * 80));
  char line[160];
  out += "WX1\n";
  std::snprintf(line, sizeof(line), "at %lld %ld %.4f %.4f\n", static_cast<long long>(f.fetchedAt),
                static_cast<long>(f.utcOffset), f.lat, f.lon);
  out += line;
  std::snprintf(line, sizeof(line), "now %.1f %.1f %.1f %d %d %d\n", static_cast<double>(f.temp),
                static_cast<double>(f.feels), static_cast<double>(f.wind), f.humidity, f.code, f.isDay ? 1 : 0);
  out += line;
  for (int i = 0; i < f.hourCount; ++i) {
    const Hour& h = f.hours[i];
    std::snprintf(line, sizeof(line), "h %lld %.1f %d %d %d\n", static_cast<long long>(h.time),
                  static_cast<double>(h.temp), h.code, h.rainChance, h.day ? 1 : 0);
    out += line;
  }
  for (int i = 0; i < f.dayCount; ++i) {
    const Day& d = f.days[i];
    std::snprintf(line, sizeof(line), "d %lld %d %.1f %.1f %d %.1f %.1f %lld %lld\n", static_cast<long long>(d.time),
                  d.code, static_cast<double>(d.high), static_cast<double>(d.low), d.rainChance,
                  static_cast<double>(d.rainMm), static_cast<double>(d.windMax), static_cast<long long>(d.sunrise),
                  static_cast<long long>(d.sunset));
    out += line;
  }
  return out;
}

bool parseForecast(const char* text, Forecast& f) {
  f = Forecast{};
  if (text == nullptr || std::strncmp(text, "WX1\n", 4) != 0) return false;
  bool haveAt = false;
  bool haveNow = false;
  const char* at = text + 4;
  while (*at != '\0') {
    const char* end = std::strchr(at, '\n');
    if (std::strncmp(at, "at ", 3) == 0) {
      Fields in(at + 3);
      const long long when = in.integer();
      const long offset = static_cast<long>(in.integer());
      const double lat = in.real();
      const double lon = in.real();
      if (in.ok) {
        f.fetchedAt = when;
        f.utcOffset = static_cast<int32_t>(offset);
        f.lat = lat;
        f.lon = lon;
        haveAt = true;
      }
    } else if (std::strncmp(at, "now ", 4) == 0) {
      Fields in(at + 4);
      const double temp = in.real();
      const double feels = in.real();
      const double wind = in.real();
      const long long humidity = in.integer();
      const long long code = in.integer();
      const long long day = in.integer();
      if (in.ok) {
        f.temp = static_cast<float>(temp);
        f.feels = static_cast<float>(feels);
        f.wind = static_cast<float>(wind);
        f.humidity = static_cast<uint8_t>(humidity);
        f.code = static_cast<int16_t>(code);
        f.isDay = day != 0;
        haveNow = true;
      }
    } else if (std::strncmp(at, "h ", 2) == 0 && f.hourCount < kHours) {
      Fields in(at + 2);
      Hour h;
      h.time = in.integer();
      h.temp = static_cast<float>(in.real());
      h.code = static_cast<int16_t>(in.integer());
      h.rainChance = static_cast<uint8_t>(in.integer());
      h.day = in.integer() != 0;
      if (in.ok) f.hours[f.hourCount++] = h;
    } else if (std::strncmp(at, "d ", 2) == 0 && f.dayCount < kDays) {
      Fields in(at + 2);
      Day d;
      d.time = in.integer();
      d.code = static_cast<int16_t>(in.integer());
      d.high = static_cast<float>(in.real());
      d.low = static_cast<float>(in.real());
      d.rainChance = static_cast<uint8_t>(in.integer());
      d.rainMm = static_cast<float>(in.real());
      d.windMax = static_cast<float>(in.real());
      d.sunrise = in.integer();
      d.sunset = in.integer();
      if (in.ok) f.days[f.dayCount++] = d;
    }
    if (end == nullptr) break;
    at = end + 1;
  }
  return haveAt && haveNow && f.dayCount > 0;
}

// --- Time -----------------------------------------------------------------

int64_t localDay(const int64_t epoch, const int32_t utcOffset) { return floorDiv(epoch + utcOffset, 86400); }

Freshness freshness(const Forecast* cached, const Config& config, const int64_t now) {
  if (cached == nullptr) return Freshness::NoForecast;
  if (!placeMatches(*cached, config)) return Freshness::NewPlace;
  if (now < kClockFloor) return Freshness::NoClock;
  if (localDay(now, cached->utcOffset) != localDay(cached->fetchedAt, cached->utcOffset)) return Freshness::NewDay;
  return Freshness::Fresh;
}

int hourIndexAt(const Forecast& f, const int64_t now) {
  if (now < kClockFloor) return 0;
  int index = 0;
  for (int i = 0; i < f.hourCount; ++i) {
    if (f.hours[i].time <= now) index = i;
  }
  return index;
}

int dayIndexAt(const Forecast& f, const int64_t now) {
  if (now < kClockFloor || f.dayCount == 0) return 0;
  const int64_t today = localDay(now, f.utcOffset);
  int index = 0;
  for (int i = 0; i < f.dayCount; ++i) {
    if (localDay(f.days[i].time, f.utcOffset) <= today) index = i;
  }
  return index;
}

Now nowFor(const Forecast& f, const int64_t now) {
  Now out;
  out.temp = f.temp;
  out.code = f.code;
  out.isDay = f.isDay;
  if (now < kClockFloor || f.hourCount == 0) return out;
  if (floorDiv(now, 3600) == floorDiv(f.fetchedAt, 3600)) return out;
  const Hour& h = f.hours[hourIndexAt(f, now)];
  if (h.time > now) return out;
  out.temp = h.temp;
  out.code = h.code;
  out.isDay = h.day;
  return out;
}

// --- WMO codes ------------------------------------------------------------

Sky skyFor(const int code) {
  switch (code) {
    case 0:
      return Sky::Clear;
    case 1:
    case 2:
      return Sky::PartlyCloudy;
    case 3:
      return Sky::Cloudy;
    case 45:
    case 48:
      return Sky::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return Sky::Drizzle;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      return Sky::Rain;
    case 80:
    case 81:
    case 82:
      return Sky::Showers;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return Sky::Snow;
    case 95:
    case 96:
    case 99:
      return Sky::Storm;
    default:
      return Sky::Cloudy;
  }
}

const char* conditionLabel(const int code) {
  switch (code) {
    case 0:
      return "CLEAR";
    case 1:
      return "MOSTLY CLEAR";
    case 2:
      return "PARTLY CLOUDY";
    case 3:
      return "OVERCAST";
    case 45:
      return "FOG";
    case 48:
      return "FREEZING FOG";
    case 51:
      return "LIGHT DRIZZLE";
    case 53:
      return "DRIZZLE";
    case 55:
      return "HEAVY DRIZZLE";
    case 56:
    case 57:
      return "FREEZING DRIZZLE";
    case 61:
      return "LIGHT RAIN";
    case 63:
      return "RAIN";
    case 65:
      return "HEAVY RAIN";
    case 66:
    case 67:
      return "FREEZING RAIN";
    case 71:
      return "LIGHT SNOW";
    case 73:
      return "SNOW";
    case 75:
      return "HEAVY SNOW";
    case 77:
      return "SNOW GRAINS";
    case 80:
      return "SHOWERS";
    case 81:
      return "RAIN SHOWERS";
    case 82:
      return "HEAVY SHOWERS";
    case 85:
    case 86:
      return "SNOW SHOWERS";
    case 95:
      return "THUNDERSTORM";
    case 96:
    case 99:
      return "STORM, HAIL";
    default:
      return "CLOUDY";
  }
}

// --- Units ----------------------------------------------------------------

int toTemp(const float celsius, const TempUnit unit) {
  const double c = static_cast<double>(celsius);
  return roundToInt(unit == TempUnit::Fahrenheit ? c * 9.0 / 5.0 + 32.0 : c);
}

int toWind(const float kmh, const WindUnit unit) {
  const double v = static_cast<double>(kmh);
  switch (unit) {
    case WindUnit::Mph:
      return roundToInt(v / 1.609344);
    case WindUnit::Ms:
      return roundToInt(v / 3.6);
    case WindUnit::Knots:
      return roundToInt(v / 1.852);
    case WindUnit::Kmh:
      break;
  }
  return roundToInt(v);
}

const char* tempUnitLetter(const TempUnit unit) { return unit == TempUnit::Fahrenheit ? "F" : "C"; }
const char* tempUnitName(const TempUnit unit) { return unit == TempUnit::Fahrenheit ? "FAHRENHEIT" : "CELSIUS"; }

const char* windUnitLabel(const WindUnit unit) {
  switch (unit) {
    case WindUnit::Mph:
      return "MPH";
    case WindUnit::Ms:
      return "M/S";
    case WindUnit::Knots:
      return "KN";
    case WindUnit::Kmh:
      break;
  }
  return "KM/H";
}

const char* rainUnitLabel(const RainUnit unit) { return unit == RainUnit::Inches ? "INCHES" : "MM"; }

TempUnit nextTemp(const TempUnit unit) { return unit == TempUnit::Celsius ? TempUnit::Fahrenheit : TempUnit::Celsius; }
WindUnit nextWind(const WindUnit unit) { return static_cast<WindUnit>((static_cast<int>(unit) + 1) % 4); }
RainUnit nextRain(const RainUnit unit) { return unit == RainUnit::Mm ? RainUnit::Inches : RainUnit::Mm; }

void formatRain(const float mm, const RainUnit unit, char* out, const size_t cap) {
  const double v = static_cast<double>(mm);
  if (unit == RainUnit::Inches) {
    const double in = v / 25.4;
    if (in < 0.005) {
      std::snprintf(out, cap, "0 IN");
    } else {
      std::snprintf(out, cap, "%.2f IN", in);
    }
    return;
  }
  if (v < 0.05) {
    std::snprintf(out, cap, "0 MM");
  } else if (v < 10) {
    std::snprintf(out, cap, "%.1f MM", v);
  } else {
    std::snprintf(out, cap, "%d MM", roundToInt(v));
  }
}

// --- Text -----------------------------------------------------------------

const char* weekday(const int64_t localMidnight, const int32_t utcOffset) {
  static const char* const kNames[7] = {"THU", "FRI", "SAT", "SUN", "MON", "TUE", "WED"};
  const int64_t day = localDay(localMidnight, utcOffset);
  return kNames[static_cast<int>(((day % 7) + 7) % 7)];
}

int dayOfMonth(const int64_t epoch, const int32_t utcOffset) {
  // Howard Hinnant's civil_from_days, day part only.
  const int64_t z = localDay(epoch, utcOffset) + 719468;
  const int64_t era = floorDiv(z, 146097);
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  return static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
}

namespace {
void hourMinute(const int64_t epoch, const int32_t utcOffset, int& hour, int& minute) {
  const int64_t secs = epoch + utcOffset - localDay(epoch, utcOffset) * 86400;
  hour = static_cast<int>(secs / 3600);
  minute = static_cast<int>((secs % 3600) / 60);
}
}  // namespace

void formatHour(const int64_t epoch, const int32_t utcOffset, const bool twelveHour, char* out, const size_t cap) {
  int hour = 0;
  int minute = 0;
  hourMinute(epoch, utcOffset, hour, minute);
  if (!twelveHour) {
    std::snprintf(out, cap, "%02d", hour);
    return;
  }
  const int h12 = hour % 12 == 0 ? 12 : hour % 12;
  std::snprintf(out, cap, "%d%s", h12, hour < 12 ? "AM" : "PM");
}

void formatClock(const int64_t epoch, const int32_t utcOffset, const bool twelveHour, char* out, const size_t cap) {
  int hour = 0;
  int minute = 0;
  hourMinute(epoch, utcOffset, hour, minute);
  if (!twelveHour) {
    std::snprintf(out, cap, "%02d:%02d", hour, minute);
    return;
  }
  const int h12 = hour % 12 == 0 ? 12 : hour % 12;
  std::snprintf(out, cap, "%d:%02d%s", h12, minute, hour < 12 ? "AM" : "PM");
}

// --- Requests -------------------------------------------------------------

bool forecastUrl(const double lat, const double lon, char* out, const size_t cap) {
  const int n = std::snprintf(
      out, cap,
      "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
      "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
      "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,"
      "wind_speed_10m_max,sunrise,sunset"
      "&timezone=auto&timeformat=unixtime&forecast_days=%d&forecast_hours=%d",
      lat, lon, kDays, kHours);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool searchUrl(const char* query, char* out, const size_t cap) {
  static const char kHex[] = "0123456789ABCDEF";
  const int n = std::snprintf(out, cap, "https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&name=");
  if (n <= 0 || static_cast<size_t>(n) >= cap) return false;
  size_t at = static_cast<size_t>(n);
  bool any = false;
  for (const char* p = query; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                       c == '_' || c == '.' || c == '~';
    const size_t need = plain ? 1 : 3;
    if (at + need >= cap) return false;
    if (plain) {
      out[at++] = static_cast<char>(c);
    } else {
      out[at++] = '%';
      out[at++] = kHex[c >> 4];
      out[at++] = kHex[c & 0xF];
    }
    if (c != ' ') any = true;
  }
  out[at] = '\0';
  return any;
}

void joinRegion(const char* admin, const char* country, char* out, const size_t cap) {
  const bool a = admin != nullptr && admin[0] != '\0';
  const bool c = country != nullptr && country[0] != '\0';
  if (a && c && std::strcmp(admin, country) != 0) {
    std::snprintf(out, cap, "%s, %s", admin, country);
  } else if (a) {
    std::snprintf(out, cap, "%s", admin);
  } else if (c) {
    std::snprintf(out, cap, "%s", country);
  } else if (cap > 0) {
    out[0] = '\0';
  }
}

}  // namespace weather
