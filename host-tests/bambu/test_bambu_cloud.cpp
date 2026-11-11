// The Bambu account's rules, checked without a network: the token file, the
// request bodies, and how each of Bambu's replies is read.

#include <cstdio>
#include <string>

#include "BambuCloud.h"

using namespace bambu::cloud;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

// {"username": "u_1234567890", "exp": 1800000000}
constexpr const char* kJwt =
    "eyJhbGciOiJSUzI1NiJ9.eyJ1c2VybmFtZSI6ICJ1XzEyMzQ1Njc4OTAiLCAiZXhwIjogMTgwMDAwMDAwMH0.c2ln";

void testAccount() {
  Account account;
  account.token = kJwt;
  account.user = "u_1234567890";
  account.email = "someone@example.com";
  account.expires = 1800000000;
  const Account back = parseAccount(formatAccount(account));
  CHECK(back.token == account.token);
  CHECK(back.user == account.user);
  CHECK(back.email == account.email);
  CHECK(back.expires == account.expires);
  CHECK(signedIn(back));

  CHECK(!signedIn(Account{}));
  CHECK(!signedIn(parseAccount("token=abc\n")));
  // A user name that is not Bambu's shape is dropped, not trusted.
  CHECK(parseAccount("token=abc\nuser=bblp\n").user.empty());
  CHECK(parseAccount("token=abc\nuser=u_12x\n").user.empty());
  CHECK(signedIn(parseAccount("# comment\n  token = abc \nuser=u_42\nunknown=1\n")));
  CHECK(parseAccount("expires=-5\n").expires == 0);
  // An oversized token is cut at the cap rather than read without end.
  CHECK(parseAccount("token=" + std::string(kMaxTokenBytes + 100, 'a') + "\n").token.size() <= kMaxTokenBytes);
}

void testBodies() {
  CHECK(jsonQuote("plain") == "\"plain\"");
  CHECK(jsonQuote("a\"b\\c") == "\"a\\\"b\\\\c\"");
  CHECK(jsonQuote(std::string("tab\tnl\n")) == "\"tab\\u0009nl\\u000a\"");
  CHECK(jsonQuote("pässwörd") == "\"pässwörd\"");
  CHECK(loginBody("me@x.com", "p\"w") == "{\"account\":\"me@x.com\",\"password\":\"p\\\"w\",\"apiError\":\"\"}");
  CHECK(codeLoginBody("me@x.com", "123456") == "{\"account\":\"me@x.com\",\"code\":\"123456\"}");
  CHECK(sendCodeBody("me@x.com") == "{\"email\":\"me@x.com\",\"type\":\"codeLogin\"}");
}

void testJsonValue() {
  const std::string json =
      "{\"a\": \"x\\\"y\\u00e9\", \"n\": 42 , \"t\":true, \"z\":null, \"o\":{\"inner\":\"deep\"}, \"arr\":[1]}";
  CHECK(jsonValue(json, "a") == "x\"y\xc3\xa9");
  CHECK(jsonValue(json, "n") == "42");
  CHECK(jsonValue(json, "t") == "true");
  CHECK(jsonValue(json, "z").empty());
  CHECK(jsonValue(json, "inner") == "deep");
  CHECK(jsonValue(json, "o").empty());
  CHECK(jsonValue(json, "missing").empty());
  // A value that spells a key is not a key.
  CHECK(jsonValue("{\"b\":\"a\",\"a\":\"real\"}", "a") == "real");
  CHECK(jsonValue("{\"a\"", "a").empty());
  CHECK(jsonValue("", "a").empty());
}

