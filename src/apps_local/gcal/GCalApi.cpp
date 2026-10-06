#include "GCalApi.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Utf8.h>

#include "../bridge/BridgeHttp.h"

namespace gcal {
namespace {

constexpr const char* kTag = "GCAL";
// The roots Tasks reads: www.googleapis.com chains to the same GTS roots.
constexpr const char* kRoots = "/.crosspoint/gtasks/.roots.pem";

constexpr bridge::Endpoint kCalendar = {"www.googleapis.com", kTag, "GCAL_API_URL", kRoots};

// Bounds on a sync, so a busy account or a misbehaving nextPageToken cannot
// keep the radio up for ever. Twelve calendars of four pages of 250 is more
// than three months of anybody's schedule.
constexpr size_t kMaxCalendars = 12;
constexpr int kMaxPages = 4;
constexpr size_t kMaxEvents = 1500;

int get(const std::string& path, const std::string& token, std::string& response, std::string& message) {
  bridge::Headers headers;
  headers.add("Accept", "application/json");
  return bridge::request(kCalendar, "GET", path, token, nullptr, 0, response, message, &headers);
}

std::string googleSays(const std::string& response) {
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) return std::string();
  if (doc["error"]["message"].is<const char*>()) return doc["error"]["message"].as<const char*>();
  return std::string();
}

// 401 is a stale access token; a 403 naming scopes is a sign-in that never
// granted Calendar. Anything else is a real failure with Google's sentence.
bool refused(const int status, const std::string& response, bool& tokenRefused, bool& needsConsent,
             std::string& message) {
  if (status == 401) {
    tokenRefused = true;
    message = "Google refused the reader's key.";
    return true;
  }
  if (status == 403 && (response.find("SCOPE_INSUFFICIENT") != std::string::npos ||
                        response.find("insufficient authentication scopes") != std::string::npos)) {
    needsConsent = true;
    message =
        "This reader's Google sign-in does not include Calendar yet. Sign in again, and allow Calendar when Google "
        "asks.";
    return true;
  }
  return false;
}

std::string encodeQuery(const std::string& value) { return pathEncode(value); }

// Google's start or end: {"date": "2026-10-07"} for all day, else
// {"dateTime": "2026-10-07T09:00:00+02:00"}.
bool readWhen(JsonObjectConst when, bool& allDay, int64_t& out) {
  const char* date = when["date"] | static_cast<const char*>(nullptr);
  if (date != nullptr) {
    allDay = true;
    return parseDate(date, out);
  }
  const char* dateTime = when["dateTime"] | static_cast<const char*>(nullptr);
  allDay = false;
  return dateTime != nullptr && parseDateTime(dateTime, out);
}

}  // namespace

