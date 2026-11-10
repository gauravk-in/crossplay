# Bambu

A Bambu Lab printer's progress and AMS on the reader: what is printing, how far
along it is, the time left, the temperatures, and for every AMS slot the
filament type, its colour, how much is left, and the unit's humidity. It reads
only; it never sends the printer a command.

Code in `src/apps_local/bambu/`, host suite in `host-tests/bambu/`.

## How it reaches the printer

The printer runs an MQTT broker on the LAN, the one Bambu Studio and Home
Assistant use in LAN mode:

```
TLS to <printer ip>:8883     user "bblp", password = LAN access code
SUBSCRIBE device/<serial>/report
PUBLISH   device/<serial>/request  {"pushing":{"command":"pushall",...}}
```

- After the pushall the X1 sends full reports about once a second; the P1 and
  A1 send only the fields that changed. So the state is merged, never replaced:
  a report with three keys changes three fields (`ReportParser`).
- Reports are streamed into a SAX parser (`freeink-sdk/libs/network/JsonSax`)
  as the TLS records arrive, so a 30KB pushall never needs a 30KB buffer. The
  largest buffer is one 2KB read chunk.
- The MQTT client is the few packets a read-only client needs (CONNECT,
  SUBSCRIBE, PUBLISH, PINGREQ), in `BambuCore` where the host suite covers
  them, over the SDK's wolfSSL `SecureClient`, which falls back to TLS 1.2.
- The printer's certificate is signed by Bambu's own CA and names the serial,
  not the address, so it is not verified: the same trust Bambu Studio gives a
  printer in LAN mode. The access code is what keeps others off.

### Why not Bambu's cloud MQTT

`us.mqtt.bambulab.com` would work away from home, but it needs the Bambu
account's email and password plus an emailed code on the reader, and the token
it hands back expires after about three months. The packets are the same, so
it can be added as a second way in later; it is not here because the LAN way
needs no account at all.

### Firmware caveats

Since Bambu's January 2025 authorization firmware, *control* over LAN needs
Developer Mode. Reading the report topic still works for most printers with LAN
mode on; some P1 owners report needing LAN-only mode for MQTT to answer at all.
The access code changes whenever LAN mode is switched off and on.

## Setting it up

The phone page, the TRMNL pattern: the header's phone icon (or SET UP ON PHONE
on first run) serves `/bambu` from the reader and shows a QR code for it. It
asks for the IP address, the serial and the access code, and writes
`/.crosspoint/bambu/config.txt`:

```
host=192.168.1.42
serial=01P00A123456789
access_code=12345678
keep_awake=1
```

The file can be written by hand too. Every value is trimmed and capped, and the
serial is upper-cased.

## The screen

Layout C ("spools") won over a column-of-swatches layout and a spreadsheet of
rows, because it is the only one with room for two AMS units and the external
spool at once (`/mnt/project-files/bambu/layout-variants.png` in the project).

- The big number is `mc_percent`; the bar under it is the same number.
- Each AMS is a row of four spools, inked in the nearest of four grays to the
  filament colour, with the nearest everyday colour name under it. The slot
  feeding the nozzle (`tray_now`) has its label inverted.
- Humidity is `humidity_raw` (%RH) where the firmware sends it, else the AMS's
  own 1 to 5 level, printed as a level rather than guessed into a percentage.
- `remain` of -1 (a spool the AMS cannot read) shows as `?`.
- As many AMS units as fit are drawn, spools shrinking to make room, and the
  rest are counted.
- The bar along the bottom is inverted while the data is live. Tapping it, or
  either side key, asks the printer for a pushall again (or reconnects).

## Repainting

The panel takes a second per refresh, and the X1 reports every second. A change
of stage, of the AMS units or of the feeding slot is drawn at once; percent,
layer and temperatures wait at least 20 seconds; temperature changes under a
degree are not changes. A clean refresh every tenth paint clears ghosting.

The last state goes to `/.crosspoint/bambu/last.json` (in the report's own
shape) at most every five minutes and on the way out, so the monitor opens on
it, marked not live, while the radio comes up.

## Not done

- Not tested against a real printer yet; built and host-tested only.
- No sleep-screen source, and no fetching while the reader sleeps.
- HMS error codes are not decoded: a print error shows as `ERROR` beside the
  stage.
- The AMS HT and the H2D's second nozzle are not drawn specially.
