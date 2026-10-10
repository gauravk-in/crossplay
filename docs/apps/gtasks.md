# Tasks (Google Tasks)

Every Google Tasks list on the reader, one on screen at a time. Tick things off
with a tap; the ticks go up to Google on the next sync. Any list can also be the
picture the reader shows while it is off.

## Signing in

Sign-in happens on the reader itself. There is no server in between: your
Google key is kept in `auth.cfg` on the card and goes only to Google.

1. Open **Apps > TASKS** and tap **START SIGN-IN**. The reader joins the saved
   Wi-Fi network (or opens the Wi-Fi picker) and shows a QR code and an
   address.
2. Scan the QR with a phone on the same Wi-Fi. The page that opens is served
   by the reader. Tap **Sign in with Google**, sign in, and allow access to
   Google Tasks and Calendar.
3. Google then sends the phone to a `127.0.0.1` page that will not load. That
   is expected. Copy the whole address from the address bar, go back to the
   reader's page, paste it and tap **Send to the reader**.
4. The page says who you signed in as, and the reader fetches your lists.

The same sign-in serves [Calendar](gcal.md): signing in from either app
signs in both, and signing out of either signs out both. A sign-in made before
Calendar existed lacks its permission; Calendar says so and asks for a new one.

Google's own device sign-in (a code typed at google.com/device) does not allow
the Tasks permission, which is why the reader runs the installed-app sign-in
instead (PKCE, with a loopback redirect the phone cannot load). The pasted
address only works with the sign-in the reader started, and only once.

### The Google client

The reader signs in as a Google Cloud OAuth client that you make once:

1. In the Google Cloud console, create a project and enable the
   **Google Tasks API** and the **Google Calendar API**.
2. Under **Google Auth Platform**, set up the consent screen (External), and
   add the scopes `openid`, `email`, `.../auth/tasks` and
   `.../auth/calendar.readonly`.
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
- **The menu button**, left of the list's name, opens **LISTS**: every list on
  the account with how many tasks are open in it. Tap one to show it. The
  reader opens on the list you looked at last.
- **REFRESH** joins the saved Wi-Fi network (or opens the Wi-Fi picker when
  there is none), sends your ticks from every list, and reads every list again.
  A list deleted on Google disappears from the card on the next sync.
- **On the charger**, with the app open, the reader checks Google by itself
  every minute and keeps the screen awake. The band reads **AUTO SYNC**, or
  **OFFLINE** when a check could not get through. The panel repaints only when
  the list actually changed. Off the charger nothing polls and the reader
  sleeps as usual.
- **The gear** opens settings:
  - **SHOW**: **ALL**, or **DUE TODAY**, which keeps tasks due today, overdue
    tasks, and tasks with no due date. A parent stays on screen when one of its
    subtasks does. "Today" comes from the reader's clock; on a reader whose
    clock has never been set, every task shows. The sleep screen follows this
    setting too, as of the moment the reader went to sleep.
  - **AUTO SYNC**: how often to check on the charger (every 1, 2, 5, 10, 15,
    30 or 60 minutes, or off).
  - **SLEEP SCREEN**: tap it to put the list on screen on the panel while the
    reader is off. It reads **THIS LIST**, the name of another list that is
    already there, or **OFF**. Tapping it on the list that is already there
    turns it off and puts back the sleep screen you had before. It is also
    **Tasks** under the reader's own Sleep Screen setting, and choosing that
    with no list picked shows the default sleep screen. The sleep screen
    shows the list as last synced, without buttons; the reader does not go
    online to draw it.
  - **SIGN OUT** removes the token and every list from the card, puts back
    your old sleep screen, and tells Google to revoke the grant when Wi-Fi is
    up.
- The side keys page a long list.

Completed tasks are not shown. Tasks with no due date come first, in Google's
order, then dated tasks from the soonest due. Subtasks are indented under their
parent and sorted the same way among themselves.

## On the card

`/.crosspoint/gtasks/`:

| File           | What                                                               |
| -------------- | ------------------------------------------------------------------ |
| `auth.cfg`     | Google's refresh token for this reader, and the account's address  |
| `client.cfg`   | the Google OAuth client (see above), unless it is built in         |
| `lists.tsv`    | every list's id, title and open count as last synced               |
| `list-<id>.tsv`| one list's tasks as last synced, plus ticks not yet sent           |
| `settings.cfg` | `poll_minutes=N`, `today_only=0\|1`                                |
| `meta.cfg`     | the list on screen and when the lists last synced                  |
| `asleep.cfg`   | the list on the sleep screen, and the sleep settings it replaced   |
| `.roots.pem`   | optional: CA roots that override the built-in bundle               |

The connections to Google are verified TLS against the roots the other bridges
use (GTS Root R1 and R4 are in it). The sign-in page the reader serves on your
Wi-Fi is plain HTTP and only up while the QR is on the screen; it hands out the
consent address and takes one paste, and serves nothing from the card.
