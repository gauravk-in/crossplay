# Cards

Codes you show at a counter -- a loyalty card, a boarding pass, a link, your
contact details -- kept on the reader, one per screen, at a size a scanner reads.

## Using it

CARDS is on the shelf. It opens on the list of cards by title; tap one to show
it. The card's title is on the band, the code fills the width under it, and the
caption (if it has one) sits beneath. PREV and NEXT at the foot, or the Up and
Down keys, move between cards; Back returns to the list. The bin on the band
deletes the card on screen after a confirm, and KEEP IT covers where the moon
and NEXT were, so a double tap keeps the card.

## On the sleep screen

The moon between PREV and NEXT puts the open card on the sleep screen, so a
boarding pass at the gate is one press of the power button away. It is outlined
while the card is not there and filled while it is; tap it again to take the
card off. Putting a card up sets Settings > Sleep screen to **Card** and turns
off Quick Resume on Timeout (which would skip the sleep screen); taking it off
puts back both settings as they were. It also turns Live off, as a note does.

The card is drawn from its file each time the reader sleeps, with no counter or
buttons and the code centred on the page. The choice is kept in
`/.crosspoint/cards-asleep.txt`: the card's file name, then the sleep screen
mode and Quick Resume setting it replaced. Deleting the card, on the reader or
from the phone page, takes it off the sleep screen; if the file goes missing
some other way, the reader shows the default sleep screen.

## Adding a card

The pencil on the list's band starts a small web server and shows a QR code for
it. On the phone page, give the card a title, choose a screenshot or photo of the
code, and optionally a caption. The page reads the code **on the phone**
(`BarcodeDetector` where the browser has it, otherwise the bundled jsQR), shows
what it says, and sends only that text to the reader. A code that will not read
can be typed in instead. The phone page also lists the cards and can delete them.

Only QR codes are read. Boarding passes that use Aztec or PDF417 codes, and
loyalty cards with a 1D barcode, are not supported yet.

## On the SD card

One file per card in `/cards`, named `0001.txt`, `0002.txt`, ... in the order
they were added:

```
Lidl Plus            <- title
Member since 2021    <- caption, may be an empty line
https://example.com  <- everything after line two is the code's payload
```

The payload is kept verbatim (a contact card's own line ends survive); only
trailing line ends are dropped. The reader redraws the code from the payload at
error correction M (L when only L holds it), so it is sharp at any size.
Payloads are capped at 1200 bytes, which keeps every module at three pixels or
more on the panel.

## Where the code is

| File | What it holds |
| ---- | ------------- |
| `src/apps_local/wallet/WalletCore.*` | File format, upload parsing, QR version choice. Host suite: `host-tests/wallet`. |
| `src/apps_local/wallet/WalletScreens.*` | The list, card, confirm and phone screens. Tested in `host-tests/ui`. |
| `src/apps_local/wallet/WalletStore.*` | SD I/O for `/cards`. |
| `src/apps_local/wallet/WalletServer.*` | The phone page's routes under `/cards`. |
| `src/apps_local/wallet/WalletPage.html`, `WalletJsqr.js` | The phone page, and jsQR 1.4.0 (Apache-2.0, `jsqr-LICENSE`). |

The directory is `wallet/` because `cards/` already holds the playing-card art
the card games share.
