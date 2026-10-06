#include "GTasksCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "../../network/DeviceReportCore.h"

namespace gtasks {
namespace {

constexpr const char* kCacheHeader = "gtasks 1";

std::string trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

// Every line, without its terminator. A final line with no newline counts.
std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    start = end + 1;
  }
  return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      out.push_back(line.substr(start));
      return out;
    }
    out.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

// A field may not carry the separators of the format it is written into.
std::string flatten(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return out;
}

bool positionLess(const Task& a, const Task& b) {
  // Google's positions are equal-length digit strings, so this is numeric
  // order. Ties fall back to the title so a reload never reshuffles.
  if (a.position != b.position) return a.position < b.position;
  return a.title < b.title;
}

bool isKnownChoice(const uint16_t minutes) {
  for (const uint16_t choice : kPollChoices) {
    if (choice == minutes) return true;
  }
  return false;
}

}  // namespace

namespace {

// Every `key=value` line of a small config file, in order. Blank lines and
// `#` comments are skipped; only the first '=' separates.
std::vector<std::pair<std::string, std::string>> keyValues(const std::string& text) {
  std::vector<std::pair<std::string, std::string>> out;
  for (const std::string& raw : lines(text)) {
    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    out.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
  }
  return out;
}

std::string base64Url(const uint8_t* data, const size_t len) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve((len * 4 + 2) / 3);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t n =
        (static_cast<uint32_t>(data[i]) << 16) | (i + 1 < len ? data[i + 1] << 8 : 0) | (i + 2 < len ? data[i + 2] : 0);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    if (i + 1 < len) out += kAlphabet[(n >> 6) & 63];
    if (i + 2 < len) out += kAlphabet[n & 63];
  }
  return out;
}

// base64url without padding, as JWT segments are; '+' and '/' are taken too.
// Stops at the first character that is neither.
std::string fromBase64Url(const std::string& in) {
  std::string out;
  uint32_t acc = 0;
  int bits = 0;
  for (const char c : in) {
    int v;
    if (c >= 'A' && c <= 'Z') {
      v = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      v = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      v = c - '0' + 52;
    } else if (c == '-' || c == '+') {
      v = 62;
    } else if (c == '_' || c == '/') {
      v = 63;
    } else {
      break;
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFF);
    }
  }
  return out;
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size() && hexValue(in[i + 1]) >= 0 && hexValue(in[i + 2]) >= 0) {
      out += static_cast<char>(hexValue(in[i + 1]) * 16 + hexValue(in[i + 2]));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

}  // namespace

Credentials parseCredentials(const std::string& text) {
  Credentials out;
  for (const auto& [key, value] : keyValues(text)) {
    if (key == "refresh_token") {
      out.refreshToken = value;
    } else if (key == "account") {
      out.account = value;
    }
  }
  return out;
}

std::string serializeCredentials(const Credentials& creds) {
  return "refresh_token=" + creds.refreshToken + "\naccount=" + creds.account + "\n";
}

Client parseClient(const std::string& text) {
  Client out;
  for (const auto& [key, value] : keyValues(text)) {
    if (key == "client_id") {
      out.id = value;
    } else if (key == "client_secret") {
      out.secret = value;
    }
  }
  return out;
}

std::string pkceChallenge(const std::string& verifier) {
  uint8_t digest[32];
  devreport::sha256(reinterpret_cast<const uint8_t*>(verifier.data()), verifier.size(), digest);
  return base64Url(digest, sizeof(digest));
}

std::string authUrl(const std::string& clientId, const std::string& challenge, const std::string& state) {
  return std::string("https://accounts.google.com/o/oauth2/v2/auth?client_id=") + formEncode(clientId) +
         "&redirect_uri=" + formEncode(kRedirectUri) + "&response_type=code&scope=" + formEncode(kScopes) +
         // offline + consent is what makes Google hand out a refresh token on
         // every sign-in, not only the first one for this client.
         "&access_type=offline&prompt=consent&code_challenge=" + formEncode(challenge) +
         "&code_challenge_method=S256&state=" + formEncode(state);
}

