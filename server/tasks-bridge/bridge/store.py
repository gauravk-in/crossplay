"""Accounts and paired readers, in one JSON file.

    <TASKS_DATA>/state.json
        accounts: uid -> {email_enc, refresh_enc, created}
        devices:  sha256(device token) -> {uid, created}

uid is a hex digest of Google's stable account id (`sub`), never the address.
The refresh token and the address are Fernet-encrypted at rest. That buys
nothing against a live compromise of this box (the key is in the env); it
exists so a backup or a stray copy of the data directory carries no usable
credential. Device tokens are kept only as hashes.

One file rather than a directory per user because nothing here is bigger than
a few hundred bytes per account, and one file is one atomic replace.
"""

import hashlib
import json
import os
import pathlib
import tempfile
import threading
import time

from cryptography.fernet import Fernet

_lock = threading.Lock()


def data_root() -> pathlib.Path:
    root = os.environ.get("TASKS_DATA")
    if not root:
        raise RuntimeError("TASKS_DATA is unset; refusing to guess a data directory")
    return pathlib.Path(root)


def fernet() -> Fernet:
    key = os.environ.get("TASKS_FERNET_KEY")
    if not key:
        # Refusing to serve is the safe failure: starting with refresh tokens
        # in plaintext because an env var was forgotten is not.
        raise RuntimeError("TASKS_FERNET_KEY is unset; refusing to store secrets")
    return Fernet(key.encode())


def uid_for(sub: str) -> str:
    return hashlib.sha256(("google:" + sub).encode()).hexdigest()[:16]


def _path() -> pathlib.Path:
    return data_root() / "state.json"


def _load() -> dict:
    try:
        state = json.loads(_path().read_text())
    except FileNotFoundError:
        state = {}
    state.setdefault("accounts", {})
    state.setdefault("devices", {})
    return state


def _save(state: dict) -> None:
    root = data_root()
    root.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=root, prefix=".state-", suffix=".json")
    with os.fdopen(fd, "w") as f:
        json.dump(state, f, indent=1, sort_keys=True)
    os.replace(tmp, _path())


def save_account(uid: str, email: str, refresh_token: str | None) -> None:
    """A sign-in. Google sends a refresh token only with consent, and the
    sign-in always asks for consent, but an absent one keeps the old token
    rather than erasing a working account."""
    f = fernet()
    with _lock:
        state = _load()
        account = state["accounts"].setdefault(uid, {"created": int(time.time())})
        account["email_enc"] = f.encrypt(email.encode()).decode()
        if refresh_token:
            account["refresh_enc"] = f.encrypt(refresh_token.encode()).decode()
        _save(state)


def add_device(uid: str, th: str) -> None:
    with _lock:
        state = _load()
        state["devices"][th] = {"uid": uid, "created": int(time.time())}
        _save(state)


def uid_for_token_hash(th: str) -> str | None:
    with _lock:
        device = _load()["devices"].get(th)
    return device["uid"] if device else None


def refresh_token(uid: str) -> str | None:
    with _lock:
        account = _load()["accounts"].get(uid)
    if not account or not account.get("refresh_enc"):
        return None
    return fernet().decrypt(account["refresh_enc"].encode()).decode()


def email(uid: str) -> str:
    with _lock:
        account = _load()["accounts"].get(uid)
    if not account or not account.get("email_enc"):
        return ""
    return fernet().decrypt(account["email_enc"].encode()).decode()


def remove_device(th: str) -> str | None:
    """Unpair one reader. -> the refresh token to revoke with Google when that
    was the account's last reader (the account is deleted with it), else None."""
    f = fernet()
    with _lock:
        state = _load()
        device = state["devices"].pop(th, None)
        if device is None:
            return None
        uid = device["uid"]
        token = None
        if not any(d["uid"] == uid for d in state["devices"].values()):
            account = state["accounts"].pop(uid, None)
            if account and account.get("refresh_enc"):
                token = f.decrypt(account["refresh_enc"].encode()).decode()
        _save(state)
    return token


def forget_account(uid: str) -> None:
    """Google refused the refresh token: the account and every reader paired
    to it are dead, and keeping them would only repeat the refusal."""
    with _lock:
        state = _load()
        state["accounts"].pop(uid, None)
        state["devices"] = {k: v for k, v in state["devices"].items() if v["uid"] != uid}
        _save(state)


def counts() -> tuple[int, int]:
    with _lock:
        state = _load()
    return len(state["accounts"]), len(state["devices"])
