#!/usr/bin/env python3
"""The whole HTTP surface, end to end, against a fake Google.

The bridge runs in-process over an ASGI transport, and Google is an
httpx.MockTransport that checks what a real token endpoint checks: the client
secret, the redirect URI, and the PKCE verifier against the challenge the
bridge sent the browser off with. So the pairing handshake, the sign-in and
the token refresh are all exercised without a network or a Google project.

Covers: pairing start/poll/expiry, CSRF on /pair, a code that is not open, a
callback that did not start here, the full sign-in, the unticked-Tasks and
not-invited refusals, device auth, token refresh, Google revoking the grant,
unpair and abandon revoking it here, and nothing secret stored in plaintext.

Run: .venv/bin/python tests/test_api.py
"""

import asyncio
import base64
import hashlib
import json
import os
import pathlib
import re
import sys
import tempfile
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

DATA = tempfile.mkdtemp(prefix="tasks-bridge-test-")
os.environ["TASKS_DATA"] = DATA
from cryptography.fernet import Fernet  # noqa: E402

os.environ["TASKS_FERNET_KEY"] = Fernet.generate_key().decode()
os.environ["GOOGLE_CLIENT_ID"] = "client-123.apps.googleusercontent.com"
os.environ["GOOGLE_CLIENT_SECRET"] = "shh-client-secret"
os.environ["TASKS_PUBLIC_URL"] = "https://tasks.example.test"
os.environ["TASKS_ALLOWLIST"] = "gaurav@example.com, other@example.com"

import httpx  # noqa: E402

from bridge import app as appmod  # noqa: E402
from bridge import google  # noqa: E402

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


class FakeGoogle:
    """Google's token and revoke endpoints, as far as this service uses them."""

    def __init__(self):
        self.grants = {}  # auth code -> {challenge, redirect, who, scope}
        self.live = {}  # refresh token -> sub
        self.revoked = []
        self.refreshes = 0
        self.down = False

    def authorize(self, auth_url, who, scope=google.SCOPES):
        """What the consent screen does: approve, and send the browser back
        with a code bound to the request's challenge."""
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(auth_url).query))
        code = "auth-" + str(len(self.grants))
        self.grants[code] = {
            "challenge": q["code_challenge"],
            "redirect": q["redirect_uri"],
            "who": who,
            "scope": scope,
            "client": q["client_id"],
            "offline": q.get("access_type") == "offline",
        }
        return code, q["state"]

    @staticmethod
    def id_token(claims):
        enc = lambda d: base64.urlsafe_b64encode(json.dumps(d).encode()).rstrip(b"=").decode()  # noqa: E731
        return enc({"alg": "RS256"}) + "." + enc(claims) + ".sig"

    def handle(self, request):
        if self.down:
            raise httpx.ConnectError("down", request=request)
        form = dict(urllib.parse.parse_qsl(request.content.decode()))
        if request.url.path == "/revoke":
            self.revoked.append(form.get("token"))
            return httpx.Response(200, json={})
        if form.get("client_id") != os.environ["GOOGLE_CLIENT_ID"] or form.get("client_secret") != os.environ[
            "GOOGLE_CLIENT_SECRET"
        ]:
            return httpx.Response(401, json={"error": "invalid_client"})
        if form.get("grant_type") == "authorization_code":
            grant = self.grants.pop(form.get("code"), None)
            if grant is None or form.get("redirect_uri") != grant["redirect"]:
                return httpx.Response(400, json={"error": "invalid_grant"})
            digest = base64.urlsafe_b64encode(hashlib.sha256(form["code_verifier"].encode()).digest()).rstrip(b"=")
            if digest.decode() != grant["challenge"]:
                return httpx.Response(400, json={"error": "invalid_grant", "error_description": "bad verifier"})
            sub, email = grant["who"]
            refresh = "refresh-" + sub + "-" + str(len(self.live))
            self.live[refresh] = sub
            body = {
                "access_token": "first-access",
                "expires_in": 3599,
                "scope": grant["scope"],
                "id_token": self.id_token({"aud": grant["client"], "sub": sub, "email": email, "email_verified": True}),
            }
            if grant["offline"]:
                body["refresh_token"] = refresh
            return httpx.Response(200, json=body)
        if form.get("grant_type") == "refresh_token":
            self.refreshes += 1
            if form.get("refresh_token") not in self.live:
                return httpx.Response(400, json={"error": "invalid_grant"})
            return httpx.Response(200, json={"access_token": f"access-{self.refreshes}", "expires_in": 3599})
        return httpx.Response(400, json={"error": "unsupported_grant_type"})


