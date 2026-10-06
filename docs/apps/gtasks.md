# Tasks (Google Tasks)

Your default Google Tasks list on the reader. Tick things off with a tap; the
ticks go up to Google on the next sync.

## Signing in

1. Open **Apps > TASKS** and tap **GET A CODE**. The reader joins the saved
   Wi-Fi network (or opens the Wi-Fi picker) and shows a QR code and an
   eight-character code.
2. Scan the QR with your phone, or open the address under the code and type
   it. Sign in with Google and allow access to Google Tasks.
3. The reader shows the Google address it got and asks **IS THIS YOU?**. Tap
   **YES, USE IT** and your list arrives.

Codes last five minutes and work once. Only type a code shown on a reader in
your own hands.

Google's own device sign-in (a code typed at google.com/device) does not allow
the Tasks permission, which is why the code goes to the sign-in service
instead. The service keeps the Google grant; the reader keeps only a token for
the service and asks it for an hour's access each time it syncs. Task data goes
straight between the reader and Google.

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
  token and the list from the card, and tells the service to drop the Google
  grant when Wi-Fi is up.
- The side keys page a long list.

Completed tasks are not shown. Subtasks are indented under their parent.

## On the card

`/.crosspoint/gtasks/`:

| File           | What                                                               |
| -------------- | ------------------------------------------------------------------ |
| `auth.cfg`     | the sign-in service's token for this reader, and the address       |
| `tasks.tsv`    | the list as last synced, plus ticks not yet sent                   |
| `settings.cfg` | `poll_minutes=N`                                                   |
| `meta.cfg`     | the list's title and when it last synced                           |
| `bridge.cfg`   | optional: `host=` of a sign-in service other than the built-in one |
| `.roots.pem`   | optional: CA roots that override the built-in bundle               |

The connections to Google and to the sign-in service are verified TLS against
the roots the other bridges use (GTS Root R1 and R4 are in it).

## Running the sign-in service

`server/tasks-bridge/README.md`: a Google Cloud web OAuth client with the
Tasks API enabled, HTTPS in front of the container, and an allowlist of the
Google addresses that may sign in. The reader's built-in host is
`GTASKS_BRIDGE_HOST` in `src/apps_local/gtasks/GTasksApi.cpp`.
