# Tasks (Google Tasks)

Your default Google Tasks list on the reader. Tick things off with a tap; the
ticks go up to Google on the next sync.

## Signing in

Sign-in happens on the reader itself. There is no server in between: your
Google key is kept in `auth.cfg` on the card and goes only to Google.

1. Open **Apps > TASKS** and tap **START SIGN-IN**. The reader joins the saved
   Wi-Fi network (or opens the Wi-Fi picker) and shows a QR code and an
   address.
2. Scan the QR with a phone on the same Wi-Fi. The page that opens is served
   by the reader. Tap **Sign in with Google**, sign in, and allow access to
   Google Tasks.
3. Google then sends the phone to a `127.0.0.1` page that will not load. That
   is expected. Copy the whole address from the address bar, go back to the
   reader's page, paste it and tap **Send to the reader**.
4. The page says who you signed in as, and the reader fetches your list.

Google's own device sign-in (a code typed at google.com/device) does not allow
the Tasks permission, which is why the reader runs the installed-app sign-in
instead (PKCE, with a loopback redirect the phone cannot load). The pasted
address only works with the sign-in the reader started, and only once.

### The Google client

The reader signs in as a Google Cloud OAuth client that you make once:

1. In the Google Cloud console, create a project and enable the
   **Google Tasks API**.
2. Under **Google Auth Platform**, set up the consent screen (External), and
   add the scopes `openid`, `email` and `.../auth/tasks`.
3. Create an OAuth client of type **Desktop app**.
4. Put its ID and secret on the card in `/.crosspoint/gtasks/client.cfg`:

   ```
   client_id=1234-abc.apps.googleusercontent.com
   client_secret=GOCSPX-...
   ```

   Or build them in with `-DGTASKS_CLIENT_ID='"..."'` and
   `-DGTASKS_CLIENT_SECRET='"..."'` in `platformio.local.ini`. The card file
   wins when both are there.

Google treats a desktop client's secret as public, so it is not what keeps an
account safe; the refresh token on each card is. Do not commit either to the
repository all the same.

While the consent screen is in **Testing**, only the test users you list can
sign in, and Google expires their sign-in after seven days, so the reader asks
again every week. Publishing the app to **In production** removes both limits.
Google shows an "unverified app" warning for the Tasks scope until the app is
verified, which a personal client can click through.

## Using it

- **Tap a row** to tick it. The row is struck through and the band says how
  many ticks are waiting to go up. Tap it again before a sync to take the tick
  back.
- **REFRESH** joins the saved Wi-Fi network (or opens the Wi-Fi picker when
  there is none), sends your ticks, and reads the list again.
- **On the charger**, with the app open, the reader checks Google by itself
  every minute and keeps the screen awake. The band reads **AUTO SYNC**, or
  **OFFLINE** when a check could not get through. The panel repaints only when
  the list actually changed. Off the charger nothing polls and the reader
  sleeps as usual.
- **The gear** opens settings: how often to check on the charger (every 1, 2,
  5, 10, 15, 30 or 60 minutes, or off), and **SIGN OUT**, which removes the
  token and the list from the card, and tells Google to revoke the grant when
  Wi-Fi is up.
- The side keys page a long list.

Completed tasks are not shown. Subtasks are indented under their parent.

## On the card

`/.crosspoint/gtasks/`:

| File           | What                                                               |
| -------------- | ------------------------------------------------------------------ |
| `auth.cfg`     | Google's refresh token for this reader, and the account's address  |
| `client.cfg`   | the Google OAuth client (see above), unless it is built in         |
| `tasks.tsv`    | the list as last synced, plus ticks not yet sent                   |
| `settings.cfg` | `poll_minutes=N`                                                   |
| `meta.cfg`     | the list's title and when it last synced                           |
| `.roots.pem`   | optional: CA roots that override the built-in bundle               |

The connections to Google are verified TLS against the roots the other bridges
use (GTS Root R1 and R4 are in it). The sign-in page the reader serves on your
Wi-Fi is plain HTTP and only up while the QR is on the screen; it hands out the
consent address and takes one paste, and serves nothing from the card.