Pasted parsePasted(const std::string& pasted, const std::string& expectedState) {
  Pasted out;
  std::string text = trim(pasted);
  const size_t question = text.find('?');
  if (question != std::string::npos) text = text.substr(question + 1);
  const size_t hash = text.find('#');
  if (hash != std::string::npos) text = text.substr(0, hash);

  std::string code;
  std::string state;
  std::string error;
  std::string scope;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('&', start);
    if (end == std::string::npos) end = text.size();
    const std::string pair = text.substr(start, end - start);
    start = end + 1;
    const size_t eq = pair.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(pair.substr(0, eq));
    const std::string value = urlDecode(trim(pair.substr(eq + 1)));
    if (key == "code") code = value;
    if (key == "state") state = value;
    if (key == "error") error = value;
    if (key == "scope") scope = value;
  }

  if (!error.empty()) {
    out.message = error == "access_denied" ? "Google was told no, so nothing changed. Start again to sign in."
                                           : "Google did not sign you in (" + error + "). Start again.";
    return out;
  }
  if (code.empty()) {
    out.message = "That address has no code in it. Copy the whole address of the page Google sent you to.";
    return out;
  }
  if (state != expectedState) {
    out.message = "That address is from a different sign-in. Open the link on this page and try again.";
    return out;
  }
  // Google's consent screen lets a person untick a permission. Without Tasks
  // the reader would sign in and then fail every sync.
  if (!scope.empty() && scope.find("https://www.googleapis.com/auth/tasks") == std::string::npos) {
    out.message = "Google Tasks was not ticked. Start again and leave its box ticked.";
    return out;
  }
  out.code = code;
  return out;
}

std::string idTokenEmail(const std::string& idToken) {
  const size_t first = idToken.find('.');
  if (first == std::string::npos) return std::string();
  const size_t second = idToken.find('.', first + 1);
  if (second == std::string::npos) return std::string();
  const std::string payload = fromBase64Url(idToken.substr(first + 1, second - first - 1));
  // The claim's name in quotes, so "email_verified" is not taken for it.
  size_t at = payload.find("\"email\"");
  if (at == std::string::npos) return std::string();
  at = payload.find(':', at + 7);
  if (at == std::string::npos) return std::string();
  at = payload.find('"', at + 1);
  if (at == std::string::npos) return std::string();
  const size_t end = payload.find('"', at + 1);
  if (end == std::string::npos) return std::string();
  const std::string email = payload.substr(at + 1, end - at - 1);
  return email.find('\\') == std::string::npos ? email : std::string();
}

std::string serializeTasks(const std::vector<Task>& tasks) {
  std::string out = kCacheHeader;
  out += '\n';
  for (const Task& t : tasks) {
    if (t.id.empty()) continue;
    out += flatten(t.id);
    out += '\t';
    out += t.pending ? '1' : '0';
    out += '\t';
    out += flatten(t.position);
    out += '\t';
    out += flatten(t.parent);
    out += '\t';
    out += flatten(t.due);
    out += '\t';
    out += flatten(t.title);
    out += '\n';
  }
  return out;
}

std::vector<Task> parseTasks(const std::string& text) {
  std::vector<Task> out;
  const std::vector<std::string> all = lines(text);
  if (all.empty() || all[0] != kCacheHeader) return out;
  out.reserve(all.size() - 1);
  for (size_t i = 1; i < all.size(); ++i) {
    const std::vector<std::string> f = splitTabs(all[i]);
    if (f.size() != 6 || f[0].empty() || (f[1] != "0" && f[1] != "1")) continue;
    Task t;
    t.id = f[0];
    t.pending = f[1] == "1";
    t.position = f[2];
    t.parent = f[3];
    t.due = f[4];
    t.title = f[5];
    out.push_back(std::move(t));
  }
  return out;
}

bool isChild(const Task& task, const std::vector<Task>& all) {
  if (task.parent.empty()) return false;
  for (const Task& t : all) {
    if (t.id == task.parent) return true;
  }
  return false;
}

