#include "BambuCloudApi.h"

#include <Logging.h>

#include <cstdlib>
#include <ctime>

#include "BambuStore.h"

namespace bambu {
namespace cloud {

#if !BAMBU_CLOUD

namespace {
constexpr const char* kNoCloud = "This reader's firmware has no room for Bambu's cloud. Use the LAN settings above.";
}  // namespace

const char* caRoots() { return ""; }

Login signIn(const std::string&, const std::string&, Account&, std::string& message) {
  message = kNoCloud;
  return Login::Refused;
}

Login signInWithCode(const std::string&, const std::string&, Account&, std::string& message) {
  message = kNoCloud;
  return Login::Refused;
}

int listPrinters(const Account&, Printer*, int, std::string& message) {
  message = kNoCloud;
  return -1;
}

#else

}  // namespace cloud
}  // namespace bambu

#include "BambuCloudRoots.h"

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>
#endif

namespace bambu {
namespace cloud {

namespace {

constexpr const char* kRootsPath = "/.crosspoint/bambu/.roots.pem";
constexpr size_t kRootsMax = 48 * 1024;
constexpr const char* kLoginPath = "/v1/user-service/user/login";
constexpr const char* kSendCodePath = "/v1/user-service/user/sendemail/code";
constexpr const char* kPreferencePath = "/v1/design-user-service/my/preference";
constexpr const char* kBindPath = "/v1/iot-service/api/user/bind";
// The agent string Bambu's own network plugin sends. The sign-in sits behind a
// bot filter that turns away clients it does not recognise.
constexpr const char* kUserAgent = "bambu_network_agent/01.09.05.01";

// One request, the whole reply in `body`. The status, or 0 when nothing came
// back, with `message` set.
int call(const char* method, const char* path, const std::string& token, const std::string& payload, std::string& body,
         std::string& message) {
  body.clear();
#if !defined(FREEINK_NET_WOLFSSL)
  (void)method;
  (void)path;
  (void)token;
  (void)payload;
  message = "This build has no TLS, so it cannot reach Bambu.";
  return 0;
#else
  freeink::SecureHttpClient http;
  http.setCACert(caRoots());
  http.setTimeout(20000);
  http.setFollowRedirects(1);
  if (!http.begin(std::string("https://") + kApiHost + path)) {
    message = "The reader could not make sense of Bambu's address.";
    return 0;
  }
  http.setUserAgent(kUserAgent);
  http.addHeader("Accept", "application/json");
  if (!payload.empty()) http.addHeader("Content-Type", "application/json");
  if (!token.empty()) http.addHeader("Authorization", "Bearer " + token);
  // Neither the password nor the token is logged: the path is enough.
  LOG_INF("BAMBU", "%s %s", method, path);
  const int status = payload.empty() ? http.sendRequest(method, std::string()) : http.sendRequest(method, payload);
  if (status <= 0) {
    LOG_ERR("BAMBU", "%s %s failed: %d", method, path, status);
    message = "Could not reach Bambu. Check the reader's Wi-Fi, and that its clock is set, and try again.";
    http.end();
    return 0;
  }
  body = http.getString();
  if (body.size() > kMaxResponseBytes) body.resize(kMaxResponseBytes);
  http.end();
  LOG_INF("BAMBU", "%s %s -> %d (%u bytes)", method, path, status, static_cast<unsigned>(body.size()));
  return status;
#endif
}

// Fills the user name and the expiry once there is a token.
bool finish(const std::string& email, const std::string& token, const std::string& loginReply, Account& account,
            std::string& message) {
  Account next;
  next.email = email;
  next.token = token;
  if (!readToken(token, next.user, next.expires)) {
    std::string body;
    const int status = call("GET", kPreferencePath, token, std::string(), body, message);
    if (status == 0) return false;
    next.user = userFromPreference(body);
    if (next.user.empty()) {
      LOG_ERR("BAMBU", "preference %d had no uid", status);
      message = "Bambu signed you in but would not say which account it is. Try again later.";
      return false;
    }
  }
  if (next.expires == 0) {
    const long long in = std::strtoll(jsonValue(loginReply, "expiresIn").c_str(), nullptr, 10);
    const time_t now = time(nullptr);
    if (in > 0 && now > 1700000000) next.expires = static_cast<int64_t>(now) + in;
  }
  account = next;
  return true;
}

Login settle(const std::string& email, const int status, const std::string& body, Account& account,
             std::string& message) {
  std::string token;
  const Login result = readLogin(status, body, token, message);
  if (result != Login::Token) return result;
  if (!finish(email, token, body, account, message)) return Login::Refused;
  return Login::Token;
}

}  // namespace

const char* caRoots() {
  static std::string sd;
  static bool probed = false;
  if (!probed) {
    probed = true;
    sd = store::read(kRootsPath, kRootsMax);
    if (!sd.empty() && sd.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
      LOG_ERR("BAMBU", ".roots.pem holds no certificate; using the baked roots");
      sd.clear();
    }
  }
  return sd.empty() ? kCaRoots : sd.c_str();
}

Login signIn(const std::string& email, const std::string& password, Account& account, std::string& message) {
  std::string body;
  const int status = call("POST", kLoginPath, std::string(), loginBody(email, password), body, message);
  if (status == 0) return Login::Refused;
  const Login result = settle(email, status, body, account, message);
  if (result != Login::NeedCode) return result;
  std::string ignored;
  const int sent = call("POST", kSendCodePath, std::string(), sendCodeBody(email), ignored, message);
  if (sent < 200 || sent >= 300) {
    if (sent != 0) message = "Bambu wants a code from your email but would not send one. Try again in a minute.";
    return Login::Refused;
  }
  return Login::NeedCode;
}

Login signInWithCode(const std::string& email, const std::string& code, Account& account, std::string& message) {
  std::string body;
  const int status = call("POST", kLoginPath, std::string(), codeLoginBody(email, code), body, message);
  if (status == 0) return Login::Refused;
  const Login result = settle(email, status, body, account, message);
  // An expired code: readLogin's sentence promises a new one, so send it.
  if (result == Login::Refused && status == 400 && jsonValue(body, "code") == "1") {
    std::string ignored;
    std::string unused;
    call("POST", kSendCodePath, std::string(), sendCodeBody(email), ignored, unused);
  }
  if (result == Login::NeedCode) {
    message = "Bambu asked for a code again. Start over with your email and password.";
    return Login::Refused;
  }
  return result;
}

int listPrinters(const Account& account, Printer* out, const int max, std::string& message) {
  std::string body;
  const int status = call("GET", kBindPath, account.token, std::string(), body, message);
  if (status == 0) return -1;
  if (status == 401 || status == 403) {
    message = "Bambu has signed this reader out. Sign in again on the phone page.";
    return -1;
  }
  if (status != 200) {
    message = "Bambu would not list your printers just now. Try again later.";
    return -1;
  }
  return readPrinters(body, out, max);
}

#endif  // BAMBU_CLOUD

}  // namespace cloud
}  // namespace bambu
