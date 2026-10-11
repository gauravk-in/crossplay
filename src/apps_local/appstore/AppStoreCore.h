#pragma once

// The rules of downloadable apps, freestanding so host-tests/appstore can
// check every one of them without a device: API version compatibility, the
// manifest and catalog formats, the names an app may use, and SHA-256 for
// checking a download against its manifest.
//
// No Arduino, no storage, no network, no JSON library. The activity reads and
// writes the bytes these functions produce, and nothing else.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace appstore {

// --- Versions ----------------------------------------------------------------

// An API version, "major.minor".
struct ApiVersion {
  int major = 0;
  int minor = 0;

  bool operator==(const ApiVersion& o) const { return major == o.major && minor == o.minor; }
  bool operator<(const ApiVersion& o) const { return major != o.major ? major < o.major : minor < o.minor; }
  bool operator<=(const ApiVersion& o) const { return !(o < *this); }
  int32_t packed() const { return (major << 16) | minor; }
  static ApiVersion fromPacked(int32_t v) { return ApiVersion{v >> 16, v & 0xFFFF}; }
  std::string str() const;
};

// "1.0" -> {1, 0}. False for anything else, including "1", "1.0.0", "v1.0".
bool parseApi(const std::string& text, ApiVersion& out);

// The API this firmware provides, and the oldest app API it still runs.
// Kept in step with appsdk/include/crossplay_app.h and docs/app-api/compat.json
// by host-tests/appstore.
ApiVersion firmwareApi();
ApiVersion firmwareCompatibleFrom();

// An app built against `app` runs on a firmware that provides `provides` and
// still runs everything from `from` up.
inline bool apiCompatible(const ApiVersion& app, const ApiVersion& provides, const ApiVersion& from) {
  return from <= app && app <= provides;
}

// An app's own version, "major.minor.patch", compared numerically.
struct AppVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;
  bool operator<(const AppVersion& o) const {
    if (major != o.major) return major < o.major;
    if (minor != o.minor) return minor < o.minor;
    return patch < o.patch;
  }
  bool operator==(const AppVersion& o) const { return major == o.major && minor == o.minor && patch == o.patch; }
};
bool parseAppVersion(const std::string& text, AppVersion& out);

// --- Names -------------------------------------------------------------------

// An app id: 3 to 48 characters of [a-z0-9._-], starting with a letter, no
// "..". Reverse-DNS by convention ("org.crossplay.memory"). It names a folder
// on the card, so this is what keeps an id from reaching outside it.
bool validAppId(const std::string& id);
// A file inside a package or an app's data folder: 1 to 32 characters of
// [A-Za-z0-9._-], not starting with a dot.
bool validFileName(const char* name, size_t len);
inline bool validFileName(const std::string& name) { return validFileName(name.data(), name.size()); }

// --- JSON --------------------------------------------------------------------

// Just enough JSON for manifests and the catalog: objects, arrays, strings
// (with \uXXXX for the BMP), numbers, booleans, null. Depth-limited.
struct Json {
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<Json> items;                 // Array
  std::vector<std::string> keys;           // Object, parallel to items
  const Json* get(const char* key) const;  // nullptr when absent or not an object
  std::string str(const char* key, const char* fallback = "") const;
  long integer(const char* key, long fallback = 0) const;
};
bool parseJson(const std::string& text, Json& out);

// --- Manifests -----------------------------------------------------------------

struct PackageFile {
  std::string name;
  uint32_t size = 0;
  std::string sha256;  // lowercase hex
};

enum class Requirement : uint8_t { Touch, Buttons };

struct Manifest {
  std::string id;
  std::string name;
  std::string version;
  ApiVersion api;
  std::string summary;
  std::string author;
  std::string category;  // "games" or "apps"
  std::string entry = "app.wasm";
  bool needsTouch = false;
  bool needsButtons = false;
  uint32_t memoryKB = 1024;    // the most linear memory the app may grow to
  uint32_t dataQuotaKB = 256;  // the most its data folder may hold
  std::vector<PackageFile> files;
};

// The limits a manifest may ask for. A package beyond them is refused rather
// than trimmed: an app that asked for more memory than it got fails later and
// further from the cause.
constexpr uint32_t kMaxMemoryKB = 4096;
constexpr uint32_t kMaxDataQuotaKB = 4096;
constexpr uint32_t kMaxPackageBytes = 4u * 1024 * 1024;
constexpr size_t kMaxFiles = 8;

// Parses and validates. On failure `error` says what was wrong, in words a
// person can act on.
bool parseManifest(const std::string& text, Manifest& out, std::string& error);

// --- The catalog ---------------------------------------------------------------

// One app as the store server lists it for this device. `version` is the
// newest version that runs here, empty when none does; `latest` is the newest
// at all, so a store can say an update exists for a newer firmware.
struct CatalogEntry {
  std::string id;
  std::string name;
  std::string summary;
  std::string author;
  std::string category;
  std::string latest;
  std::string version;
  ApiVersion api;  // of `version`, or of `latest` when nothing is compatible
  uint32_t size = 0;
  bool compatible = false;
  std::string reason;       // why not, when not compatible
  std::string manifestUrl;  // of `version`
  std::vector<PackageFile> files;
  std::vector<std::string> fileUrls;  // parallel to files
};

bool parseCatalog(const std::string& text, std::vector<CatalogEntry>& out, std::string& error);

// What the store offers for one catalog entry, given what is installed.
enum class Offer : uint8_t {
  Install,       // not installed, a compatible version exists
  Update,        // installed, and a newer compatible version exists
  Installed,     // installed and current
  Incompatible,  // no version runs on this firmware
};
Offer offerFor(const CatalogEntry& entry, const std::string& installedVersion);

// Why an installed app will not start here, or "" when it will.
std::string launchProblem(const Manifest& manifest, bool hasTouch, bool hasButtons);

// --- Talking to the server -------------------------------------------------------

// A URL the catalog gave, made absolute against the server's address. Absolute
// http(s) URLs pass through; "/path" is taken from the server's origin;
// anything else is relative to the server address itself.
std::string resolveUrl(const std::string& server, const std::string& ref);

// The catalog request for this firmware and device: the server marks each app
// compatible or not with the same rule the device applies, and says why.
std::string catalogUrl(const std::string& server, bool hasTouch, bool hasButtons, const char* firmware);

// The server address from /.crosspoint/appstore.json ({"server": "..."}), or ""
// when the file has none. Only http and https addresses are taken.
std::string serverFromConfig(const std::string& text);

// --- SHA-256 -------------------------------------------------------------------

class Sha256 {
 public:
  Sha256();
  void update(const void* data, size_t len);
  std::string hex();  // finishes; call once

 private:
  void block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t bufLen_ = 0;
  uint64_t total_ = 0;
};

std::string sha256Hex(const void* data, size_t len);

}  // namespace appstore
