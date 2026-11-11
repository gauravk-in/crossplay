#include "BambuCloud.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace bambu {
namespace cloud {

namespace {

std::string trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// A token, a user name or an address carries no line break and no '='-split
// surprises; anything past the cap is not a value Bambu issued.
std::string capped(const std::string& value, const size_t cap) {
  std::string out;
  for (const char c : value) {
    if (c == '\r' || c == '\n') continue;
    out.push_back(c);
    if (out.size() >= cap) break;
  }
  return out;
}

int hexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Reads the JSON string whose opening quote is at `pos`; leaves `pos` past the
// closing quote. A surrogate pair comes out as two separate halves; no field
// read here (tokens, ids, names) is worth more care than that.
std::string readString(const std::string& json, size_t& pos) {
  std::string out;
  ++pos;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (c == '"') return out;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (pos >= json.size()) break;
    const char e = json[pos++];
    switch (e) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'u': {
        uint32_t cp = 0;
        int k = 0;
        for (; k < 4 && pos < json.size(); ++k) {
          const int d = hexDigit(json[pos]);
          if (d < 0) break;
          cp = cp * 16 + static_cast<uint32_t>(d);
          ++pos;
        }
        if (k == 4) appendUtf8(out, cp);
        break;
      }
      default:
        out.push_back(e);
        break;
    }
  }
  return out;
}

// Skips one string at `pos` without keeping it.
void skipString(const std::string& json, size_t& pos) {
  ++pos;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (c == '\\') {
      ++pos;
    } else if (c == '"') {
      return;
    }
  }
}

// The object starting at `pos` ('{'), as a substring; `pos` ends past it.
std::string readObject(const std::string& json, size_t& pos) {
  const size_t start = pos;
  int depth = 0;
  while (pos < json.size()) {
    const char c = json[pos];
    if (c == '"') {
      skipString(json, pos);
      continue;
    }
    ++pos;
    if (c == '{' || c == '[') {
      ++depth;
    } else if (c == '}' || c == ']') {
      if (--depth == 0) break;
    }
  }
  return json.substr(start, pos - start);
}

std::string base64UrlDecode(const std::string& in) {
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
    } else if (c == '=') {
      break;
    } else {
      return std::string();
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

bool validUser(const std::string& user) {
  if (user.size() < 3 || user.size() > 32 || user[0] != 'u' || user[1] != '_') return false;
  for (size_t i = 2; i < user.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(user[i]))) return false;
  }
  return true;
}

}  // namespace

// --- The file ------------------------------------------------------------------

Account parseAccount(const std::string& text) {
  Account account;
  size_t pos = 0;
  while (pos < text.size() && pos < kMaxFileBytes) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = trim(text.substr(pos, end - pos));
    pos = end + 1;
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "token") {
      account.token = capped(value, kMaxTokenBytes);
    } else if (key == "user") {
      account.user = validUser(value) ? value : std::string();
    } else if (key == "email") {
      account.email = capped(value, 128);
    } else if (key == "expires") {
      account.expires = std::strtoll(value.c_str(), nullptr, 10);
      if (account.expires < 0) account.expires = 0;
    }
  }
  return account;
}

std::string formatAccount(const Account& account) {
  return "email=" + account.email + "\nuser=" + account.user + "\nexpires=" + std::to_string(account.expires) +
         "\ntoken=" + account.token + "\n";
}

bool signedIn(const Account& account) { return !account.token.empty() && validUser(account.user); }

// --- Requests --------------------------------------------------------------

std::string jsonQuote(const std::string& value) {
  std::string out = "\"";
  for (const char c : value) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (u < 0x20) {
      char buf[12];
      std::snprintf(buf, sizeof(buf), "\\u%04x", u);
      out += buf;
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

std::string loginBody(const std::string& email, const std::string& password) {
  return "{\"account\":" + jsonQuote(email) + ",\"password\":" + jsonQuote(password) + ",\"apiError\":\"\"}";
}

std::string codeLoginBody(const std::string& email, const std::string& code) {
  return "{\"account\":" + jsonQuote(email) + ",\"code\":" + jsonQuote(code) + "}";
}

std::string sendCodeBody(const std::string& email) {
  return "{\"email\":" + jsonQuote(email) + ",\"type\":\"codeLogin\"}";
}

// --- Responses -------------------------------------------------------------

std::string jsonValue(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t at = 0;
  while ((at = json.find(needle, at)) != std::string::npos) {
    size_t pos = at + needle.size();
    at = pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != ':') continue;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return std::string();
    if (json[pos] == '"') return readString(json, pos);
    if (json[pos] == '{' || json[pos] == '[') return std::string();
    const size_t start = pos;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
           !std::isspace(static_cast<unsigned char>(json[pos]))) {
      ++pos;
    }
    const std::string literal = json.substr(start, pos - start);
    return literal == "null" ? std::string() : literal;
  }
  return std::string();
}

