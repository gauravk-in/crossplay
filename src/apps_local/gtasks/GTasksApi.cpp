#include "GTasksApi.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Utf8.h>

#include "../bridge/BridgeHttp.h"

namespace gtasks {
namespace {

constexpr const char* kTag = "GTASKS";
constexpr const char* kRoots = "/.crosspoint/gtasks/.roots.pem";

constexpr bridge::Endpoint kOauth = {"oauth2.googleapis.com", kTag, "GTASKS_OAUTH_URL", kRoots};
constexpr bridge::Endpoint kTasks = {"tasks.googleapis.com", kTag, "GTASKS_API_URL", kRoots};

// Most a list is read to. 100 per page is Google's ceiling; five pages is more
// open tasks than this screen can usefully page through, and a bound means a
// misbehaving nextPageToken cannot keep the radio up forever.
constexpr int kMaxPages = 5;

int call(const bridge::Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
         const std::string& body, const char* contentType, std::string& response, std::string& message) {
  bridge::Headers headers;
  if (contentType != nullptr) headers.add("Content-Type", contentType);
  headers.add("Accept", "application/json");
  return bridge::request(endpoint, method, path, token,
                         body.empty() ? nullptr : reinterpret_cast<const uint8_t*>(body.data()), body.size(), response,
                         message, &headers);
}

// Google's errors come in two shapes: OAuth's {"error":"invalid_grant"} and
// the API's {"error":{"code":401,"message":"..."}}. Only the second carries a
// sentence, and only that one is worth putting on the screen.
std::string googleSays(const std::string& response) {
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) return std::string();
  if (doc["error"]["message"].is<const char*>()) return doc["error"]["message"].as<const char*>();
  if (doc["error_description"].is<const char*>()) return doc["error_description"].as<const char*>();
  return std::string();
}

}  // namespace

namespace {

constexpr const char* kForm = "application/x-www-form-urlencoded";

// OAuth's own error codes for "this grant is no good any more", as opposed to
// a network or server failure that is worth retrying on the next sync.
bool grantRefused(const std::string& response) {
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["error"].is<const char*>()) return false;
  const std::string error = doc["error"].as<const char*>();
  return error == "invalid_grant" || error == "invalid_client" || error == "unauthorized_client";
}

}  // namespace

bool Api::exchange(const Client& client, const std::string& code, const std::string& verifier, Credentials& out,
                   std::string& message) {
  const std::string body = "code=" + formEncode(code) + "&client_id=" + formEncode(client.id) +
                           "&client_secret=" + formEncode(client.secret) + "&redirect_uri=" + formEncode(kRedirectUri) +
                           "&grant_type=authorization_code&code_verifier=" + formEncode(verifier);
  std::string response;
  const int status = call(kOauth, "POST", "/token", "", body, kForm, response, message);
  if (status == 0) return false;
  JsonDocument doc;
  if (status != 200 || deserializeJson(doc, response) != DeserializationError::Ok) {
    LOG_ERR(kTag, "exchange: HTTP %d", status);
    message = grantRefused(response) ? "Google did not accept that address. It works once, and only for a few "
                                       "minutes: sign in again on the phone."
                                     : googleSays(response);
    if (message.empty()) message = "Google would not finish the sign-in. Try again.";
    return false;
  }
  if (!doc["refresh_token"].is<const char*>()) {
    message =
        "Google signed in but kept the long-lived key back. Remove the reader at "
        "myaccount.google.com/permissions and sign in again.";
    return false;
  }
  out.refreshToken = doc["refresh_token"].as<const char*>();
  out.account = idTokenEmail(doc["id_token"] | "");
  LOG_INF(kTag, "signed in as %s", out.account.empty() ? "(no address)" : out.account.c_str());
  return true;
}

void Api::revoke(const std::string& refreshToken) {
  if (refreshToken.empty()) return;
  std::string response;
  std::string message;
  const int status = call(kOauth, "POST", "/revoke", "", "token=" + formEncode(refreshToken), kForm, response, message);
  LOG_INF(kTag, "revoke: HTTP %d", status);
}

