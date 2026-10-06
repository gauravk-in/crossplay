"""The HTTP surface: two pages for a person and five endpoints for a reader.

Person (browser): /pair takes the code off the reader and sends them to
Google; /oauth/callback catches Google's answer, stores the refresh token and
claims the code. The code, the OAuth state and the PKCE verifier ride a
Fernet-sealed, HttpOnly, SameSite=Lax cookie between the two, so a callback
that did not start from this browser's own /pair is refused.

Reader (bearer device token): pairing start/poll/abandon, /api/token for a
short-lived Google access token, and /api/unpair. Every refusal is
{"error": sentence}, and the firmware shows the sentence verbatim.

The pages and the pairing handshake follow server/read-bridge; the difference
is that nothing is synced through here. The reader takes the access token and
talks to tasks.googleapis.com itself.
"""

import base64
import hashlib
import html
import json
import logging
import os
import pathlib
import secrets

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse

from . import chrome, google, pairing, store
from .ratelimit import Window

log = logging.getLogger("bridge.app")

app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)

PAIR_IP = Window(10, 300)
# Typing codes into /pair. Eight characters from a 32-glyph alphabet is not
# guessable inside five minutes; guessing for free is the problem.
CLAIM_IP = Window(20, 300)
# A reader asks for an access token about once an hour, and once per sync when
# it has been asleep. Generous for that, ruinous for a loop.
TOKEN_DEVICE = Window(30, 300)
GLOBAL_TOKEN = Window(240, 60)

FLOW_COOKIE = "tasks_flow"
CSRF_COOKIE = "tasks_csrf"
FLOW_TTL_S = 600


def client_ip(request: Request) -> str:
    return request.headers.get("cf-connecting-ip") or (request.client.host if request.client else "?")


def public_url() -> str:
    return os.environ.get("TASKS_PUBLIC_URL", "").rstrip("/")


def redirect_uri() -> str:
    return public_url() + "/oauth/callback"


def allowed(email: str) -> bool:
    """TASKS_ALLOWLIST: comma-separated addresses, or "*". Unset is closed to
    everyone, because a Google client is someone's quota and someone's name
    on the consent screen."""
    raw = os.environ.get("TASKS_ALLOWLIST", "").strip()
    if not raw:
        return False
    names = {e.strip().lower() for e in raw.split(",") if e.strip()}
    return "*" in names or email.strip().lower() in names


def error(message: str, status: int) -> JSONResponse:
    return JSONResponse({"error": message}, status_code=status)


def _seal(data: dict) -> str:
    return store.fernet().encrypt(json.dumps(data).encode()).decode()


def _unseal(value: str | None) -> dict | None:
    if not value:
        return None
    try:
        return json.loads(store.fernet().decrypt(value.encode(), ttl=FLOW_TTL_S))
    except Exception:
        return None


def device_of(request: Request) -> tuple[str, str] | None:
    """-> (uid, token_hash), or None when the bearer token is not a paired
    reader."""
    auth = request.headers.get("authorization", "")
    if not auth.startswith("Bearer "):
        return None
    th = pairing.token_hash(auth[7:].strip())
    uid = store.uid_for_token_hash(th)
    return (uid, th) if uid else None


def build_id() -> str:
    try:
        return (pathlib.Path(__file__).with_name("BUILD").read_text().strip() or "unknown")[:64]
    except OSError:
        return "unknown"


@app.get("/healthz")
async def healthz():
    return {"ok": True, "build": build_id(), "google": google.configured()}


_ASSETS = {"jersey25.woff2", "instrumentserif.woff2"}


@app.get("/assets/{name}")
async def asset(name: str):
    if name not in _ASSETS:
        return error("not found", 404)
    return FileResponse(
        pathlib.Path(__file__).parent / "static" / name,
        media_type="font/woff2",
        headers={"cache-control": "public, max-age=31536000, immutable"},
    )


# ------------------------------------------------------------------ the pages
def outcome(title: str, ok: bool, sentence: str, *, again: bool = True, status: int = 200) -> HTMLResponse:
    body = chrome.mark(ok) + f"<h1>{html.escape(title)}</h1><p class=lede>{html.escape(sentence)}</p>"
    if again:
        body += '<a class=btn href="/pair">Type the code again</a>'
    response = chrome.page(title, body)
    response.status_code = status
    return response


