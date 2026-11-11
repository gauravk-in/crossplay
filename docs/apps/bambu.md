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

### Away from home: Bambu's cloud

The printer also keeps a connection open to Bambu's own broker and publishes
the same reports there. With a Bambu account signed in, the reader uses it
when the LAN does not answer:

```
TLS to us.mqtt.bambulab.com:8883   user "u_<account number>", password = access token
SUBSCRIBE / PUBLISH as on the LAN
```

- The LAN is tried first when it is set up. After it fails with the cloud to
  fall back on, it is left alone for ten minutes, so a reader away from home
  does not spend ten seconds of every reconnect on it.
- Signing in is on the phone page, and the reader makes the requests itself
  (`BambuCloudApi`): `POST api.bambulab.com/v1/user-service/user/login` with
  the email and password; when Bambu answers `loginType: verifyCode`, it asks
  Bambu to email a code (`/user/sendemail/code`) and the page asks for it. The
  user name comes from the token (a JWT naming `username`), or else from
  `/v1/design-user-service/my/preference`.
- The printers on the account (`/v1/iot-service/api/user/bind`) fill in the
  serial and the LAN access code when the settings have none, so only the IP
  address is left to type for the LAN.
- The token goes to `/.crosspoint/bambu/cloud.txt`, which the phone page never
  reads back; the page is told the email and the expiry. The password is
  passed through to Bambu and not kept. It travels from the phone to the
  reader over the LAN in plain HTTP, like the access code does.
- The API and the broker are verified against `BambuCloudRoots.h`, a bundle of
  the common public roots, because Bambu does not say which CA it uses. A PEM
  bundle at `/.crosspoint/bambu/.roots.pem` wins over it.
- The token lasts about three months. When the broker refuses it, the screen
  says to sign in again.
- None of this is a published API. It is what Bambu's apps and the Home
  Assistant integration use, and Bambu can change it. Accounts with
  authenticator-app sign-in (TFA) are refused with a sentence; China-region
  accounts (`api.bambulab.cn`) are not supported.

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

The file can be written by hand too. With a Bambu account signed in, `serial`
alone is enough. Every value is trimmed and capped, and the
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

- Not tested against a real printer or a real Bambu account yet; built and
  host-tested only.
- No sleep-screen source, and no fetching while the reader sleeps.
- HMS error codes are not decoded: a print error shows as `ERROR` beside the
  stage.
- The AMS HT and the H2D's second nozzle are not drawn specially.
