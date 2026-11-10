// Reads each symbol aztec_dump printed back with the ZXing build the phone page
// ships, and checks it says what was encoded.
'use strict';
const fs = require('fs');
const path = require('path');
const ZXing = require(path.join(__dirname, '../../src/apps_local/wallet/WalletZxing.js'));
// The phone page reads Binary Shift bytes back one character a byte the same way.
ZXing.ZXingStringEncoding.customDecoder = (bytes, name) =>
  name === 'ISO-8859-1' ? Buffer.from(bytes).toString('latin1') : new TextDecoder(name).decode(bytes);
const lines = fs.readFileSync(process.argv[2], 'utf8').trim().split('\n');
let failed = 0, skipped = 0;
for (const line of lines) {
  const [hex, sizeText, modules, layers] = line.split('\t');
  const want = Buffer.from(hex, 'hex');
  if (sizeText === 'FAIL') { console.log('FAIL encode', want.length, 'bytes'); failed++; continue; }
  // Its decoder fails on any symbol past 22 layers, its own included.
  if (Number(layers) > 22) { skipped++; continue; }
  const size = Number(sizeText);
  const px = 4, quiet = 4, side = (size + 2 * quiet) * px;
  const lum = new Uint8ClampedArray(side * side).fill(255);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    if (modules[y * size + x] !== '1') continue;
    for (let dy = 0; dy < px; dy++) for (let dx = 0; dx < px; dx++)
      lum[((y + quiet) * px + dy) * side + (x + quiet) * px + dx] = 0;
  }
  const source = new ZXing.RGBLuminanceSource(lum, side, side);
  const bitmap = new ZXing.BinaryBitmap(new ZXing.HybridBinarizer(source));
  let got = null;
  try {
    const result = new ZXing.AztecCodeReader().decode(bitmap);
    got = Buffer.from(result.getText(), 'latin1');
  } catch (e) { got = null; if (process.env.DEBUG) console.log(e); }
  const ok = got !== null && got.equals(want);
  if (!ok) failed++;
  if (!ok || process.env.VERBOSE) console.log((ok ? 'ok  ' : 'BAD ') + size + 'x' + size + '  ' + JSON.stringify(want.toString('latin1').slice(0, 40)));
}
console.log('ZXing: ' + (failed ? failed + ' failed' : 'all ' + (lines.length - skipped) + ' read back') +
  (skipped ? ', ' + skipped + ' past 22 layers left to zxing-cpp' : ''));
process.exit(failed ? 1 : 0);