# The reader's QR puts the code in the URL fragment, which never reaches this
# server or its logs; the page lifts it into the field.
FILL_CODE = (
    "<script>try{const c=location.hash.slice(1).toUpperCase();"
    "if(/^[A-Z0-9]{8}$/.test(c))document.getElementById('code').value=c}catch(e){}</script>"
)


@app.get("/")
async def home():
    return RedirectResponse("/pair")


@app.get("/pair")
async def pair_page(request: Request):
    csrf = request.cookies.get(CSRF_COOKIE) or secrets.token_urlsafe(24)
    response = chrome.page(
        "Pair a reader",
        "<h1>Your tasks, on the reader</h1>"
        "<p class=lede>Open TASKS on the reader and press GET A CODE. Type the code"
        " it shows, then sign in with the Google account whose tasks you want.</p>"
        + chrome.reader_with_code()
        + "<form method=post action=/pair>"
        f"<input type=hidden name=csrf value='{html.escape(csrf)}'>"
        "<label for=code>The code on the reader</label>"
        "<input id=code class=code name=code maxlength=8 autocomplete=off"
        " autocapitalize=characters spellcheck=false required>"
        "<button>Sign in with Google</button></form>"
        "<p class=small>Only type a code shown on a reader in your own hands."
        " The reader will show which account it got and ask before it keeps it.</p>"
        + FILL_CODE,
        step=1,
    )
    response.set_cookie(CSRF_COOKIE, csrf, max_age=3600, httponly=True, secure=True, samesite="lax")
    return response


@app.post("/pair")
async def pair_submit(request: Request):
    if not CLAIM_IP.allow(client_ip(request)):
        return outcome("Slow down", False, "Too many codes from here. Wait a few minutes.", status=429)
    form = await request.form()
    csrf = request.cookies.get(CSRF_COOKIE)
    if not csrf or not secrets.compare_digest(str(form.get("csrf", "")), csrf):
        return outcome("Start again", False, "This form expired. Load the page again.", status=403)
    if not google.configured() or not public_url():
        return outcome("Not set up", False, "This service has no Google client yet.", again=False, status=503)
    code = pairing.normalise(str(form.get("code", "")))
    if not pairing.PAIRINGS.open(code):
        return outcome(
            "That code is gone", False, "Codes last five minutes and work once. Ask the reader for a fresh one.",
            status=404,
        )
    state = secrets.token_urlsafe(24)
    verifier = secrets.token_urlsafe(64)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    response = RedirectResponse(google.auth_url(redirect_uri(), state, challenge), status_code=303)
    response.set_cookie(
        FLOW_COOKIE,
        _seal({"code": code, "state": state, "verifier": verifier}),
        max_age=FLOW_TTL_S,
        httponly=True,
        secure=True,
        samesite="lax",
        path="/oauth",
    )
    return response


@app.get("/oauth/callback")
async def oauth_callback(request: Request):
    flow = _unseal(request.cookies.get(FLOW_COOKIE))
    q = request.query_params
    if not flow or not secrets.compare_digest(q.get("state", ""), flow["state"]):
        return outcome("Start again", False, "This sign-in did not start here, or took too long.", status=400)
    if q.get("error"):
        return outcome("Not signed in", False, "Google did not sign you in, so nothing changed.")
    try:
        who = await google.exchange(q.get("code", ""), flow["verifier"], redirect_uri())
    except google.Refused as e:
        log.info("code exchange refused: %s", e)
        return outcome("Not signed in", False, "Google refused the sign-in. Try again.", status=400)
    except google.Unavailable as e:
        log.warning("code exchange failed: %s", e)
        return outcome("Not signed in", False, "Google is not answering. Try again in a few minutes.", status=502)

    if google.SCOPES.split()[-1] not in who["scope"].split():
        # Google's consent screen lets a person untick a permission. Without
        # this one the reader would pair and then fail every sync.
        return outcome("Tasks was not ticked", False, "Sign in again and leave the Google Tasks box ticked.")
    if not who["email"] or not allowed(who["email"]):
        log.info("sign-in refused for an address outside TASKS_ALLOWLIST")
        return outcome("Not invited", False, "This service only signs in the accounts it was set up for.")
    if not who["refresh_token"]:
        return outcome("Not signed in", False, "Google sent no lasting key. Try again.")

    uid = store.uid_for(who["sub"])
    store.save_account(uid, who["email"], who["refresh_token"])
    if pairing.PAIRINGS.claim(flow["code"], uid, who["email"]) is None:
        return outcome("That code is gone", False, "The code ran out while you signed in. Ask the reader for a fresh one.")
    log.info("account %s: signed in, code claimed", uid)

    response = chrome.page(
        "Confirm on the reader",
        "<h1>Now confirm on the reader</h1>"
        f"<p class=lede>It shows {html.escape(who['email'])}. Tap YES, USE IT there and"
        " your tasks arrive.</p>" + chrome.confirm_on_reader() + chrome.waiting(),
        step=3,
    )
    response.delete_cookie(FLOW_COOKIE, path="/oauth")
    return response


