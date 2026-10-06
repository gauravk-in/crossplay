# Workouts

Schedules are written on a phone; sets are ticked on the reader.

## Using it

**Apps > Workouts.** The first time, tap **SET UP ON YOUR PHONE**. The reader
joins Wi-Fi if it needs to, starts a small web server and shows a QR code.
Scan it, and the page that opens is the editor: one card per schedule (Upper
Body, Push, Legs...), a mark for each picked from twelve, and under it the
exercises with a set count each. **Save to the reader** writes the plan, and
the reader's screen says **SAVED FROM YOUR PHONE**. Tap **DONE** and the
schedules are there.

Tap a schedule to train it. Each exercise is a row with a box per set; tapping
anywhere on the row ticks the next box. A full exercise ignores further taps
rather than wrapping to zero, and **UNDO** takes back the last tick made since
the schedule was opened. The band says how many of the schedule's sets are
done.

The opening screen keeps the last seven days at the foot, ending today (the
heavier frame). A day you trained is a black square carrying the mark of the
schedule you trained; with two schedules on one day it carries the later one.

A new day starts a fresh session on its own. There is no reset.

## The files

All three are plain text in `/workouts` on the card, so they can be read or
fixed on a computer.

| File        | What                                          | Written                              |
| ----------- | --------------------------------------------- | ------------------------------------ |
| `plan.txt`  | The schedules                                 | By the phone page                    |
| `today.txt` | Sets done today, per schedule, keyed by title | On every tick                        |
| `log.txt`   | One line per schedule trained per day         | On a schedule's first tick of a day  |

A plan reads like this, a schedule line and then its exercises:

```
= Upper Body | arms
Bench press | 4
Pull-ups | 3
```

and the log like this, day number (days since 1970-01-01), mark, title:

```
20732|arms|Upper Body
```

The rules (limits, what an unknown mark draws, how a day rolls over) are in
`src/apps_local/workouts/WorkoutsCore.h` and tested by `host-tests/workouts`.

## The clock

The week needs a date. A reader whose clock was never set says so in the strip
instead of drawing 1970, and does not log workouts. Every save from the phone
page carries the phone's time, and the reader adopts it when its own clock is
unset, so setting up the schedules also sets the date.