bool Api::refresh(const Client& client, const Credentials& creds, const uint32_t nowMs, AccessToken& out,
                  std::string& message) {
  signedOut = false;
  if (!creds.complete()) {
    signedOut = true;
    message = "This reader is not signed in to Google.";
    return false;
  }
  if (!client.complete()) {
    message = "No Google client is set up on this card. See docs/apps/gtasks.md.";
    return false;
  }
  const std::string body = "client_id=" + formEncode(client.id) + "&client_secret=" + formEncode(client.secret) +
                           "&refresh_token=" + formEncode(creds.refreshToken) + "&grant_type=refresh_token";
  std::string response;
  const int status = call(kOauth, "POST", "/token", "", body, kForm, response, message);
  if (status == 0) return false;
  JsonDocument doc;
  if (status != 200 || deserializeJson(doc, response) != DeserializationError::Ok ||
      !doc["access_token"].is<const char*>()) {
    LOG_ERR(kTag, "token: HTTP %d", status);
    if (grantRefused(response)) {
      signedOut = true;
      message = "Google signed this reader out. Sign in again.";
      return false;
    }
    message = googleSays(response);
    if (message.empty()) message = "Google did not hand out a key.";
    return false;
  }
  out.value = doc["access_token"].as<const char*>();
  const uint32_t lifetime = doc["expires_in"] | 3600u;
  out.goodUntilMs = nowMs + (lifetime > 120 ? lifetime - 60 : lifetime / 2) * 1000u;
  return true;
}

bool Api::listTitle(const AccessToken& token, std::string& title, std::string& message) {
  tokenRefused = false;
  std::string response;
  const int status =
      call(kTasks, "GET", "/tasks/v1/users/@me/lists/@default", token.value, "", nullptr, response, message);
  if (status == 0) return false;
  if (status == 401) {
    tokenRefused = true;
    message = "Google refused the reader's key.";
    return false;
  }
  JsonDocument doc;
  if (status != 200 || deserializeJson(doc, response) != DeserializationError::Ok) {
    message = googleSays(response);
    if (message.empty()) message = "Google would not say which list this is.";
    return false;
  }
  title = utf8FoldTypography(doc["title"] | "");
  return true;
}

bool Api::openTasks(const AccessToken& token, std::vector<Task>& out, std::string& message) {
  tokenRefused = false;
  out.clear();
  out.reserve(64);

  // Only the fields drawn or kept. A task's notes and links can be long, and
  // nothing here shows them.
  JsonDocument filter;
  filter["nextPageToken"] = true;
  JsonObject item = filter["items"].add<JsonObject>();
  item["id"] = true;
  item["title"] = true;
  item["position"] = true;
  item["parent"] = true;
  item["due"] = true;
  item["status"] = true;

  std::string pageToken;
  for (int page = 0; page < kMaxPages; ++page) {
    std::string path = "/tasks/v1/lists/@default/tasks?showCompleted=false&showHidden=false&maxResults=100";
    if (!pageToken.empty()) path += "&pageToken=" + formEncode(pageToken);
    std::string response;
    const int status = call(kTasks, "GET", path, token.value, "", nullptr, response, message);
    if (status == 0) return false;
    if (status == 401) {
      tokenRefused = true;
      message = "Google refused the reader's key.";
      return false;
    }
    JsonDocument doc;
    if (status != 200 ||
        deserializeJson(doc, response, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
      message = googleSays(response);
      if (message.empty()) message = "Google sent a list this reader could not read.";
      LOG_ERR(kTag, "tasks page %d: HTTP %d", page, status);
      return false;
    }
    for (JsonObject t : doc["items"].as<JsonArray>()) {
      const std::string status_ = t["status"] | "";
      if (status_ == "completed") continue;
      Task task;
      task.id = t["id"] | "";
      // Tasks with no title are how Google's apps leave a blank row; nothing
      // here could draw one, and an empty tick box reads as a bug.
      task.title = utf8FoldTypography(t["title"] | "");
      if (task.id.empty() || task.title.empty()) continue;
      task.position = t["position"] | "";
      task.parent = t["parent"] | "";
      task.due = dueDate(t["due"] | "");
      out.push_back(std::move(task));
    }
    pageToken = doc["nextPageToken"] | "";
    if (pageToken.empty()) break;
  }
  LOG_INF(kTag, "%d open tasks", static_cast<int>(out.size()));
  return true;
}

bool Api::complete(const AccessToken& token, const std::string& id, std::string& message) {
  tokenRefused = false;
  if (!safeId(id)) {
    // Not a request worth making, and not one worth retrying forever either:
    // report it gone so the tick leaves the queue.
    LOG_ERR(kTag, "refusing to complete a task with a malformed id");
    return true;
  }
  std::string response;
  const int status = call(kTasks, "PATCH", "/tasks/v1/lists/@default/tasks/" + id, token.value,
                          "{\"status\":\"completed\"}", "application/json", response, message);
  if (status == 0) return false;
  if (status == 401) {
    tokenRefused = true;
    message = "Google refused the reader's key.";
    return false;
  }
  if (status == 404 || status == 410) {
    LOG_INF(kTag, "task %s is gone on Google; nothing to complete", id.c_str());
    return true;
  }
  if (status != 200) {
    message = googleSays(response);
    if (message.empty()) message = "Google would not take a tick. It stays here for the next sync.";
    LOG_ERR(kTag, "complete %s: HTTP %d", id.c_str(), status);
    return false;
  }
  return true;
}

}  // namespace gtasks