# -------------------------------------------------------------- device side
@app.post("/api/pair/start")
async def pair_start(request: Request):
    if not PAIR_IP.allow(client_ip(request)):
        return error("Too many codes asked for. Wait a few minutes.", 429)
    if not google.configured():
        return error("The sign-in service has no Google client yet.", 503)
    return pairing.PAIRINGS.start()


@app.get("/api/pair/poll")
async def pair_poll(pollToken: str = ""):
    result = pairing.PAIRINGS.poll(pollToken)
    if result is None:
        return error("That code expired. Ask for a fresh one.", 410)
    if result["pending"]:
        return {"pending": True}
    # Registered on delivery, not on claim: a code nobody polls for again never
    # becomes a live token.
    store.add_device(result["uid"], pairing.token_hash(result["deviceToken"]))
    return {"pending": False, "email": result["email"], "deviceToken": result["deviceToken"]}


@app.post("/api/pair/abandon")
async def pair_abandon(request: Request):
    """The reader walked away: from the code (pollToken), or from an account
    its confirm screen declined (deviceToken)."""
    try:
        body = await request.json()
    except ValueError:
        body = {}
    if not isinstance(body, dict):
        body = {}
    if body.get("pollToken"):
        pairing.PAIRINGS.abandon(str(body["pollToken"]))
    if body.get("deviceToken"):
        revoke = store.remove_device(pairing.token_hash(str(body["deviceToken"])))
        if revoke:
            await google.revoke(revoke)
    return {"ok": True}


@app.post("/api/token")
async def access_token(request: Request):
    device = device_of(request)
    if device is None:
        return error("This reader is not signed in anymore. Sign in again.", 401)
    uid, th = device
    if not TOKEN_DEVICE.allow(th) or not GLOBAL_TOKEN.allow("all"):
        return error("Too many requests. Try again in a few minutes.", 429)
    refresh = store.refresh_token(uid)
    if refresh is None:
        store.forget_account(uid)
        return error("This reader is not signed in anymore. Sign in again.", 401)
    try:
        token, expires_in = await google.refresh(refresh)
    except google.Refused as e:
        log.info("account %s: refresh refused (%s); forgetting it", uid, e)
        store.forget_account(uid)
        return error("Google signed this reader out. Sign in again.", 401)
    except google.Unavailable as e:
        log.warning("account %s: refresh failed: %s", uid, e)
        return error("Google is not answering. Try again in a few minutes.", 502)
    return {"accessToken": token, "expiresIn": expires_in}


@app.post("/api/unpair")
async def unpair(request: Request):
    """Sign-out from the reader. Revokes the Google grant when this was the
    account's last reader. Idempotent: an unknown token is already unpaired."""
    device = device_of(request)
    if device is not None:
        revoke = store.remove_device(device[1])
        if revoke:
            await google.revoke(revoke)
    return {"ok": True}


@app.on_event("startup")
async def startup():
    store.fernet()  # refuses to start without a key
    if not google.configured() or not public_url():
        log.error("GOOGLE_CLIENT_ID/GOOGLE_CLIENT_SECRET/TASKS_PUBLIC_URL unset: sign-in is refused until they are")
    if not os.environ.get("TASKS_ALLOWLIST", "").strip():
        log.error("TASKS_ALLOWLIST is empty: every Google account will be refused")
    accounts, devices = store.counts()
    log.info("tasks bridge up: %d accounts, %d readers", accounts, devices)