Login readLogin(const int status, const std::string& body, std::string& token, std::string& message) {
  token.clear();
  if (status == 200) {
    const std::string got = jsonValue(body, "accessToken");
    if (!got.empty() && got.size() <= kMaxTokenBytes) {
      token = got;
      return Login::Token;
    }
    const std::string type = jsonValue(body, "loginType");
    if (type == "verifyCode") return Login::NeedCode;
    if (type == "tfa") return Login::NeedTfa;
  }
  if ((status == 403 || status == 429) && body.find("loudflare") != std::string::npos) {
    message = "Bambu's servers turned the reader away for now. Try again in a few minutes.";
    return Login::Refused;
  }
  if (status == 400) {
    const std::string code = jsonValue(body, "code");
    if (code == "1") {
      message = "That code has expired. A new one is on its way to your email.";
      return Login::Refused;
    }
    if (code == "2") {
      message = "That code is not the one Bambu sent. Check the email and try again.";
      return Login::Refused;
    }
  }
  if (status <= 0) {
    message = "Could not reach Bambu. Check the reader's Wi-Fi and try again.";
    return Login::Refused;
  }
  std::string said = jsonValue(body, "error");
  if (said.empty()) said = jsonValue(body, "message");
  if (!said.empty() && said.size() < 200) {
    message = "Bambu said: " + said;
  } else if (status >= 400 && status < 500) {
    message = "Bambu did not accept that email and password.";
  } else {
    message = "Bambu's sign-in did not answer properly. Try again later.";
  }
  return Login::Refused;
}

bool readToken(const std::string& token, std::string& user, int64_t& expires) {
  const size_t a = token.find('.');
  if (a == std::string::npos) return false;
  const size_t b = token.find('.', a + 1);
  if (b == std::string::npos || token.find('.', b + 1) != std::string::npos) return false;
  const std::string payload = base64UrlDecode(token.substr(a + 1, b - a - 1));
  if (payload.empty()) return false;
  const std::string name = jsonValue(payload, "username");
  if (!validUser(name)) return false;
  user = name;
  const std::string exp = jsonValue(payload, "exp");
  expires = exp.empty() ? 0 : std::strtoll(exp.c_str(), nullptr, 10);
  if (expires < 0) expires = 0;
  return true;
}

std::string userFromPreference(const std::string& body) {
  const std::string uid = jsonValue(body, "uid");
  if (uid.empty()) return std::string();
  const std::string user = "u_" + uid;
  return validUser(user) ? user : std::string();
}

int readPrinters(const std::string& body, Printer* out, const int max) {
  const size_t key = body.find("\"devices\"");
  if (key == std::string::npos) return 0;
  size_t pos = body.find('[', key);
  if (pos == std::string::npos) return 0;
  ++pos;
  int n = 0;
  while (pos < body.size() && n < max) {
    const char c = body[pos];
    if (c == ']') break;
    if (c != '{') {
      ++pos;
      continue;
    }
    const std::string object = readObject(body, pos);
    Printer p;
    p.serial = capped(jsonValue(object, "dev_id"), 32);
    for (char& ch : p.serial) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (p.serial.empty()) continue;
    p.name = capped(jsonValue(object, "name"), 48);
    p.model = capped(jsonValue(object, "dev_product_name"), 32);
    if (p.model.empty()) p.model = capped(jsonValue(object, "dev_model_name"), 32);
    p.accessCode = capped(jsonValue(object, "dev_access_code"), 16);
    p.online = jsonValue(object, "online") == "true";
    out[n++] = p;
  }
  return n;
}

std::string untilPhrase(const int64_t expires) {
  if (expires <= 0) return "Signed in";
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const time_t t = static_cast<time_t>(expires);
  struct tm day{};
  gmtime_r(&t, &day);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "Signed in until %d %s %d", day.tm_mday, kMonths[day.tm_mon % 12],
                day.tm_year + 1900);
  return buf;
}

}  // namespace cloud
}  // namespace bambu
