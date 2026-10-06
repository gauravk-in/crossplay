// Host tests for Google Tasks: the card's two files, the merge a sync runs,
// and when the charger's poll fires.
//
// Every failure here is silent on the device. A cache that misreads drops a
// task with nothing to say so; a merge that forgets a pending tick un-ticks
// something the reader did; a poll that misjudges its interval either hammers
// Google every loop pass or never syncs at all.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/gtasks/GTasksCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

using gtasks::Task;

Task task(const char* id, const char* title, const char* position, const char* parent = "", bool pending = false) {
  Task t;
  t.id = id;
  t.title = title;
  t.position = position;
  t.parent = parent;
  t.pending = pending;
  return t;
}

void testCredentialsRoundTrip() {
  gtasks::Credentials c;
  c.refreshToken = "1//0g-xyz";
  c.account = "gaurav@example.com";
  const gtasks::Credentials back = gtasks::parseCredentials(gtasks::serializeCredentials(c));
  CHECK(back.complete());
  CHECK_EQ(back.refreshToken, c.refreshToken);
  CHECK_EQ(back.account, c.account);
}

void testCredentialsToleratesAHandEditedFile() {
  // Saved on Windows, with a comment and spaces round the '='.
  const gtasks::Credentials c =
      gtasks::parseCredentials("# copied by hand\r\nrefresh_token = a=b\r\n\r\naccount=me@x.com \r\n");
  // Only the first '=' separates; a token may carry its own.
  CHECK_EQ(c.refreshToken, "a=b");
  CHECK_EQ(c.account, "me@x.com");
  CHECK(c.complete());
}

void testCredentialsWithoutATokenAreIncomplete() {
  CHECK(!gtasks::parseCredentials("account=me@x.com\n").complete());
  CHECK(!gtasks::parseCredentials("").complete());
  CHECK(!gtasks::parseCredentials("garbage without equals\n").complete());
}

void testClientConfig() {
  const gtasks::Client c = gtasks::parseClient(
      "# desktop client\nclient_id=123.apps.googleusercontent.com\n"
      "client_secret = GOCSPX-abc\n");
  CHECK(c.complete());
  CHECK_EQ(c.id, "123.apps.googleusercontent.com");
  CHECK_EQ(c.secret, "GOCSPX-abc");
  CHECK(!gtasks::parseClient("client_id=only\n").complete());
}