void testLogin() {
  std::string token;
  std::string message;
  CHECK(readLogin(200, "{\"accessToken\":\"tok\",\"refreshToken\":\"r\",\"expiresIn\":7776000}", token, message) ==
        Login::Token);
  CHECK(token == "tok");

  CHECK(readLogin(200, "{\"accessToken\":\"\",\"loginType\":\"verifyCode\"}", token, message) == Login::NeedCode);
  CHECK(token.empty());
  CHECK(readLogin(200, "{\"accessToken\":\"\",\"loginType\":\"tfa\",\"tfaKey\":\"k\"}", token, message) ==
        Login::NeedTfa);

  message.clear();
  CHECK(readLogin(400, "{\"code\":1,\"error\":\"expired\"}", token, message) == Login::Refused);
  CHECK(message.find("expired") != std::string::npos);
  CHECK(readLogin(400, "{\"code\":2}", token, message) == Login::Refused);
  CHECK(message.find("not the one") != std::string::npos);

  CHECK(readLogin(400, "{\"error\":\"Incorrect password\"}", token, message) == Login::Refused);
  CHECK(message == "Bambu said: Incorrect password");
  CHECK(readLogin(401, "", token, message) == Login::Refused);
  CHECK(message == "Bambu did not accept that email and password.");
  CHECK(readLogin(403, "<html>Cloudflare</html>", token, message) == Login::Refused);
  CHECK(message.find("turned the reader away") != std::string::npos);
  CHECK(readLogin(502, "<html>bad gateway</html>", token, message) == Login::Refused);
  CHECK(message.find("did not answer properly") != std::string::npos);
  CHECK(readLogin(0, "", token, message) == Login::Refused);
  CHECK(message.find("Could not reach") != std::string::npos);
  // A 200 that is neither a token nor a next step is a refusal, not a sign-in.
  CHECK(readLogin(200, "{}", token, message) == Login::Refused);
  CHECK(token.empty());
}

void testToken() {
  std::string user;
  int64_t expires = -1;
  CHECK(readToken(kJwt, user, expires));
  CHECK(user == "u_1234567890");
  CHECK(expires == 1800000000);

  user.clear();
  CHECK(!readToken("opaque-token-without-dots", user, expires));
  CHECK(!readToken("a.b", user, expires));
  CHECK(!readToken("a.b.c.d", user, expires));
  CHECK(!readToken("x.!!!.y", user, expires));
  // {"username":"bblp"}: a JWT, but not naming an account.
  CHECK(!readToken("x.eyJ1c2VybmFtZSI6ImJibHAifQ.y", user, expires));
  CHECK(user.empty());

  CHECK(userFromPreference("{\"uid\":1234567890,\"name\":\"x\"}") == "u_1234567890");
  CHECK(userFromPreference("{\"uid\":\"77\"}") == "u_77");
  CHECK(userFromPreference("{\"name\":\"x\"}").empty());
  CHECK(userFromPreference("{\"uid\":\"12; DROP\"}").empty());
}

void testPrinters() {
  const std::string body =
      "{\"message\":\"success\",\"code\":null,\"devices\":["
      "{\"dev_id\":\"01p00a123456789\",\"name\":\"Workshop P1S\",\"online\":true,\"print_status\":\"RUNNING\","
      "\"dev_model_name\":\"C12\",\"dev_product_name\":\"P1S\",\"dev_access_code\":\"12345678\",\"nested\":{\"x\":[1,2]"
      "}},"
      "{\"dev_id\":\"00M09A987654321\",\"name\":\"Desk \\\"A1\\\" mini\",\"online\":false,"
      "\"dev_model_name\":\"N1\",\"dev_access_code\":\"87654321\"},"
      "{\"name\":\"no serial\"}"
      "]}";
  Printer printers[kMaxPrinters];
  const int n = readPrinters(body, printers, kMaxPrinters);
  CHECK(n == 2);
  CHECK(printers[0].serial == "01P00A123456789");
  CHECK(printers[0].name == "Workshop P1S");
  CHECK(printers[0].model == "P1S");
  CHECK(printers[0].accessCode == "12345678");
  CHECK(printers[0].online);
  CHECK(printers[1].serial == "00M09A987654321");
  CHECK(printers[1].name == "Desk \"A1\" mini");
  CHECK(printers[1].model == "N1");
  CHECK(!printers[1].online);

  CHECK(readPrinters(body, printers, 1) == 1);
  CHECK(readPrinters("{\"devices\":[]}", printers, kMaxPrinters) == 0);
  CHECK(readPrinters("{\"message\":\"x\"}", printers, kMaxPrinters) == 0);
  CHECK(readPrinters("{\"devices\":[{\"dev_id\":\"abc\"", printers, kMaxPrinters) == 1);
}

void testWords() {
  CHECK(untilPhrase(0) == "Signed in");
  CHECK(untilPhrase(1800000000) == "Signed in until 15 Jan 2027");
}

}  // namespace

int main() {
  testAccount();
  testBodies();
  testJsonValue();
  testLogin();
  testToken();
  testPrinters();
  testWords();
  std::printf("bambu cloud: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
