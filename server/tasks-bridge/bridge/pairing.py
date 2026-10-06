"""Pairing: a reader shows a code, a person signs in to Google against it, the
reader confirms the account it got.

Adapted from server/read-bridge/bridge/pairing.py. Codes are 8 chars from an
unambiguous alphabet (no 0/O/1/I), expire in five minutes and are single-use.
The device token is 32 random bytes and the store keeps only its hash. The
reader shows "is this you?" with the Google address before it keeps the token,
which is what stops a stranger who saw the code from pairing their own account
to someone else's reader.
"""

import hashlib
import secrets
import time

CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"
CODE_TTL_S = 300


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def normalise(code: str) -> str:
    return "".join(c for c in str(code).upper() if c in CODE_ALPHABET)[:8]


class Pairings:
    """In-memory: a restart forgets pending pairings, which costs one more
    code on the reader and nothing else. Paired tokens live in state.json."""

    def __init__(self):
        self._pending: dict[str, dict] = {}

    def _sweep(self):
        now = time.time()
        for code in [c for c, p in self._pending.items() if p["expires"] < now]:
            del self._pending[code]

    def start(self) -> dict:
        self._sweep()
        code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(8))
        poll_token = secrets.token_urlsafe(24)
        self._pending[code] = {
            "poll_token": poll_token,
            "expires": time.time() + CODE_TTL_S,
            "uid": None,
            "email": None,
            "device_token": None,
        }
        return {"code": code, "pollToken": poll_token, "expiresIn": CODE_TTL_S}

    def open(self, code: str) -> bool:
        """True while `code` is waiting for an account. Checked before sending
        anyone to Google, so a typo is answered here and not after a consent
        screen."""
        self._sweep()
        p = self._pending.get(normalise(code))
        return p is not None and p["uid"] is None

    def claim(self, code: str, uid: str, email: str) -> str | None:
        """-> the new device token, or None when the code is gone or taken."""
        self._sweep()
        p = self._pending.get(normalise(code))
        if p is None or p["uid"] is not None:
            return None
        p["uid"] = uid
        p["email"] = email
        p["device_token"] = secrets.token_urlsafe(32)
        return p["device_token"]

    def abandon(self, poll_token: str) -> None:
        self._sweep()
        for code, p in list(self._pending.items()):
            if p["poll_token"] == poll_token:
                del self._pending[code]
                return

    def poll(self, poll_token: str) -> dict | None:
        """None: unknown/expired. {'pending': True}: not yet claimed. Else the
        one-shot result; the pairing is consumed on delivery."""
        self._sweep()
        for code, p in self._pending.items():
            if p["poll_token"] != poll_token:
                continue
            if p["uid"] is None:
                return {"pending": True}
            result = {
                "pending": False,
                "uid": p["uid"],
                "email": p["email"],
                "deviceToken": p["device_token"],
            }
            del self._pending[code]
            return result
        return None


PAIRINGS = Pairings()
