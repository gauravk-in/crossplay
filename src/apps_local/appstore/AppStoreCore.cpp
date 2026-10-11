#include "AppStoreCore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../../appsdk/include/crossplay_app.h"

namespace appstore {

// --- Versions ----------------------------------------------------------------

namespace {

// A run of digits, no sign, no leading zero unless it is the only digit, at
// most five long. Returns the index after it, or `npos` on failure.
size_t readNumber(const std::string& text, size_t i, int& out) {
  const size_t start = i;
  long value = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    value = value * 10 + (text[i] - '0');
    ++i;
    if (i - start > 5) return std::string::npos;
  }
  if (i == start) return std::string::npos;
  if (text[start] == '0' && i - start > 1) return std::string::npos;
  out = static_cast<int>(value);
  return i;
}

}  // namespace

std::string ApiVersion::str() const {
  char text[16];
  std::snprintf(text, sizeof(text), "%d.%d", major, minor);
  return text;
}

bool parseApi(const std::string& text, ApiVersion& out) {
  ApiVersion v;
  size_t i = readNumber(text, 0, v.major);
  if (i == std::string::npos || i >= text.size() || text[i] != '.') return false;
  i = readNumber(text, i + 1, v.minor);
  if (i != text.size()) return false;
  out = v;
  return true;
}

ApiVersion firmwareApi() { return ApiVersion{CP_API_VERSION_MAJOR, CP_API_VERSION_MINOR}; }
ApiVersion firmwareCompatibleFrom() { return ApiVersion{CP_API_COMPATIBLE_FROM_MAJOR, CP_API_COMPATIBLE_FROM_MINOR}; }

bool parseAppVersion(const std::string& text, AppVersion& out) {
  AppVersion v;
  size_t i = readNumber(text, 0, v.major);
  if (i == std::string::npos || i >= text.size() || text[i] != '.') return false;
  i = readNumber(text, i + 1, v.minor);
  if (i == std::string::npos || i >= text.size() || text[i] != '.') return false;
  i = readNumber(text, i + 1, v.patch);
  if (i != text.size()) return false;
  out = v;
  return true;
}

// --- Names -------------------------------------------------------------------

