#include "BambuServer.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>

#include "BambuCloudApi.h"
#include "BambuCore.h"
#include "BambuPageHtml.generated.h"
#include "BambuStore.h"

namespace {
constexpr uint16_t kPort = 80;
#if BAMBU_CLOUD
constexpr size_t kMaxSignInBytes = 512;

// One key from a key=value body. A password may hold '=' and anything else
// but a line break, so only the first '=' splits.
std::string bodyValue(const std::string& body, const char* key) {
  const std::string prefix = std::string(key) + "=";
  size_t pos = 0;
  while (pos < body.size()) {
    size_t end = body.find('\n', pos);
    if (end == std::string::npos) end = body.size();
    std::string line = body.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, prefix.size(), prefix) == 0) return line.substr(prefix.size());
    pos = end + 1;
  }
  return std::string();
}

std::string trimmed(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

// Says the reader is busy for the length of a sign-in, then closes the Bambu
// connection it kept open and says the wait is over, however the handler returns.
struct HangUp {
  void* ctx;
  void (*fn)(void*, bool);
  HangUp(void* c, void (*f)(void*, bool)) : ctx(c), fn(f) {
    if (fn) fn(ctx, true);
  }
  ~HangUp() {
    bambu::cloud::hangUp();
    if (fn) fn(ctx, false);
  }
};

std::string noLineBreaks(std::string s) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '|') c = ' ';
  }
  return s;
}
#endif
}  // namespace

bool BambuServer::begin() {
  if (running_) return true;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  server_ = makeUniqueNoThrow<WebServer>(kPort);
  if (!server_) {
    LOG_ERR("BAMBU", "OOM: WebServer");
    return false;
  }
  // A sleeping radio drops the phone's request and the page blames the network.
  WiFi.setSleep(false);
  server_->on("/bambu", HTTP_GET, [this] { handlePage(); });
  server_->on("/bambu/config", HTTP_GET, [this] { handleRead(); });
  // PUT, not POST: this core hands one callback to both the multipart and the
  // raw paths, and the settings are small enough to arrive as a plain body.
  server_->on("/bambu/config", HTTP_PUT, [this] { handleSave(); });
#if BAMBU_CLOUD
  server_->on("/bambu/cloud/signin", HTTP_PUT, [this] { handleSignIn(); });
  server_->on("/bambu/cloud/code", HTTP_PUT, [this] { handleCode(); });
  server_->on("/bambu/cloud/signout", HTTP_PUT, [this] { handleSignOut(); });
#endif
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not found"); });
  static const char* kHeaders[] = {"If-None-Match"};
  server_->collectHeaders(kHeaders, 1);
  server_->begin();
  running_ = true;
  return true;
}

void BambuServer::stop() {
  if (!server_) return;
  running_ = false;
  server_->stop();
  server_.reset();
}

void BambuServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void BambuServer::handlePage() {
  // Baked into flash at build time, so the ETag is stable for the image.
  if (server_->header("If-None-Match") == BambuPageHtmlETag) {
    server_->sendHeader("ETag", BambuPageHtmlETag);
    server_->send(304);
    return;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", BambuPageHtmlETag);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", BambuPageHtml, sizeof(BambuPageHtml));
}

void BambuServer::handleRead() {
  server_->sendHeader("Cache-Control", "no-store");
  // Normalised on the way out, so the page parses the one shape the reader
  // writes rather than whatever a computer left in the file.
  const std::string text =
      bambu::formatConfig(bambu::parseConfig(bambu::store::read(bambu::store::kConfigPath, bambu::kMaxConfigBytes))) +
      accountFacts() + facts_;
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}

void BambuServer::handleSave() {
  const String raw = server_->arg("plain");
  if (raw.length() > bambu::kMaxConfigBytes) {
    server_->send(413, "text/plain", "Those settings are too long for the reader.");
    return;
  }
  // A form post or an empty body would parse as all defaults and drop the code.
  if (raw.indexOf("host=") < 0) {
    server_->send(400, "text/plain", "Send the settings as plain key=value lines.");
    return;
  }
  // Parsed and written back out, so every limit in BambuCore applies to a
  // phone exactly as it does to a file.
  const std::string text = bambu::formatConfig(bambu::parseConfig(std::string(raw.c_str(), raw.length())));
  if (!bambu::store::write(bambu::store::kConfigPath, text)) {
    server_->send(500, "text/plain", "The card would not take it.");
    return;
  }
  changed_ = true;
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", (text + accountFacts() + facts_).c_str());
}

// --- The Bambu account ---------------------------------------------------------

#if !BAMBU_CLOUD

std::string BambuServer::accountFacts() const { return std::string(); }

#else

std::string BambuServer::accountFacts() const {
  const bambu::cloud::Account account =
      bambu::cloud::parseAccount(bambu::store::read(bambu::store::kCloudPath, bambu::cloud::kMaxFileBytes));
  if (!bambu::cloud::signedIn(account)) return std::string();
  // The token stays on the reader: the page is told who and until when.
  return "cloud_email=" + noLineBreaks(account.email) + "\ncloud_until=" + bambu::cloud::untilPhrase(account.expires) +
         "\n";
}

