// Protokol serial: [0xA5][type][len lo][len hi][payload...][crc]
// crc = XOR dari type, len lo, len hi, dan seluruh payload (sama dengan firmware).

export const CMD = {
  HELLO: 0x01, LAYOUT: 0x02, LIVE: 0x03, SAVE: 0x04, PLAY: 0x05, LIST: 0x06,
  DELETE: 0x07, PRESET: 0x08, WELCOME: 0x09, OFF: 0x0a, BRIGHT: 0x0b, GET: 0x0c,
};
export const RSP = { STATUS: 0x81, LIST: 0x82, ANIM: 0x83, MSG: 0x8e };

const enc = new TextEncoder();
const dec = new TextDecoder();

export function frame(type, payload = new Uint8Array(0)) {
  const n = payload.length;
  const out = new Uint8Array(4 + n + 1);
  out[0] = 0xa5;
  out[1] = type;
  out[2] = n & 0xff;
  out[3] = n >> 8;
  let crc = 0;
  for (let i = 1; i < 4; i++) crc ^= out[i];
  out.set(payload, 4);
  for (let i = 0; i < n; i++) crc ^= payload[i];
  out[4 + n] = crc;
  return out;
}

export function textPayload(s) { return enc.encode(s); }
export function nameFrame(type, name) { return frame(type, enc.encode(name)); }
export function saveFrame(name, animBytes) {
  const nb = enc.encode(name);
  const p = new Uint8Array(1 + nb.length + animBytes.length);
  p[0] = nb.length;
  p.set(nb, 1);
  p.set(animBytes, 1 + nb.length);
  return frame(CMD.SAVE, p);
}

// Parser streaming. Panggil feed(chunk) untuk setiap potongan data dari serial.
export class FrameParser {
  constructor(onFrame) {
    this.onFrame = onFrame;
    this.reset();
  }
  reset() { this.state = 0; this.type = 0; this.len = 0; this.crc = 0; this.buf = []; }
  feed(bytes) {
    for (const b of bytes) {
      switch (this.state) {
        case 0: if (b === 0xa5) this.state = 1; break;
        case 1: this.type = b; this.crc = b; this.state = 2; break;
        case 2: this.len = b; this.crc ^= b; this.state = 3; break;
        case 3:
          this.len |= b << 8; this.crc ^= b; this.buf = [];
          this.state = this.len ? 4 : 5;
          break;
        case 4:
          this.buf.push(b); this.crc ^= b;
          if (this.buf.length >= this.len) this.state = 5;
          break;
        case 5:
          if (b === this.crc) this.onFrame(this.type, Uint8Array.from(this.buf));
          this.state = 0;
          break;
      }
    }
  }
}

export function text(payload) { return dec.decode(payload); }
