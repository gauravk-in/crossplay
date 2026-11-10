#pragma once

// Bambu's cloud, for a printer the reader cannot reach on the LAN.
//
// The printer keeps a connection open to Bambu's MQTT broker
// (us.mqtt.bambulab.com:8883) and publishes the same reports there as on the
// LAN, on the same device/<serial>/report topic. The broker takes the account's
// user name ("u_" and the account number) and its access token as password,
// so all the account work is getting a token once:
//
//   POST api.bambulab.com/v1/user-service/user/login {account, password}
//     -> {accessToken} or {loginType: "verifyCode"} or {loginType: "tfa"}
//   POST .../user/sendemail/code {email, type: "codeLogin"}
//   POST .../user/login {account, code} -> {accessToken}
//
// The token lasts about three months. The password is passed through and
// never kept; the token lives in its own file, which the phone page never
// reads back.
//
// None of this is a published API: it is what Bambu's own apps and the Home
// Assistant integration use. Everything here is the pure half and is
// host-tested; BambuCloudApi does the requests.

#include <cstddef>
#include <cstdint>
#include <string>

// Left out of the ESP32-C3 builds (upstream's X4 and X3): their app partition
// has no room left for it, and no CrossPlay device is a C3. Without it the
// phone page's sign-in answers with a sentence and the monitor stays on the LAN.
#ifndef BAMBU_CLOUD
#if defined(FREEINK_DEVICE_X4) || defined(FREEINK_DEVICE_X3)
#define BAMBU_CLOUD 0
#else
#define BAMBU_CLOUD 1
#endif
#endif

namespace bambu {
namespace cloud {

constexpr const char* kApiHost = "api.bambulab.com";
constexpr const char* kMqttHost = "us.mqtt.bambulab.com";
constexpr size_t kMaxTokenBytes = 4096;
constexpr size_t kMaxFileBytes = 5 * 1024;
constexpr size_t kMaxResponseBytes = 64 * 1024;

// The account, as /.crosspoint/bambu/cloud.txt holds it.
struct Account {
  std::string token;    // the access token, which is also the MQTT password
  std::string user;     // "u_1234567890"
  std::string email;    // shown on the phone page
  int64_t expires = 0;  // unix seconds, from the token; 0 when it does not say
};

Account parseAccount(const std::string& text);
std::string formatAccount(const Account& account);
bool signedIn(const Account& account);

// --- Requests --------------------------------------------------------------

std::string jsonQuote(const std::string& value);
std::string loginBody(const std::string& email, const std::string& password);
std::string codeLoginBody(const std::string& email, const std::string& code);
std::string sendCodeBody(const std::string& email);

// --- Responses -------------------------------------------------------------

// The first value under `key` anywhere in `json`: a string unescaped, a number
// or literal as written. Empty when absent. Enough for Bambu's flat replies.
std::string jsonValue(const std::string& json, const char* key);

enum class Login : uint8_t { Token, NeedCode, NeedTfa, Refused };
// What a login reply means. `token` is set on Token, `message` on Refused.
Login readLogin(int status, const std::string& body, std::string& token, std::string& message);

// The account's MQTT user name and the expiry, read from the token when it is
// a JWT. False when it is not one, and the user name has to be asked for.
bool readToken(const std::string& token, std::string& user, int64_t& expires);
// "u_<uid>" from the preference reply, empty when it has no uid.
std::string userFromPreference(const std::string& body);

struct Printer {
  std::string serial;
  std::string name;
  std::string model;
  std::string accessCode;
  bool online = false;
};
constexpr int kMaxPrinters = 8;
// The bound printers in the order Bambu lists them. Returns how many.
int readPrinters(const std::string& body, Printer* out, int max);

// "Signed in until 8 Feb 2027" or "Signed in".
std::string untilPhrase(int64_t expires);

}  // namespace cloud
}  // namespace bambu