bool Api::calendars(const gtasks::AccessToken& token, std::vector<Calendar>& out, std::string& message) {
  tokenRefused = false;
  needsConsent = false;
  out.clear();
  out.reserve(kMaxCalendars);

  JsonDocument filter;
  filter["nextPageToken"] = true;
  JsonObject item = filter["items"].add<JsonObject>();
  item["id"] = true;
  item["summary"] = true;
  item["summaryOverride"] = true;
  item["selected"] = true;
  item["hidden"] = true;
  item["primary"] = true;

  std::string pageToken;
  for (int page = 0; page < kMaxPages; ++page) {
    std::string path = "/calendar/v3/users/me/calendarList?maxResults=100&minAccessRole=freeBusyReader";
    if (!pageToken.empty()) path += "&pageToken=" + encodeQuery(pageToken);
    std::string response;
    const int status = get(path, token.value, response, message);
    if (status == 0) return false;
    if (refused(status, response, tokenRefused, needsConsent, message)) return false;
    JsonDocument doc;
    if (status != 200 ||
        deserializeJson(doc, response, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
      message = googleSays(response);
      if (message.empty()) message = "Google would not say which calendars there are.";
      LOG_ERR(kTag, "calendarList page %d: HTTP %d", page, status);
      return false;
    }
    for (JsonObject c : doc["items"].as<JsonArray>()) {
      const bool primary = c["primary"] | false;
      const bool selected = c["selected"] | false;
      const bool hidden = c["hidden"] | false;
      if (hidden || (!selected && !primary)) continue;
      Calendar cal;
      cal.id = c["id"] | "";
      if (cal.id.empty()) continue;
      const char* name = c["summaryOverride"] | static_cast<const char*>(nullptr);
      cal.title = utf8FoldTypography(name != nullptr ? name : (c["summary"] | ""));
      if (primary) {
        out.insert(out.begin(), std::move(cal));
      } else {
        out.push_back(std::move(cal));
      }
    }
    pageToken = doc["nextPageToken"] | "";
    if (pageToken.empty()) break;
  }
  if (out.size() > kMaxCalendars) {
    LOG_ERR(kTag, "%d calendars shown; reading the first %d", static_cast<int>(out.size()),
            static_cast<int>(kMaxCalendars));
    out.resize(kMaxCalendars);
  }
  LOG_INF(kTag, "%d calendars", static_cast<int>(out.size()));
  return true;
}

bool Api::events(const gtasks::AccessToken& token, const std::string& calendarId, const int64_t timeMin,
                 const int64_t timeMax, std::vector<Event>& out, std::string& message) {
  tokenRefused = false;
  needsConsent = false;

  // Only what is drawn. Descriptions, attendee lists and conference links can
  // be kilobytes an event, and Google trims them server side with `fields`.
  JsonDocument filter;
  filter["nextPageToken"] = true;
  JsonObject item = filter["items"].add<JsonObject>();
  item["summary"] = true;
  item["location"] = true;
  item["status"] = true;
  item["start"] = true;
  item["end"] = true;
  JsonObject attendee = item["attendees"].add<JsonObject>();
  attendee["self"] = true;
  attendee["responseStatus"] = true;

  const std::string base =
      "/calendar/v3/calendars/" + pathEncode(calendarId) +
      "/events?singleEvents=true&orderBy=startTime&maxResults=250&timeMin=" + encodeQuery(formatDateTimeUtc(timeMin)) +
      "&timeMax=" + encodeQuery(formatDateTimeUtc(timeMax)) + "&fields=" +
      encodeQuery(
          "nextPageToken,items(summary,location,status,start,end,"
          "attendees(self,responseStatus))");
  std::string pageToken;
  int added = 0;
  for (int page = 0; page < kMaxPages; ++page) {
    std::string path = base;
    if (!pageToken.empty()) path += "&pageToken=" + encodeQuery(pageToken);
    std::string response;
    const int status = get(path, token.value, response, message);
    if (status == 0) return false;
    if (refused(status, response, tokenRefused, needsConsent, message)) return false;
    JsonDocument doc;
    if (status != 200 ||
        deserializeJson(doc, response, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
      message = googleSays(response);
      if (message.empty()) message = "Google sent a calendar this reader could not read.";
      LOG_ERR(kTag, "events page %d: HTTP %d", page, status);
      return false;
    }
    for (JsonObject e : doc["items"].as<JsonArray>()) {
      if (out.size() >= kMaxEvents) break;
      const std::string state = e["status"] | "";
      if (state == "cancelled") continue;
      bool declined = false;
      for (JsonObject a : e["attendees"].as<JsonArray>()) {
        if ((a["self"] | false) && std::string(a["responseStatus"] | "") == "declined") declined = true;
      }
      if (declined) continue;
      Event event;
      bool startAllDay = false;
      bool endAllDay = false;
      if (!readWhen(e["start"].as<JsonObjectConst>(), startAllDay, event.start) ||
          !readWhen(e["end"].as<JsonObjectConst>(), endAllDay, event.end) || startAllDay != endAllDay ||
          event.end < event.start) {
        continue;
      }
      event.allDay = startAllDay;
      // An event with no title is drawn the way Google's own apps draw it.
      event.title = utf8FoldTypography(e["summary"] | "(No title)");
      event.location = utf8FoldTypography(e["location"] | "");
      out.push_back(std::move(event));
      ++added;
    }
    pageToken = doc["nextPageToken"] | "";
    if (pageToken.empty() || out.size() >= kMaxEvents) break;
  }
  LOG_INF(kTag, "%d events", added);
  return true;
}

}  // namespace gcal