void testPkceMatchesTheRfcVector() {
  // RFC 7636 appendix B.
  CHECK_EQ(gtasks::pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
           "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

void testAuthUrlAsksForWhatTheReaderNeeds() {
  const std::string url = gtasks::authUrl("123.apps.googleusercontent.com", "CHAL", "ST8");
  CHECK(url.rfind("https://accounts.google.com/o/oauth2/v2/auth?", 0) == 0);
  CHECK(url.find("client_id=123.apps.googleusercontent.com") != std::string::npos);
  CHECK(url.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A1") != std::string::npos);
  CHECK(url.find("https%3A%2F%2Fwww.googleapis.com%2Fauth%2Ftasks") != std::string::npos);
  CHECK(url.find("access_type=offline") != std::string::npos);
  CHECK(url.find("prompt=consent") != std::string::npos);
  CHECK(url.find("code_challenge=CHAL&code_challenge_method=S256") != std::string::npos);
  CHECK(url.find("state=ST8") != std::string::npos);
}

void testPastedAddresses() {
  const std::string scope = "&scope=email%20https://www.googleapis.com/auth/tasks%20openid";
  // The whole address, as a phone's address bar copies it.
  gtasks::Pasted p = gtasks::parsePasted("http://127.0.0.1:1/?state=ST8&code=4/0Ab%2Bc" + scope, "ST8");
  CHECK(p.ok());
  CHECK_EQ(p.code, "4/0Ab+c");
  // With spaces around it, and only the query.
  p = gtasks::parsePasted("  state=ST8&code=xyz" + scope + " \n", "ST8");
  CHECK_EQ(p.code, "xyz");
  // No scope at all is not a refusal: older answers leave it out.
  CHECK(gtasks::parsePasted("http://127.0.0.1:1/?code=abc&state=ST8", "ST8").ok());
  // Another sign-in's address.
  p = gtasks::parsePasted("http://127.0.0.1:1/?state=OTHER&code=abc", "ST8");
  CHECK(!p.ok() && p.message.find("different") != std::string::npos);
  // The person said no on the consent screen.
  p = gtasks::parsePasted("http://127.0.0.1:1/?error=access_denied&state=ST8", "ST8");
  CHECK(!p.ok() && p.message.find("told no") != std::string::npos);
  // Tasks unticked.
  p = gtasks::parsePasted("http://127.0.0.1:1/?state=ST8&code=abc&scope=email%20openid", "ST8");
  CHECK(!p.ok() && p.message.find("Tasks") != std::string::npos);
  // Not an address at all.
  p = gtasks::parsePasted("hello", "ST8");
  CHECK(!p.ok() && !p.message.empty());
}

void testIdTokenEmail() {
  // {"aud":"x","email_verified":true,"email":"me@x.com","sub":"1"}
  const std::string token =
      "eyJhbGciOiJSUzI1NiJ9."
      "eyJhdWQiOiJ4IiwiZW1haWxfdmVyaWZpZWQiOnRydWUsImVtYWlsIjoibWVAeC5jb20iLCJzdWIiOiIxIn0.sig";
  CHECK_EQ(gtasks::idTokenEmail(token), "me@x.com");
  CHECK_EQ(gtasks::idTokenEmail("not-a-token"), "");
  CHECK_EQ(gtasks::idTokenEmail(""), "");
}

void testCacheRoundTrip() {
  std::vector<Task> in = {task("a", "Buy milk", "001"), task("b", "Call\tmum\nback", "002", "a", true)};
  in[0].due = "2026-10-07";
  const std::vector<Task> out = gtasks::parseTasks(gtasks::serializeTasks(in));
  CHECK(out.size() == 2);
  if (out.size() != 2) return;
  CHECK_EQ(out[0].id, "a");
  CHECK_EQ(out[0].due, "2026-10-07");
  CHECK(!out[0].pending);
  // Separators in a title become spaces rather than new fields or new rows.
  CHECK_EQ(out[1].title, "Call mum back");
  CHECK_EQ(out[1].parent, "a");
  CHECK(out[1].pending);
}

void testCacheDropsDamageNotTheFile() {
  const std::string text = std::string("gtasks 1\n") + "a\t0\t001\t\t\tOne\n" + "half a row\n" +
                           "\t0\t002\t\t\tNo id\n" + "c\t7\t003\t\t\tBad flag\n" +
                           "d\t1\t004\t\t\tFour";  // no final newline
  const std::vector<Task> out = gtasks::parseTasks(text);
  CHECK(out.size() == 2);
  if (out.size() == 2) {
    CHECK_EQ(out[0].id, "a");
    CHECK_EQ(out[1].title, "Four");
  }
}

void testACacheFromSomebodyElseIsEmpty() {
  CHECK(gtasks::parseTasks("gtasks 2\na\t0\t001\t\t\tOne\n").empty());
  CHECK(gtasks::parseTasks("").empty());
}

void testSortPutsChildrenUnderTheirParent() {
  std::vector<Task> t = {task("c1", "child of b", "000", "b"), task("a", "A", "002"), task("b", "B", "001"),
                         task("c2", "orphan", "003", "gone")};
  gtasks::sortForDisplay(t);
  CHECK(t.size() == 4);
  if (t.size() != 4) return;
  CHECK_EQ(t[0].id, "b");
  CHECK_EQ(t[1].id, "c1");
  CHECK_EQ(t[2].id, "a");
  // A child whose parent is not listed is drawn as a parent, in its place.
  CHECK_EQ(t[3].id, "c2");
  CHECK(gtasks::isChild(t[1], t));
  CHECK(!gtasks::isChild(t[3], t));
}

void testMergeKeepsAPendingTickThatHasNotGoneUp() {
  const std::vector<Task> local = {task("a", "A", "001", "", true), task("b", "B", "002")};
  const std::vector<Task> fresh = {task("a", "A renamed", "001"), task("b", "B", "002"), task("n", "New", "003")};
  const std::vector<Task> out = gtasks::merge(local, fresh);
  CHECK(out.size() == 3);
  if (out.size() != 3) return;
  CHECK(out[0].pending);
  CHECK_EQ(out[0].title, "A renamed");
  CHECK(!out[1].pending);
  CHECK_EQ(out[2].id, "n");
  CHECK(gtasks::pendingCount(out) == 1);
}

void testMergeDropsATickOnATaskGoogleNoLongerHas() {
  const std::vector<Task> local = {task("a", "A", "001", "", true)};
  const std::vector<Task> out = gtasks::merge(local, {task("b", "B", "001")});
  CHECK(out.size() == 1);
  CHECK(gtasks::pendingIds(out).empty());
}

void testDueLabels() {
  CHECK_EQ(gtasks::dueDate("2026-10-07T00:00:00.000Z"), "2026-10-07");
  CHECK_EQ(gtasks::dueDate("soon"), "");
  CHECK_EQ(gtasks::dueLabel("2026-10-07"), "DUE 7 OCT");
  CHECK_EQ(gtasks::dueLabel("2026-01-31"), "DUE 31 JAN");
  CHECK_EQ(gtasks::dueLabel("2026-13-01"), "");
  CHECK_EQ(gtasks::dueLabel("2026-1x-01"), "");
  CHECK_EQ(gtasks::dueLabel(""), "");
}

void testSettingsDefaultIsOneMinute() {
  CHECK(gtasks::parseSettings("").pollMinutes == 1);
  CHECK(gtasks::parseSettings("poll_minutes=15\n").pollMinutes == 15);
  CHECK(gtasks::parseSettings("poll_minutes=0\n").pollMinutes == 0);
  // Not a choice the screen offers, so not a value the reader will poll at.
  CHECK(gtasks::parseSettings("poll_minutes=7\n").pollMinutes == 1);
  CHECK(gtasks::parseSettings("poll_minutes=-5\n").pollMinutes == 1);
  CHECK(gtasks::parseSettings(gtasks::serializeSettings(gtasks::Settings{30})).pollMinutes == 30);
}

void testPollChoicesCycleThroughOff() {
  uint16_t m = 1;
  std::string seen;
  for (int i = 0; i < 8; ++i) {
    seen += gtasks::pollLabel(m) + ",";
    m = gtasks::nextPollMinutes(m);
  }
  CHECK_EQ(seen, "EVERY MIN,EVERY 2 MIN,EVERY 5 MIN,EVERY 10 MIN,EVERY 15 MIN,EVERY 30 MIN,EVERY HOUR,OFF,");
  CHECK(m == 1);
  CHECK(gtasks::nextPollMinutes(7) == 1);
}

void testPollOnlyOnTheCharger() {
  CHECK(!gtasks::pollDue(false, 1, false, 0, 0));
  CHECK(!gtasks::pollDue(true, 0, false, 0, 0));
  CHECK(gtasks::pollDue(true, 1, false, 5, 0));
  CHECK(!gtasks::pollDue(true, 1, true, 59999, 0));
  CHECK(gtasks::pollDue(true, 1, true, 60000, 0));
  CHECK(!gtasks::pollDue(true, 5, true, 200000, 0));
  // millis() wraps after 49 days; the interval must survive it.
  CHECK(gtasks::pollDue(true, 1, true, 30000u, 0xFFFFFFFFu - 40000u));
  CHECK(!gtasks::pollDue(true, 1, true, 10000u, 0xFFFFFFFFu - 40000u));
}

void testFormEncodingAndIds() {
  CHECK_EQ(gtasks::formEncode("1//0g-a_b.c~"), "1%2F%2F0g-a_b.c~");
  CHECK_EQ(gtasks::formEncode("a b+c&d=e"), "a%20b%2Bc%26d%3De");
  CHECK(gtasks::safeId("MTIzNDU2Nzg5_-"));
  CHECK(!gtasks::safeId(""));
  CHECK(!gtasks::safeId("../lists"));
  CHECK(!gtasks::safeId("a b"));
}

}  // namespace

// Every list, in Google's order, survives the card; a list whose id could
// spell a path is dropped rather than written.
void testListsRoundTrip() {
  std::vector<gtasks::TaskList> lists(3);
  lists[0].id = "MDk4NjM1";
  lists[0].title = "My Tasks";
  lists[0].open = 7;
  lists[1].id = "../evil";
  lists[1].title = "Nope";
  lists[2].id = "Z3JvY2Vy";
  lists[2].title = "Groceries\tand\nthings";
  const std::vector<gtasks::TaskList> back = gtasks::parseLists(gtasks::serializeLists(lists));
  CHECK(back.size() == 2);
  CHECK_EQ(back[0].id, "MDk4NjM1");
  CHECK_EQ(back[0].title, "My Tasks");
  CHECK(back[0].open == 7);
  CHECK_EQ(back[1].title, "Groceries and things");
  CHECK(gtasks::parseLists("gtasks 1\nabc\t1\tx\n").empty());
  CHECK(gtasks::parseLists("").empty());
}

void testAsleepRoundTrip() {
  gtasks::Asleep a;
  a.listId = "Z3JvY2Vy";
  a.previousMode = 3;
  a.previousQuick = 1;
  gtasks::Asleep back;
  CHECK(gtasks::parseAsleep(gtasks::serializeAsleep(a), back));
  CHECK_EQ(back.listId, "Z3JvY2Vy");
  CHECK(back.previousMode == 3);
  CHECK(back.previousQuick == 1);
  // Nothing recorded is -1, and a file with no usable list is no choice.
  CHECK(gtasks::parseAsleep("list=abc\n", back));
  CHECK(back.previousMode == -1 && back.previousQuick == -1);
  CHECK(!gtasks::parseAsleep("list=../x\n", back));
  CHECK(!gtasks::parseAsleep("", back));
}

int main() {
  testCredentialsRoundTrip();
  testCredentialsToleratesAHandEditedFile();
  testCredentialsWithoutATokenAreIncomplete();
  testClientConfig();
  testPkceMatchesTheRfcVector();
  testAuthUrlAsksForWhatTheReaderNeeds();
  testPastedAddresses();
  testIdTokenEmail();
  testCacheRoundTrip();
  testCacheDropsDamageNotTheFile();
  testACacheFromSomebodyElseIsEmpty();
  testSortPutsChildrenUnderTheirParent();
  testMergeKeepsAPendingTickThatHasNotGoneUp();
  testMergeDropsATickOnATaskGoogleNoLongerHas();
  testDueLabels();
  testSettingsDefaultIsOneMinute();
  testPollChoicesCycleThroughOff();
  testPollOnlyOnTheCharger();
  testFormEncodingAndIds();
  testListsRoundTrip();
  testAsleepRoundTrip();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
