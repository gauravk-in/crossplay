#include "GTasksApi.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Utf8.h>

#include "../bridge/BridgeHttp.h"

namespace gtasks {
namespace {

constexpr const char* kTag = "GTASKS";
constexpr const char* kRoots = "/.crosspoint/gtasks/.roots.pem";

#ifndef GTASKS_BRIDGE_HOST
// Where server/tasks-bridge is deployed. /.crosspoint/gtasks/bridge.cfg
// overrides it without a reflash.
#define GTASKS_BRIDGE_HOST "tasks.gauravk.in"
#endif

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

void Api::setBridgeHost(const std::string& host) { bridgeHost_ = host; }

std::string Api::pairAddress() const {
  return (bridgeHost_.empty() ? std::string(GTASKS_BRIDGE_HOST) : bridgeHost_) + "/pair";
}

std::string Api::pairUrl(const std::string& code) const { return "https://" + pairAddress() + "#" + code; }

int Api::callBridge(const char* method, const std::string& path, const std::string& token, const std::string& body,
                    std::string& response, std::string& message) const {
  const std::string host = bridgeHost_.empty() ? std::string(GTASKS_BRIDGE_HOST) : bridgeHost_;
  const bridge::Endpoint endpoint = {host.c_str(), kTag, "GTASKS_BRIDGE_URL", kRoots};
  return call(endpoint, method, path, token, body, body.empty() ? nullptr : "application/json", response, message);
}

bool Api::pairStart(PairStart& out, std::string& message) {
  std::string response;
  const int status = callBridge("POST", "/api/pair/start", "", "", response, message);
  if (status == 0) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "The sign-in service refused. Try again later.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["code"].is<const char*>() ||
      !doc["pollToken"].is<const char*>()) {
    message = "The sign-in service answered something unexpected.";
    return false;
  }
  out.code = doc["code"].as<const char*>();
  out.pollToken = doc["pollToken"].as<const char*>();
  // On serial deliberately: "read me the code" is the first support question.
  LOG_INF(kTag, "pairing code %s", out.code.c_str());
  return true;
}

int Api::pairPoll(const std::string& pollToken, std::string& account, std::string& token, std::string& message) {
  std::string response;
  const int status = callBridge("GET", "/api/pair/poll?pollToken=" + formEncode(pollToken), "", "", response, message);
  if (status == 0) return -1;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "That code expired. Ask for a fresh one.";
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    message = "The sign-in service answered something unexpected.";
    return -1;
  }
  if (doc["pending"] | false) return 0;
  if (!doc["deviceToken"].is<const char*>()) {
    message = "The sign-in service answered something unexpected.";
    return -1;
  }
  account = doc["email"] | "";
  token = doc["deviceToken"].as<const char*>();
  return 1;
}

void Api::pairAbandon(const std::string& pollToken, const std::string& deviceToken) {
  JsonDocument doc;
  if (!pollToken.empty()) doc["pollToken"] = pollToken;
  if (!deviceToken.empty()) doc["deviceToken"] = deviceToken;
  std::string body;
  serializeJson(doc, body);
  std::string response;
  std::string message;
  callBridge("POST", "/api/pair/abandon", "", body, response, message);
}

void Api::unpair(const std::string& deviceToken) {
  std::string response;
  std::string message;
  callBridge("POST", "/api/unpair", deviceToken, "{}", response, message);
}

bool Api::refresh(const Credentials& creds, const uint32_t nowMs, AccessToken& out, std::string& message) {
  signedOut = false;
  if (!creds.complete()) {
    signedOut = true;
    message = "This reader is not signed in to Google.";
    return false;
  }
  std::string response;
  const int status = callBridge("POST", "/api/token", creds.deviceToken, "{}", response, message);
  if (status == 0) return false;
  if (status == 401) {
    signedOut = true;
    if (!bridge::takeServerError(response, message)) message = "This reader is not signed in anymore. Sign in again.";
    LOG_ERR(kTag, "token refused by the sign-in service");
    return false;
  }
  JsonDocument doc;
  if (status != 200 || deserializeJson(doc, response) != DeserializationError::Ok ||
      !doc["accessToken"].is<const char*>()) {
    if (!bridge::takeServerError(response, message)) message = "The sign-in service did not hand out a key.";
    LOG_ERR(kTag, "token: HTTP %d", status);
    return false;
  }
  out.value = doc["accessToken"].as<const char*>();
  const uint32_t lifetime = doc["expiresIn"] | 3600u;
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
