// Susunan LED: mengubah daftar panjang strip + pola menjadi peta "urutan LED -> sel kanvas".
// Strip dianggap satu rantai LED yang berurutan. Setiap LED menunjuk ke indeks sel (y*W+x),
// atau NO_CELL (0xFFFF) jika LED itu tidak punya sel pada kanvas.

export const NO_CELL = 0xffff;

export const PATTERNS = {
  row_zigzag: 'Baris zigzag (bolak-balik)',
  row_straight: 'Baris lurus',
  col_zigzag: 'Kolom zigzag (bolak-balik)',
  col_straight: 'Kolom lurus',
};

export function chainCells(W, H, pattern, reverse = false) {
  const cells = [];
  if (pattern.startsWith('row')) {
    for (let y = 0; y < H; y++) {
      for (let i = 0; i < W; i++) {
        const x = pattern === 'row_zigzag' && y % 2 === 1 ? W - 1 - i : i;
        cells.push(y * W + x);
      }
    }
  } else {
    for (let x = 0; x < W; x++) {
      for (let i = 0; i < H; i++) {
        const y = pattern === 'col_zigzag' && x % 2 === 1 ? H - 1 - i : i;
        cells.push(y * W + x);
      }
    }
  }
  if (reverse) cells.reverse();
  return cells;
}

// lengths: array panjang tiap strip (LED). Mengembalikan map sepanjang total LED.
export function buildMap(W, H, lengths, pattern, reverse = false) {
  const total = lengths.reduce((a, b) => a + b, 0);
  const cells = chainCells(W, H, pattern, reverse);
  const map = new Array(total);
  for (let k = 0; k < total; k++) map[k] = k < cells.length ? cells[k] : NO_CELL;
  return map;
}

// Payload layout: W(2 LE) H(2 LE) nStrips(1) len[n](2 LE each) map[total](2 LE each)
export function encodeLayout(W, H, lengths, map) {
  const out = new Uint8Array(5 + lengths.length * 2 + map.length * 2);
  const dv = new DataView(out.buffer);
  dv.setUint16(0, W, true);
  dv.setUint16(2, H, true);
  out[4] = lengths.length;
  lengths.forEach((l, i) => dv.setUint16(5 + i * 2, l, true));
  let p = 5 + lengths.length * 2;
  for (const v of map) { dv.setUint16(p, v, true); p += 2; }
  return out;
}
