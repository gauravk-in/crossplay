# Tasks bridge

The sign-in half of the reader's Google Tasks app (`src/apps_local/gtasks/`):
a FastAPI service (uvicorn, port 8080 inside the container,
`bridge.app:app`) that pairs a reader to a Google account and hands it
short-lived access tokens.

Google's device sign-in (a code typed on a phone) does not allow the Tasks
scope, so the reader cannot sign itself in. This service does the web sign-in
for it, the same way the Instapaper bridge pairs a reader:

1. The reader asks `/api/pair/start` for an eight-character code and shows it
   with a QR of `https://<host>/pair#<code>`.
2. On a phone, `/pair` takes the code and sends the person to Google with
   PKCE. Google sends them back to `/oauth/callback`, which stores the refresh
   token (Fernet-encrypted) and claims the code.
3. The reader's poll of `/api/pair/poll` gets the Google address and a device
   token. It shows "IS THIS YOU?" and keeps the token only on YES.
4. From then on the reader posts its device token to `/api/token` for an
   access token, and talks to `tasks.googleapis.com` with it directly. Task
   data never passes through here.

Sign-out on the reader calls `/api/unpair`; when that was the account's last
reader the grant is revoked with Google too. If Google refuses the refresh
token (revoked at myaccount.google.com, or a Testing-mode consent screen's
seven days ran out), `/api/token` answers 401 and the reader asks to sign in
again.

## What it needs to run

- **A Google Cloud OAuth client of type "Web application"**, in a project with
  the **Google Tasks API** enabled. Its authorized redirect URI is
  `<TASKS_PUBLIC_URL>/oauth/callback`. Put the consent screen **In
  production**: in Testing, Google expires every refresh token after seven
  days. An unverified app in production works for the addresses you allow;
  Google shows a warning screen during sign-in that can be clicked through.
- **HTTPS at `TASKS_PUBLIC_URL`.** Google refuses a plain-http redirect for a
  web client. `compose.yaml` publishes on `127.0.0.1:8090` only; put a
  Cloudflare Tunnel or a reverse proxy in front.
- **`TASKS_FERNET_KEY`**: the service refuses to start without one.
- **`TASKS_ALLOWLIST`**: comma-separated Google addresses, or `*`. Empty means
  nobody can sign in, on purpose.

`.env.example` lists them. Copy it to `.env` (mode 600) and
`docker compose up -d --build`.

The reader has the host compiled in (`GTASKS_BRIDGE_HOST`, see
`src/apps_local/gtasks/GTasksApi.cpp`), with a card override at
`/.crosspoint/gtasks/bridge.cfg` (`host=tasks.example.com`).

## Suites

```sh
uv venv .venv && uv pip install --python .venv/bin/python -r requirements.txt
.venv/bin/python tests/test_api.py   # the whole surface against a fake Google
```

The fake Google checks the client secret, the redirect URI and the PKCE
verifier, so the suite proves the sign-in without a Google project.
`scripts_local/check.sh` runs it.
