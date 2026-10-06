"""The three Google OAuth calls this service makes: trade a sign-in for a
refresh token, trade a refresh token for an access token, and revoke.

The client secret lives here and nowhere else. The reader never holds it and
never holds the refresh token either: it holds a device token for this
service, asks /api/token for a short-lived access token, and talks to
tasks.googleapis.com with that directly. So the only Tasks traffic through
this box is the sign-in and the hourly refresh, and a stolen reader is undone
by unpairing it here without touching the Google account.
"""

import base64
import json
import os
import urllib.parse

import httpx

AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth"
TOKEN_URL = "https://oauth2.googleapis.com/token"
REVOKE_URL = "https://oauth2.googleapis.com/revoke"
SCOPES = "openid email https://www.googleapis.com/auth/tasks"

# Tests swap in an httpx.MockTransport; production leaves it None.
TRANSPORT: httpx.AsyncBaseTransport | None = None


class Refused(Exception):
    """Google answered and said no: the grant is dead and signing in again is
    the only way forward."""


class Unavailable(Exception):
    """Google could not be asked, or answered something unexpected. Worth
    trying again later; nothing is deleted on this."""


def client_id() -> str:
    return os.environ.get("GOOGLE_CLIENT_ID", "")


def client_secret() -> str:
    return os.environ.get("GOOGLE_CLIENT_SECRET", "")


def configured() -> bool:
    return bool(client_id() and client_secret())


def _client() -> httpx.AsyncClient:
    return httpx.AsyncClient(transport=TRANSPORT, timeout=20.0)


def auth_url(redirect_uri: str, state: str, challenge: str) -> str:
    return AUTH_URL + "?" + urllib.parse.urlencode(
        {
            "client_id": client_id(),
            "redirect_uri": redirect_uri,
            "response_type": "code",
            "scope": SCOPES,
            # offline + consent is what makes Google hand out a refresh token
            # on every sign-in, not only the first one for this client.
            "access_type": "offline",
            "prompt": "consent",
            "include_granted_scopes": "false",
            "code_challenge": challenge,
            "code_challenge_method": "S256",
            "state": state,
        }
    )


def _claims(id_token: str) -> dict:
    """The id_token's payload, unverified. That is allowed here and only here:
    it came straight from Google's token endpoint over TLS in answer to our own
    code exchange (OpenID Connect Core 3.1.3.7), so the channel is the proof.
    The audience is still checked, which costs nothing."""
    try:
        payload = id_token.split(".")[1]
        payload += "=" * (-len(payload) % 4)
        claims = json.loads(base64.urlsafe_b64decode(payload))
    except (IndexError, ValueError):
        raise Unavailable("Google sent an identity this service could not read.")
    if claims.get("aud") != client_id() or not claims.get("sub"):
        raise Unavailable("Google sent an identity for a different app.")
    return claims


async def exchange(code: str, verifier: str, redirect_uri: str) -> dict:
    """-> {sub, email, refresh_token, scope}."""
    try:
        async with _client() as http:
            r = await http.post(
                TOKEN_URL,
                data={
                    "code": code,
                    "client_id": client_id(),
                    "client_secret": client_secret(),
                    "redirect_uri": redirect_uri,
                    "grant_type": "authorization_code",
                    "code_verifier": verifier,
                },
            )
    except httpx.HTTPError as e:
        raise Unavailable("Google could not be reached.") from e
    try:
        body = r.json()
    except ValueError:
        body = {}
    if r.status_code != 200:
        raise Refused(body.get("error_description") or body.get("error") or f"HTTP {r.status_code}")
    claims = _claims(body.get("id_token", ""))
    email = claims.get("email", "") if claims.get("email_verified", True) else ""
    return {
        "sub": claims["sub"],
        "email": email,
        "refresh_token": body.get("refresh_token"),
        "scope": body.get("scope", ""),
    }


async def refresh(refresh_token: str) -> tuple[str, int]:
    """-> (access_token, expires_in seconds)."""
    try:
        async with _client() as http:
            r = await http.post(
                TOKEN_URL,
                data={
                    "client_id": client_id(),
                    "client_secret": client_secret(),
                    "refresh_token": refresh_token,
                    "grant_type": "refresh_token",
                },
            )
    except httpx.HTTPError as e:
        raise Unavailable("Google could not be reached.") from e
    try:
        body = r.json()
    except ValueError:
        body = {}
    if r.status_code == 200 and body.get("access_token"):
        return body["access_token"], int(body.get("expires_in", 3600))
    # invalid_grant is the refresh token itself (revoked, expired, password
    # changed); invalid_client and unauthorized_client mean this service's own
    # client is gone. All three are permanent for this grant.
    if body.get("error") in ("invalid_grant", "invalid_client", "unauthorized_client"):
        raise Refused(body["error"])
    raise Unavailable(f"HTTP {r.status_code} {body.get('error', '')}".strip())


async def revoke(token: str) -> None:
    """Best effort: a revoke that fails leaves a grant the person can still
    remove at myaccount.google.com/permissions."""
    try:
        async with _client() as http:
            await http.post(REVOKE_URL, data={"token": token})
    except httpx.HTTPError:
        pass
