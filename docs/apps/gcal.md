# Calendar (Google Calendar)

Your Google calendars as the phone app's Schedule view: one running list of
days, starting today, with each event under its date. It can also be the
picture the reader shows while it is off.

## Signing in

Calendar uses the same Google sign-in as [Tasks](gtasks.md): signing in from
either app signs in both. Open **Apps > CALENDAR**, tap **START SIGN-IN** and
follow the steps in the Tasks page.

The Google client needs the **Google Calendar API** enabled and the
`.../auth/calendar.readonly` scope on its consent screen. The reader only reads
calendars; it never changes them. A sign-in made before Calendar existed lacks
that permission, and Calendar asks you to sign in again.

## Using it

- The page opens on **today**. The day's number sits in a column on the left,
  on a black disc for today. Each event is a card with its title and, under
  it, its time and place. All-day events are solid bars on top of their day.
  A month's name runs across the page where a new month starts.
- **The side keys**, or **<** and **>** in the footer, page back and forward
  through the days. **TODAY** jumps back to today.
- **REFRESH** joins the saved Wi-Fi network (or opens the Wi-Fi picker) and
  reads every calendar again. The reader keeps two weeks back and three months
  ahead.
- **On the charger**, with the app open, the reader checks Google by itself
  and keeps the screen awake. The band reads **AUTO SYNC**, or **OFFLINE** when
  a check could not get through. Off the charger nothing polls; tap
  **REFRESH**.
- **The gear** opens settings:
  - **AUTO SYNC**: how often to check on the charger (every 1, 2, 5, 10, 15,
    30 or 60 minutes, or off). Five minutes to begin with.
  - **SLEEP SCREEN**: **ON** draws today's schedule, as last synced, on the
    panel while the reader is off. It is also **Calendar** under the reader's
    own Sleep Screen setting. Turning it off puts back the sleep screen you
    had before. The reader does not go online to draw it, and shows the
    default sleep screen when its clock is not set.
  - **SIGN OUT** removes the token and the calendar from the card, signs Tasks
    out too, and tells Google to revoke the grant when Wi-Fi is up.

Every calendar ticked as shown in Google Calendar appears, as on the phone,
up to twelve. Events you declined and cancelled events are left out. Times are
in the reader's time zone.

## On the card

`/.crosspoint/gcal/`:

| File           | What                                                     |
| -------------- | -------------------------------------------------------- |
| `events.tsv`   | every event in the window, as last synced                |
| `settings.cfg` | `poll_minutes=N`                                         |
| `meta.cfg`     | when the calendars last synced, and how many there were  |
| `asleep.cfg`   | the sleep settings the calendar replaced                 |

The token and the Google client are Tasks' files in `/.crosspoint/gtasks/`.
