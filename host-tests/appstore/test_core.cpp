// host-tests/appstore: the rules in AppStoreCore, without a device.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/apps_local/appstore/AppStoreCore.h"

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

using namespace appstore;

const char* kHash = "0000000000000000000000000000000000000000000000000000000000000000";

std::string manifest(const std::string& fields) {
  return "{\"id\":\"org.crossplay.memory\",\"name\":\"Memory\",\"version\":\"1.2.3\",\"api\":\"1.0\"," + fields +
         "\"files\":[{\"name\":\"app.wasm\",\"size\":1000,\"sha256\":\"" + kHash + "\"}]}";
}

void versions() {
  ApiVersion v;
  check(parseApi("1.0", v) && v.major == 1 && v.minor == 0, "1.0 parses");
  check(parseApi("2.13", v) && v.major == 2 && v.minor == 13, "2.13 parses");
  check(!parseApi("1", v), "1 is not an API version");
  check(!parseApi("1.0.0", v), "1.0.0 is not an API version");
  check(!parseApi("v1.0", v), "v1.0 is not an API version");
  check(!parseApi("01.0", v), "a leading zero is refused");
  check(!parseApi("1.", v), "1. is refused");
  check(!parseApi("", v), "empty is refused");

  // compatibleFrom <= app <= provides
  const ApiVersion from{1, 2};
  const ApiVersion provides{1, 5};
  check(apiCompatible({1, 2}, provides, from), "the oldest supported API runs");
  check(apiCompatible({1, 5}, provides, from), "the firmware's own API runs");
  check(!apiCompatible({1, 6}, provides, from), "an app newer than the firmware does not run");
  check(!apiCompatible({1, 1}, provides, from), "an app older than a break does not run");
  check(!apiCompatible({2, 0}, provides, from), "a newer major does not run");
  check(apiCompatible({1, 9}, {2, 0}, {1, 0}), "a major bump that kept compatibility runs old apps");

  check(firmwareCompatibleFrom() <= firmwareApi(), "the firmware runs its own API");

  AppVersion a, b;
  check(parseAppVersion("1.2.3", a) && a.major == 1 && a.minor == 2 && a.patch == 3, "1.2.3 parses");
  check(parseAppVersion("1.10.0", b) && a < b, "1.10.0 is newer than 1.2.3, numerically");
  check(!parseAppVersion("1.2", a), "1.2 is not an app version");
}

void names() {
  check(validAppId("org.crossplay.memory"), "a reverse-DNS id is valid");
  check(!validAppId("Org.crossplay"), "an id with capitals is refused");
  check(!validAppId("../etc"), "an id with a path is refused");
  check(!validAppId("a..b"), "an id with .. is refused");
  check(!validAppId("ab"), "a two-character id is refused");
  check(!validAppId("1abc"), "an id starting with a digit is refused");
  check(validFileName(std::string("history.txt")), "a plain file name is valid");
  check(!validFileName(std::string(".hidden")), "a dot file is refused");
  check(!validFileName(std::string("a/b")), "a slash is refused");
  check(!validFileName(std::string("..")), "dot-dot is refused");
  check(!validFileName(std::string(33, 'a')), "33 characters are refused");
  check(!validFileName(std::string("")), "an empty name is refused");
}

void json() {
  Json j;
  check(parseJson("{\"a\":[1,2,{\"b\":\"c\\u00e9\\n\"}],\"t\":true,\"n\":null}", j), "nested JSON parses");
  check(j.get("a") != nullptr && j.get("a")->items.size() == 3, "arrays keep their items");
  check(j.get("a")->items[2].str("b") == "c\xC3\xA9\n", "escapes decode to UTF-8");
  check(!parseJson("{\"a\":1,}", j), "a trailing comma is refused");
  check(!parseJson("{\"a\":1} x", j), "trailing garbage is refused");
  std::string deep;
  for (int i = 0; i < 40; ++i) deep += "[";
  for (int i = 0; i < 40; ++i) deep += "]";
  check(!parseJson(deep, j), "nesting beyond the limit is refused");
}

void manifests() {
  Manifest m;
  std::string error;
  check(parseManifest(manifest("\"requires\":[\"touch\"],"), m, error), "a good manifest parses");
  check(m.needsTouch && !m.needsButtons, "requires is read");
  check(m.entry == "app.wasm" && m.files.size() == 1, "files and entry are read");
  check(launchProblem(m, true, true).empty(), "it launches on a touch device");
  check(!launchProblem(m, false, true).empty(), "it does not launch without touch");

  check(!parseManifest(manifest("\"requires\":[\"camera\"],"), m, error), "an unknown requirement is refused");
  check(!parseManifest(manifest("\"memoryKB\":999999,"), m, error), "too much memory is refused");
  check(!parseManifest(manifest("\"entry\":\"main.wasm\","), m, error), "a missing entry file is refused");
  check(!parseManifest(manifest("\"category\":\"tools\","), m, error), "an unknown category is refused");
  check(!parseManifest("{\"id\":\"../x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api\":\"1.0\",\"files\":[]}", m, error),
        "a path in the id is refused");
  const std::string twice = "{\"id\":\"org.a.b\",\"name\":\"X\",\"version\":\"1.0.0\",\"api\":\"1.0\",\"files\":[" +
                            std::string("{\"name\":\"app.wasm\",\"size\":1,\"sha256\":\"") + kHash + "\"}," +
                            "{\"name\":\"app.wasm\",\"size\":1,\"sha256\":\"" + kHash + "\"}]}";
  check(!parseManifest(twice, m, error), "a file listed twice is refused");
  const std::string huge = "{\"id\":\"org.a.b\",\"name\":\"X\",\"version\":\"1.0.0\",\"api\":\"1.0\",\"files\":[" +
                           std::string("{\"name\":\"app.wasm\",\"size\":9999999,\"sha256\":\"") + kHash + "\"}]}";
  check(!parseManifest(huge, m, error), "a package over the limit is refused");

}