async def main():
    fake = FakeGoogle()
    google.TRANSPORT = httpx.MockTransport(fake.handle)
    transport = httpx.ASGITransport(app=appmod.app)
    async with httpx.AsyncClient(transport=transport, base_url="https://tasks.example.test") as web:

        async def start_pairing():
            r = await web.post("/api/pair/start")
            ok(r.status_code == 200, "pair/start answers")
            body = r.json()
            ok(re.fullmatch(r"[2-9A-HJ-NP-Z]{8}", body["code"]) is not None, "code is 8 unambiguous characters")
            return body["code"], body["pollToken"]

        async def submit(code, csrf=None):
            if csrf is None:
                page = await web.get("/pair")
                csrf = re.search(r"name=csrf value='([^']+)'", page.text).group(1)
            return await web.post("/pair", data={"code": code, "csrf": csrf})

        async def sign_in(code, who, scope=google.SCOPES):
            r = await submit(code)
            ok(r.status_code == 303 and r.headers["location"].startswith(google.AUTH_URL), "/pair sends to Google")
            auth_code, state = fake.authorize(r.headers["location"], who, scope)
            return await web.get("/oauth/callback", params={"code": auth_code, "state": state})

        # --- health
        r = await web.get("/healthz")
        ok(r.status_code == 200 and r.json()["google"] is True, "healthz reports a configured client")

        # --- pairing, pending
        code, poll = await start_pairing()
        r = await web.get("/api/pair/poll", params={"pollToken": poll})
        ok(r.json() == {"pending": True}, "an unclaimed code polls pending")
        r = await web.get("/api/pair/poll", params={"pollToken": "nope"})
        ok(r.status_code == 410 and "expired" in r.json()["error"], "an unknown poll token is gone, with a sentence")

        # --- /pair refusals
        r = await submit(code, csrf="forged")
        ok(r.status_code == 403, "a /pair post without the page's csrf is refused")
        r = await submit("ZZZZZZZZ")
        ok(r.status_code == 404, "a code nobody asked for is refused before Google")
        r = await web.get("/oauth/callback", params={"code": "x", "state": "y"})
        ok(r.status_code == 400, "a callback that did not start here is refused")

        # --- the auth URL asks for what the reader needs
        r = await submit(code)
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(r.headers["location"]).query))
        ok(q["redirect_uri"] == "https://tasks.example.test/oauth/callback", "redirect is the public callback")
        ok("https://www.googleapis.com/auth/tasks" in q["scope"].split(), "the Tasks scope is asked for")
        ok(q["access_type"] == "offline" and q["prompt"] == "consent", "offline + consent, so a refresh token comes")
        ok(q["code_challenge_method"] == "S256", "PKCE S256")
        ok(code not in r.headers["location"], "the pairing code is not sent to Google")
        state_wrong = q["state"] + "x"
        r = await web.get("/oauth/callback", params={"code": "x", "state": state_wrong})
        ok(r.status_code == 400, "a callback with the wrong state is refused")

        # --- full sign-in
        r = await sign_in(code, ("sub-gaurav", "gaurav@example.com"))
        ok(r.status_code == 200 and "gaurav@example.com" in r.text, "callback confirms and names the account")
        ok("YES, USE IT" in r.text, "the page names the reader's confirm control")
        r = await web.get("/api/pair/poll", params={"pollToken": poll})
        body = r.json()
        ok(body.get("pending") is False and body.get("email") == "gaurav@example.com", "poll delivers the address")
        device = body.get("deviceToken", "")
        ok(len(device) >= 40, "poll delivers a device token")
        r = await web.get("/api/pair/poll", params={"pollToken": poll})
        ok(r.status_code == 410, "a delivered pairing is consumed")

        # --- a claimed code cannot be claimed twice
        r = await submit(code)
        ok(r.status_code == 404, "a used code is gone")

        # --- tokens
        r = await web.post("/api/token")
        ok(r.status_code == 401 and r.json()["error"], "no bearer, no token")
        r = await web.post("/api/token", headers={"authorization": "Bearer not-a-device"})
        ok(r.status_code == 401, "an unknown device gets no token")
        auth = {"authorization": "Bearer " + device}
        r = await web.post("/api/token", headers=auth)
        ok(r.status_code == 200 and r.json()["accessToken"].startswith("access-"), "a paired reader gets a token")
        ok(r.json()["expiresIn"] == 3599, "expiry is passed through")

        # --- nothing in plaintext
        raw = pathlib.Path(DATA, "state.json").read_text()
        ok("gaurav@example.com" not in raw, "the address is not stored in plaintext")
        ok("refresh-sub-gaurav" not in raw, "the refresh token is not stored in plaintext")
        ok(device not in raw, "the device token is stored only as a hash")

        # --- Google down: a sentence, and nothing forgotten
        fake.down = True
        r = await web.post("/api/token", headers=auth)
        ok(r.status_code == 502 and "not answering" in r.json()["error"], "Google down is a retryable sentence")
        fake.down = False
        r = await web.post("/api/token", headers=auth)
        ok(r.status_code == 200, "and the account survived it")

        # --- unticked Tasks on the consent screen
        code2, poll2 = await start_pairing()
        r = await sign_in(code2, ("sub-gaurav", "gaurav@example.com"), scope="openid email")
        ok("Tasks" in r.text and "ticked" in r.text, "an unticked Tasks box is named")
        r = await web.get("/api/pair/poll", params={"pollToken": poll2})
        ok(r.json() == {"pending": True}, "and the code is not claimed")

        # --- not on the allowlist
        r = await sign_in(code2, ("sub-stranger", "stranger@example.com"))
        ok("Not invited" in r.text, "an address outside the allowlist is refused")
        r = await web.get("/api/pair/poll", params={"pollToken": poll2})
        ok(r.json() == {"pending": True}, "and the code is still not claimed")

        # --- a second reader on the same account, then declined on-device
        r = await sign_in(code2, ("sub-gaurav", "gaurav@example.com"))
        second = (await web.get("/api/pair/poll", params={"pollToken": poll2})).json()["deviceToken"]
        revoked_before = len(fake.revoked)
        r = await web.post("/api/pair/abandon", json={"deviceToken": second})
        ok(r.status_code == 200, "abandon answers")
        r = await web.post("/api/token", headers={"authorization": "Bearer " + second})
        ok(r.status_code == 401, "a declined reader's token is dead")
        ok(len(fake.revoked) == revoked_before, "declining one of two readers does not revoke the account's grant")
        r = await web.post("/api/token", headers=auth)
        ok(r.status_code == 200, "the first reader still works")

        # --- abandon a pending code
        code3, poll3 = await start_pairing()
        await web.post("/api/pair/abandon", json={"pollToken": poll3})
        r = await web.get("/api/pair/poll", params={"pollToken": poll3})
        ok(r.status_code == 410, "an abandoned code is gone")
        r = await submit(code3)
        ok(r.status_code == 404, "and cannot be signed in against")
        r = await web.post("/api/pair/abandon", content=b"[1,2]", headers={"content-type": "application/json"})
        ok(r.status_code == 200, "abandon tolerates a body that is not an object")

        # --- unpair: the last reader revokes the grant
        r = await web.post("/api/unpair", headers=auth)
        ok(r.status_code == 200, "unpair answers")
        ok(any(t and t.startswith("refresh-sub-gaurav") for t in fake.revoked), "the last reader's unpair revokes")
        r = await web.post("/api/token", headers=auth)
        ok(r.status_code == 401, "an unpaired reader gets no token")
        r = await web.post("/api/unpair", headers=auth)
        ok(r.status_code == 200, "unpair is idempotent")

        # --- Google revokes the grant from its side
        code4, poll4 = await start_pairing()
        await sign_in(code4, ("sub-other", "other@example.com"))
        dev4 = (await web.get("/api/pair/poll", params={"pollToken": poll4})).json()["deviceToken"]
        fake.live.clear()
        r = await web.post("/api/token", headers={"authorization": "Bearer " + dev4})
        ok(r.status_code == 401 and "signed this reader out" in r.json()["error"], "a revoked grant signs out")
        state = json.loads(pathlib.Path(DATA, "state.json").read_text())
        ok(not state["accounts"] and not state["devices"], "and the dead account is forgotten")

        # --- rate limit on pairing start
        appmod.PAIR_IP.hits.clear()
        statuses = [(await web.post("/api/pair/start")).status_code for _ in range(12)]
        ok(statuses.count(429) == 2, "pair/start is rate limited per address")

    print(f"test_api: {checks} checks, {failures} failed")
    return failures == 0


if __name__ == "__main__":
    sys.exit(0 if asyncio.run(main()) else 1)