void sortForDisplay(std::vector<Task>& tasks) {
  std::vector<Task> parents;
  std::vector<Task> children;
  parents.reserve(tasks.size());
  children.reserve(tasks.size());
  // Decided before anything is moved: isChild looks the parent up by id, and a
  // moved-from task has none.
  std::vector<bool> child(tasks.size());
  for (size_t i = 0; i < tasks.size(); ++i) child[i] = isChild(tasks[i], tasks);
  for (size_t i = 0; i < tasks.size(); ++i) {
    Task& t = tasks[i];
    if (child[i]) {
      children.push_back(std::move(t));
    } else {
      parents.push_back(std::move(t));
    }
  }
  std::stable_sort(parents.begin(), parents.end(), positionLess);
  std::stable_sort(children.begin(), children.end(), positionLess);
  std::vector<Task> out;
  out.reserve(parents.size() + children.size());
  for (Task& p : parents) {
    const std::string id = p.id;
    out.push_back(std::move(p));
    for (Task& c : children) {
      if (c.parent == id) out.push_back(c);
    }
  }
  tasks = std::move(out);
}

std::vector<Task> merge(const std::vector<Task>& local, const std::vector<Task>& fresh) {
  std::vector<Task> out = fresh;
  for (Task& t : out) {
    t.pending = false;
    for (const Task& l : local) {
      if (l.id == t.id) {
        t.pending = l.pending;
        break;
      }
    }
  }
  sortForDisplay(out);
  return out;
}

std::vector<std::string> pendingIds(const std::vector<Task>& tasks) {
  std::vector<std::string> out;
  for (const Task& t : tasks) {
    if (t.pending) out.push_back(t.id);
  }
  return out;
}

int pendingCount(const std::vector<Task>& tasks) {
  int n = 0;
  for (const Task& t : tasks) n += t.pending ? 1 : 0;
  return n;
}

std::string dueDate(const std::string& rfc3339) {
  if (rfc3339.size() < 10 || rfc3339[4] != '-' || rfc3339[7] != '-') return std::string();
  return rfc3339.substr(0, 10);
}

std::string dueLabel(const std::string& due) {
  static constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  if (due.size() != 10 || due[4] != '-' || due[7] != '-') return std::string();
  for (const size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (due[i] < '0' || due[i] > '9') return std::string();
  }
  const int month = std::atoi(due.substr(5, 2).c_str());
  const int day = std::atoi(due.substr(8, 2).c_str());
  if (month < 1 || month > 12 || day < 1 || day > 31) return std::string();
  char out[16];
  std::snprintf(out, sizeof(out), "DUE %d %s", day, kMonths[month - 1]);
  return out;
}

Settings parseSettings(const std::string& text) {
  Settings out;
  for (const std::string& raw : lines(text)) {
    const std::string line = trim(raw);
    const size_t eq = line.find('=');
    if (eq == std::string::npos || trim(line.substr(0, eq)) != "poll_minutes") continue;
    const std::string value = trim(line.substr(eq + 1));
    if (value.empty() || value.size() > 4 || value.find_first_not_of("0123456789") != std::string::npos) continue;
    const int minutes = std::atoi(value.c_str());
    if (isKnownChoice(static_cast<uint16_t>(minutes))) out.pollMinutes = static_cast<uint16_t>(minutes);
  }
  return out;
}

std::string serializeSettings(const Settings& settings) {
  return "poll_minutes=" + std::to_string(settings.pollMinutes) + "\n";
}

uint16_t nextPollMinutes(const uint16_t minutes) {
  constexpr size_t kCount = sizeof(kPollChoices) / sizeof(kPollChoices[0]);
  for (size_t i = 0; i < kCount; ++i) {
    if (kPollChoices[i] == minutes) return kPollChoices[(i + 1) % kCount];
  }
  return kPollChoices[0];
}

std::string pollLabel(const uint16_t minutes) {
  if (minutes == 0) return "OFF";
  if (minutes == 1) return "EVERY MIN";
  if (minutes == 60) return "EVERY HOUR";
  return "EVERY " + std::to_string(minutes) + " MIN";
}

bool pollDue(const bool charging, const uint16_t pollMinutes, const bool everAttempted, const uint32_t nowMs,
             const uint32_t lastAttemptMs) {
  if (!charging || pollMinutes == 0) return false;
  if (!everAttempted) return true;
  // Unsigned subtraction, so millis() wrapping after 49 days is still right.
  return nowMs - lastAttemptMs >= static_cast<uint32_t>(pollMinutes) * 60000u;
}

std::string formEncode(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (const char ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

bool safeId(const std::string& id) {
  if (id.empty() || id.size() > 128) return false;
  for (const char c : id) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace gtasks