void BambuServer::answer(const char* result, const std::string& message, const std::string& extra) {
  server_->sendHeader("Cache-Control", "no-store");
  const std::string text =
      std::string("result=") + result + "\nmessage=" + noLineBreaks(message) + "\n" + extra + accountFacts();
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}

void BambuServer::handleSignIn() {
  const String raw = server_->arg("plain");
  if (raw.length() > kMaxSignInBytes) {
    server_->send(413, "text/plain", "That is too long for the reader.");
    return;
  }
  const std::string body(raw.c_str(), raw.length());
  const std::string email = trimmed(bodyValue(body, "email"));
  const std::string password = bodyValue(body, "password");
  if (email.empty() || password.empty()) {
    answer("error", "Fill in your Bambu email and password.");
    return;
  }
  const HangUp hangUp(busyCtx_, busyFn_);
  bambu::cloud::Account account;
  std::string message;
  switch (bambu::cloud::signIn(email, password, account, message)) {
    case bambu::cloud::Login::Token:
      finishSignIn(account);
      return;
    case bambu::cloud::Login::NeedCode:
      answer("code", "Bambu sent a code to " + email + ". Type it in below.");
      return;
    case bambu::cloud::Login::NeedTfa:
      answer("error",
             "This account signs in with an authenticator app, which the reader cannot do yet. Turn two-step "
             "sign-in off in your Bambu account, or use the LAN settings above.");
      return;
    case bambu::cloud::Login::Refused:
      answer("error", message);
      return;
  }
}

void BambuServer::handleCode() {
  const String raw = server_->arg("plain");
  if (raw.length() > kMaxSignInBytes) {
    server_->send(413, "text/plain", "That is too long for the reader.");
    return;
  }
  const std::string body(raw.c_str(), raw.length());
  const std::string email = trimmed(bodyValue(body, "email"));
  const std::string code = trimmed(bodyValue(body, "code"));
  if (email.empty() || code.empty()) {
    answer("error", "Type in the code from the email.");
    return;
  }
  const HangUp hangUp(busyCtx_, busyFn_);
  bambu::cloud::Account account;
  std::string message;
  if (bambu::cloud::signInWithCode(email, code, account, message) != bambu::cloud::Login::Token) {
    answer("code", message);
    return;
  }
  finishSignIn(account);
}

void BambuServer::finishSignIn(const bambu::cloud::Account& account) {
  if (!bambu::store::write(bambu::store::kCloudPath, bambu::cloud::formatAccount(account))) {
    answer("error", "Bambu signed you in, but the card would not keep it.");
    return;
  }
  changed_ = true;
  // Heap, not stack: eight printers' strings are over a kilobyte.
  auto printers = makeUniqueNoThrow<bambu::cloud::Printer[]>(bambu::cloud::kMaxPrinters);
  std::string message = "There was not enough memory to list the printers.";
  const int count =
      printers ? bambu::cloud::listPrinters(account, printers.get(), bambu::cloud::kMaxPrinters, message) : -1;
  std::string extra;
  for (int i = 0; i < count; ++i) {
    extra += "printer=" + printers[i].serial + "|" + noLineBreaks(printers[i].name) + "|" +
             noLineBreaks(printers[i].model) + "|" + (printers[i].online ? "online" : "offline") + "\n";
  }
  // A first sign-in with no LAN settings yet: the account says which printer,
  // and its access code, so only the address is left to type.
  bambu::Config config = bambu::parseConfig(bambu::store::read(bambu::store::kConfigPath, bambu::kMaxConfigBytes));
  int pick = -1;
  for (int i = 0; i < count; ++i) {
    if (printers[i].serial == config.serial) pick = i;
  }
  if (pick < 0 && config.serial.empty() && count > 0) pick = 0;
  if (pick >= 0 && (config.serial.empty() || config.accessCode.empty())) {
    config.serial = printers[pick].serial;
    if (config.accessCode.empty()) config.accessCode = printers[pick].accessCode;
    if (!bambu::store::write(bambu::store::kConfigPath, bambu::formatConfig(config))) {
      LOG_ERR("BAMBU", "config.txt was not updated from the account");
    }
  }
  extra +=
      bambu::formatConfig(bambu::parseConfig(bambu::store::read(bambu::store::kConfigPath, bambu::kMaxConfigBytes)));
  if (count < 0) {
    answer("signed_in", "Signed in. " + message, extra);
  } else if (count == 0) {
    answer("signed_in", "Signed in, but there is no printer on this account.", extra);
  } else {
    answer("signed_in", "Signed in.", extra);
  }
}

void BambuServer::handleSignOut() {
  if (!bambu::store::remove(bambu::store::kCloudPath)) {
    answer("error", "The card would not let go of the account.");
    return;
  }
  changed_ = true;
  answer("signed_out", "Signed out. The reader no longer holds anything from your Bambu account.");
}

#endif  // BAMBU_CLOUD