void catalog() {
  const std::string text =
      std::string("{\"apps\":[") +
      // compatible, newer than installed
      "{\"id\":\"org.a.one\",\"name\":\"One\",\"latest\":\"1.1.0\",\"version\":\"1.1.0\",\"api\":\"1.0\"," +
      "\"compatible\":true,\"manifest\":\"/m\",\"files\":[{\"name\":\"app.wasm\",\"size\":10,\"sha256\":\"" + kHash +
      "\",\"url\":\"/f\"}]}," +
      // the server says compatible, the device knows better
      "{\"id\":\"org.a.two\",\"name\":\"Two\",\"latest\":\"1.0.0\",\"version\":\"1.0.0\",\"api\":\"9.0\"," +
      "\"compatible\":true,\"manifest\":\"/m\",\"files\":[{\"name\":\"app.wasm\",\"size\":10,\"sha256\":\"" + kHash +
      "\",\"url\":\"/f\"}]}," +
      // incompatible
      "{\"id\":\"org.a.three\",\"name\":\"Three\",\"latest\":\"2.0.0\",\"api\":\"2.0\",\"compatible\":false," +
      "\"reason\":\"Needs app API 2.0\"}," +
      // garbage is skipped, not fatal
      "{\"id\":\"../bad\",\"name\":\"Bad\",\"latest\":\"1.0.0\",\"api\":\"1.0\"}" + "]}";
  std::vector<CatalogEntry> entries;
  std::string error;
  check(parseCatalog(text, entries, error), "the catalog parses");
  check(entries.size() == 3, "a bad entry is skipped and the rest kept");
  if (entries.size() == 3) {
    check(entries[0].compatible && entries[0].fileUrls.size() == 1, "a compatible entry keeps its files");
    check(!entries[1].compatible, "the device re-checks the server's compatibility");
    check(!entries[2].compatible && entries[2].files.empty(), "an incompatible entry has nothing to download");
    check(offerFor(entries[0], "") == Offer::Install, "not installed: install");
    check(offerFor(entries[0], "1.0.9") == Offer::Update, "older installed: update");
    check(offerFor(entries[0], "1.1.0") == Offer::Installed, "same installed: installed");
    check(offerFor(entries[2], "") == Offer::Incompatible, "nothing runs here: incompatible");
    check(offerFor(entries[2], "1.0.0") == Offer::Installed,
          "installed, and only incompatible updates exist: keep what is there");
  }
  check(!parseCatalog("[]", entries, error), "a non-object is refused");
}

void urls() {
  check(resolveUrl("http://h:8080", "/v1/x") == "http://h:8080/v1/x", "a rooted path takes the server's origin");
  check(resolveUrl("http://h:8080/store/", "/v1/x") == "http://h:8080/v1/x", "a rooted path drops the server's path");
  check(resolveUrl("http://h/store", "v1/x") == "http://h/store/v1/x", "a relative path hangs off the server");
  check(resolveUrl("http://h", "https://cdn/a.wasm") == "https://cdn/a.wasm", "an absolute URL passes through");
  const std::string url = catalogUrl("http://h/", true, false, "2026.1 x");
  check(url.rfind("http://h/v1/apps?api=", 0) == 0, "the catalog URL starts at /v1/apps");
  check(url.find("&touch=1&buttons=0") != std::string::npos, "the catalog URL carries the device");
  check(url.find("&firmware=2026.1%20x") != std::string::npos, "the firmware version is escaped");
  check(serverFromConfig("{\"server\":\"http://192.168.1.5:8080/\"}") == "http://192.168.1.5:8080",
        "the configured server is read, without its trailing slash");
  check(serverFromConfig("{\"server\":\"ftp://x\"}").empty(), "only http and https servers are taken");
  check(serverFromConfig("not json").empty(), "a broken config is no server");
}

void sha() {
  check(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");
  check(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 of abc");
  const std::string m(1000, 'a');
  Sha256 split;
  split.update(m.data(), 1);
  split.update(m.data() + 1, 63);
  split.update(m.data() + 64, 936);
  check(split.hex() == sha256Hex(m.data(), m.size()), "sha256 does not depend on how the bytes arrive");
  check(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56) ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "sha256 of the two-block vector");
}

}  // namespace

int main() {
  versions();
  names();
  json();
  manifests();
  catalog();
  urls();
  sha();
  if (failures != 0) {
    std::printf("%d appstore check(s) failed\n", failures);
    return 1;
  }
  std::printf("appstore: all checks passed\n");
  return 0;
}