bool validAppId(const std::string& id) {
  if (id.size() < 3 || id.size() > 48) return false;
  if (id[0] < 'a' || id[0] > 'z') return false;
  for (const char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return id.find("..") == std::string::npos;
}

bool validFileName(const char* name, const size_t len) {
  if (name == nullptr || len == 0 || len > 32 || name[0] == '.') return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = name[i];
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// --- JSON --------------------------------------------------------------------

const Json* Json::get(const char* key) const {
  if (type != Type::Object) return nullptr;
  for (size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == key) return &items[i];
  }
  return nullptr;
}

std::string Json::str(const char* key, const char* fallback) const {
  const Json* v = get(key);
  return v != nullptr && v->type == Type::String ? v->string : std::string(fallback);
}

long Json::integer(const char* key, const long fallback) const {
  const Json* v = get(key);
  return v != nullptr && v->type == Type::Number ? static_cast<long>(v->number) : fallback;
}

namespace {

class JsonParser {
 public:
  explicit JsonParser(const std::string& text) : s_(text) {}

  bool parse(Json& out) {
    if (!value(out, 0)) return false;
    skip();
    return i_ == s_.size();
  }

 private:
  static constexpr int kMaxDepth = 16;

  void skip() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
  }

  bool literal(const char* word) {
    const size_t n = std::strlen(word);
    if (s_.compare(i_, n, word) != 0) return false;
    i_ += n;
    return true;
  }

  static void utf8(std::string& out, const uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool string(std::string& out) {
    if (i_ >= s_.size() || s_[i_] != '"') return false;
    ++i_;
    while (i_ < s_.size()) {
      const char c = s_[i_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) return false;
      const char e = s_[i_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out += e;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          if (i_ + 4 > s_.size()) return false;
          uint32_t cp = 0;
          for (int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') {
              cp |= static_cast<uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
              cp |= static_cast<uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
              cp |= static_cast<uint32_t>(h - 'A' + 10);
            } else {
              return false;
            }
          }
          // Surrogate pairs are outside what a manifest needs; keep the text
          // rather than refuse it.
          utf8(out, cp >= 0xD800 && cp <= 0xDFFF ? 0xFFFD : cp);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool number(double& out) {
    const size_t start = i_;
    if (i_ < s_.size() && s_[i_] == '-') ++i_;
    while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' ||
                              s_[i_] == '+' || s_[i_] == '-')) {
      ++i_;
    }
    if (i_ == start) return false;
    const std::string text = s_.substr(start, i_ - start);
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end != nullptr && *end == '\0';
  }

  bool value(Json& out, const int depth) {
    if (depth > kMaxDepth) return false;
    skip();
    if (i_ >= s_.size()) return false;
    const char c = s_[i_];
    if (c == '{') {
      ++i_;
      out.type = Json::Type::Object;
      skip();
      if (i_ < s_.size() && s_[i_] == '}') {
        ++i_;
        return true;
      }
      while (true) {
        skip();
        std::string key;
        if (!string(key)) return false;
        skip();
        if (i_ >= s_.size() || s_[i_] != ':') return false;
        ++i_;
        Json item;
        if (!value(item, depth + 1)) return false;
        out.keys.push_back(std::move(key));
        out.items.push_back(std::move(item));
        skip();
        if (i_ < s_.size() && s_[i_] == ',') {
          ++i_;
          continue;
        }
        if (i_ < s_.size() && s_[i_] == '}') {
          ++i_;
          return true;
        }
        return false;
      }
    }
    if (c == '[') {
      ++i_;
      out.type = Json::Type::Array;
      skip();
      if (i_ < s_.size() && s_[i_] == ']') {
        ++i_;
        return true;
      }
      while (true) {
        Json item;
        if (!value(item, depth + 1)) return false;
        out.items.push_back(std::move(item));
        skip();
        if (i_ < s_.size() && s_[i_] == ',') {
          ++i_;
          continue;
        }
        if (i_ < s_.size() && s_[i_] == ']') {
          ++i_;
          return true;
        }
        return false;
      }
    }
    if (c == '"') {
      out.type = Json::Type::String;
      return string(out.string);
    }
    if (literal("true")) {
      out.type = Json::Type::Bool;
      out.boolean = true;
      return true;
    }
    if (literal("false")) {
      out.type = Json::Type::Bool;
      return true;
    }
    if (literal("null")) return true;
    out.type = Json::Type::Number;
    return number(out.number);
  }

  const std::string& s_;
  size_t i_ = 0;
};

bool isHex64(const std::string& s) {
  if (s.size() != 64) return false;
  for (const char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool parseFiles(const Json* files, std::vector<PackageFile>& out, std::vector<std::string>* urls, std::string& error) {
  if (files == nullptr || files->type != Json::Type::Array || files->items.empty()) {
    error = "lists no files";
    return false;
  }
  if (files->items.size() > kMaxFiles) {
    error = "lists too many files";
    return false;
  }
  uint64_t total = 0;
  for (const Json& f : files->items) {
    PackageFile file;
    file.name = f.str("name");
    file.sha256 = f.str("sha256");
    const long size = f.integer("size", -1);
    if (!validFileName(file.name)) {
      error = "has a file with a bad name";
      return false;
    }
    if (file.name == "manifest.json") {
      error = "lists its own manifest as a file";
      return false;
    }
    if (!isHex64(file.sha256)) {
      error = "has a file without a SHA-256";
      return false;
    }
    if (size < 0 || static_cast<unsigned long>(size) > kMaxPackageBytes) {
      error = "has a file with a bad size";
      return false;
    }
    for (const PackageFile& seen : out) {
      if (seen.name == file.name) {
        error = "lists a file twice";
        return false;
      }
    }
    file.size = static_cast<uint32_t>(size);
    total += file.size;
    out.push_back(file);
    if (urls != nullptr) urls->push_back(f.str("url"));
  }
  if (total > kMaxPackageBytes) {
    error = "is larger than 4 MB";
    return false;
  }
  return true;
}

}  // namespace

bool parseJson(const std::string& text, Json& out) {
  out = Json{};
  JsonParser parser(text);
  return parser.parse(out);
}

// --- Manifests -----------------------------------------------------------------

bool parseManifest(const std::string& text, Manifest& out, std::string& error) {
  Json root;
  if (!parseJson(text, root) || root.type != Json::Type::Object) {
    error = "The manifest is not valid JSON.";
    return false;
  }
  Manifest m;
  m.id = root.str("id");
  m.name = root.str("name");
  m.version = root.str("version");
  m.summary = root.str("summary");
  m.author = root.str("author");
  m.category = root.str("category", "apps");
  m.entry = root.str("entry", "app.wasm");
  if (!validAppId(m.id)) {
    error = "The app id is missing or not allowed.";
    return false;
  }
  if (m.name.empty() || m.name.size() > 24) {
    error = "The app name is missing or longer than 24 characters.";
    return false;
  }
  AppVersion version;
  if (!parseAppVersion(m.version, version)) {
    error = "The app version is not major.minor.patch.";
    return false;
  }
  if (!parseApi(root.str("api"), m.api)) {
    error = "The API version is not major.minor.";
    return false;
  }
  if (m.category != "games" && m.category != "apps") {
    error = "The category is neither games nor apps.";
    return false;
  }
  if (const Json* needs = root.get("requires")) {
    if (needs->type != Json::Type::Array) {
      error = "requires is not a list.";
      return false;
    }
    for (const Json& r : needs->items) {
      if (r.type == Json::Type::String && r.string == "touch") {
        m.needsTouch = true;
      } else if (r.type == Json::Type::String && r.string == "buttons") {
        m.needsButtons = true;
      } else {
        // An unknown requirement is one this firmware cannot promise.
        error = "It needs something this firmware does not know: " + (r.type == Json::Type::String ? r.string : "?");
        return false;
      }
    }
  }
  const long memoryKB = root.integer("memoryKB", 1024);
  const long quotaKB = root.integer("dataQuotaKB", 256);
  if (memoryKB < 64 || memoryKB > static_cast<long>(kMaxMemoryKB)) {
    error = "It asks for more memory than an app may have.";
    return false;
  }
  if (quotaKB < 0 || quotaKB > static_cast<long>(kMaxDataQuotaKB)) {
    error = "It asks for more storage than an app may have.";
    return false;
  }
  m.memoryKB = static_cast<uint32_t>(memoryKB);
  m.dataQuotaKB = static_cast<uint32_t>(quotaKB);
  std::string why;
  if (!parseFiles(root.get("files"), m.files, nullptr, why)) {
    error = "The package " + why + ".";
    return false;
  }
  bool hasEntry = false;
  for (const PackageFile& f : m.files) hasEntry = hasEntry || f.name == m.entry;
  if (!hasEntry) {
    error = "The package does not contain its entry file.";
    return false;
  }
  out = m;
  return true;
}

// --- The catalog ---------------------------------------------------------------

bool parseCatalog(const std::string& text, std::vector<CatalogEntry>& out, std::string& error) {
  Json root;
  if (!parseJson(text, root) || root.type != Json::Type::Object) {
    error = "The store sent something that is not a catalog.";
    return false;
  }
  const Json* apps = root.get("apps");
  if (apps == nullptr || apps->type != Json::Type::Array) {
    error = "The store's catalog has no app list.";
    return false;
  }
  out.clear();
  out.reserve(apps->items.size());
  for (const Json& a : apps->items) {
    CatalogEntry e;
    e.id = a.str("id");
    e.name = a.str("name");
    e.summary = a.str("summary");
    e.author = a.str("author");
    e.category = a.str("category", "apps");
    e.latest = a.str("latest");
    e.version = a.str("version");
    e.reason = a.str("reason");
    e.manifestUrl = a.str("manifest");
    const Json* compatible = a.get("compatible");
    e.compatible = compatible != nullptr && compatible->type == Json::Type::Bool && compatible->boolean;
    const long size = a.integer("size", 0);
    e.size = size > 0 ? static_cast<uint32_t>(size) : 0;
    // An entry this firmware cannot make sense of is left out rather than
    // failing the whole catalog: one bad app should not empty the store.
    if (!validAppId(e.id) || e.name.empty() || !parseApi(a.str("api"), e.api)) continue;
    AppVersion v;
    if (!parseAppVersion(e.latest, v)) continue;
    if (e.compatible) {
      std::string why;
      if (!parseAppVersion(e.version, v) || e.manifestUrl.empty() ||
          !parseFiles(a.get("files"), e.files, &e.fileUrls, why)) {
        continue;
      }
      // The server's answer is advice; the device checks the rule itself.
      if (!apiCompatible(e.api, firmwareApi(), firmwareCompatibleFrom())) {
        e.compatible = false;
        e.reason = "Needs app API " + e.api.str();
      }
    }
    if (!e.compatible) {
      e.version.clear();
      e.files.clear();
      e.fileUrls.clear();
      if (e.reason.empty()) e.reason = "Needs app API " + e.api.str();
    }
    out.push_back(std::move(e));
  }
  return true;
}

Offer offerFor(const CatalogEntry& entry, const std::string& installedVersion) {
  if (installedVersion.empty()) return entry.compatible ? Offer::Install : Offer::Incompatible;
  if (!entry.compatible) return Offer::Installed;
  AppVersion have;
  AppVersion offered;
  if (!parseAppVersion(installedVersion, have) || !parseAppVersion(entry.version, offered)) return Offer::Installed;
  return have < offered ? Offer::Update : Offer::Installed;
}

std::string launchProblem(const Manifest& manifest, const bool hasTouch, const bool hasButtons) {
  if (!apiCompatible(manifest.api, firmwareApi(), firmwareCompatibleFrom())) {
    if (firmwareApi() < manifest.api) {
      return "Needs a newer firmware (app API " + manifest.api.str() + ", this firmware has " + firmwareApi().str() +
             ").";
    }
    return "Built for app API " + manifest.api.str() + ", which this firmware no longer runs. Update the app.";
  }
  if (manifest.needsTouch && !hasTouch) return "Needs a touch screen.";
  if (manifest.needsButtons && !hasButtons) return "Needs buttons.";
  return "";
}

// --- Talking to the server -------------------------------------------------------

namespace {

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool isHttp(const std::string& url) { return startsWith(url, "http://") || startsWith(url, "https://"); }

std::string trimSlash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

}  // namespace

std::string resolveUrl(const std::string& server, const std::string& ref) {
  if (isHttp(ref)) return ref;
  const std::string base = trimSlash(server);
  if (!ref.empty() && ref[0] == '/') {
    const size_t scheme = base.find("://");
    const size_t path = scheme == std::string::npos ? std::string::npos : base.find('/', scheme + 3);
    return (path == std::string::npos ? base : base.substr(0, path)) + ref;
  }
  return base + "/" + ref;
}

std::string catalogUrl(const std::string& server, const bool hasTouch, const bool hasButtons, const char* firmware) {
  std::string url = trimSlash(server) + "/v1/apps?api=" + firmwareApi().str() +
                    "&from=" + firmwareCompatibleFrom().str() + "&touch=" + (hasTouch ? "1" : "0") +
                    "&buttons=" + (hasButtons ? "1" : "0");
  if (firmware != nullptr && *firmware != '\0') {
    url += "&firmware=";
    for (const char* p = firmware; *p != '\0'; ++p) {
      const char c = *p;
      const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                         c == '-' || c == '_';
      if (plain) {
        url += c;
      } else {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
        url += hex;
      }
    }
  }
  return url;
}

std::string serverFromConfig(const std::string& text) {
  Json json;
  if (!parseJson(text, json)) return "";
  const std::string server = trimSlash(json.str("server"));
  return isHttp(server) && server.size() <= 200 ? server : "";
}

// --- SHA-256 -------------------------------------------------------------------

namespace {

constexpr uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(const uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

Sha256::Sha256()
    : h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, buf_{} {}

void Sha256::block(const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
           (static_cast<uint32_t>(p[4 * i + 2]) << 8) | static_cast<uint32_t>(p[4 * i + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha256::update(const void* data, size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  total_ += len;
  while (len > 0) {
    const size_t take = len < 64 - bufLen_ ? len : 64 - bufLen_;
    std::memcpy(buf_ + bufLen_, p, take);
    bufLen_ += take;
    p += take;
    len -= take;
    if (bufLen_ == 64) {
      block(buf_);
      bufLen_ = 0;
    }
  }
}

std::string Sha256::hex() {
  const uint64_t bits = total_ * 8;
  const uint8_t pad = 0x80;
  update(&pad, 1);
  const uint8_t zero = 0;
  while (bufLen_ != 56) update(&zero, 1);
  uint8_t length[8];
  for (int i = 0; i < 8; ++i) length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  update(length, 8);
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (const uint32_t v : h_) {
    for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(v >> shift) & 0xF];
  }
  return out;
}

std::string sha256Hex(const void* data, const size_t len) {
  Sha256 sha;
  sha.update(data, len);
  return sha.hex();
}

}  // namespace appstore
