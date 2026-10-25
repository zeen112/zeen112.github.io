// Koneksi USB serial dari browser (Web Serial API). Hanya Chrome/Edge di desktop.
import { FrameParser } from './protocol.js';

export const SERIAL_BAUD = 921600;

export class SerialLink {
  constructor({ onFrame, onClose }) {
    this.onFrame = onFrame;
    this.onClose = onClose;
    this.port = null;
    this.writer = null;
    this.reading = false;
    this.parser = new FrameParser((t, p) => this.onFrame(t, p));
  }

  static supported() { return 'serial' in navigator; }

  // Minta port (harus dari klik pengguna) atau pakai port yang sudah diizinkan sebelumnya.
  async connect(existingPort = null) {
    this.port = existingPort || await navigator.serial.requestPort();
    await this.port.open({ baudRate: SERIAL_BAUD });
    this.writer = this.port.writable.getWriter();
    this.reading = true;
    this.parser.reset();
    this._readLoop();
    return this.port;
  }

  async _readLoop() {
    const reader = this.port.readable.getReader();
    this._reader = reader;
    try {
      while (this.reading) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) this.parser.feed(value);
      }
    } catch (e) {
      // port terputus
    } finally {
      reader.releaseLock();
      if (this.reading) this._closed();
    }
  }

  _closed() {
    this.reading = false;
    this.writer = null;
    this.onClose && this.onClose();
  }

  async send(bytes) {
    if (!this.writer) throw new Error('Belum terhubung ke perangkat');
    await this.writer.write(bytes);
  }

  // Tutup koneksi tetapi biarkan objek port tetap bisa dipakai lagi (mis. untuk flash).
  async disconnect() {
    this.reading = false;
    try { if (this._reader) await this._reader.cancel(); } catch (e) {}
    try { if (this.writer) { this.writer.releaseLock(); this.writer = null; } } catch (e) {}
    try { if (this.port) await this.port.close(); } catch (e) {}
  }
}
