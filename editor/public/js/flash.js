// Flash firmware ESP32 langsung dari browser memakai esptool-js (Web Serial).
// Urutan file mengikuti tata letak default PlatformIO/Arduino ESP32:
//   ESP32-C3: bootloader.bin @0x0, partitions.bin @0x8000, boot_app0.bin @0xe000, firmware.bin @0x10000
//   (ESP32 klasik: bootloader @0x1000)

// PERLU DIVERIFIKASI: versi esptool-js. Ganti URL ini ke versi yang sudah ada di npm
// (lihat https://www.npmjs.com/package/esptool-js) dan pastikan modul ESM-nya bisa di-import.
export const ESPTOOL_URL = 'https://cdn.jsdelivr.net/npm/esptool-js@0.4.3/bundle.js';

const PARTS = [
  ['bootloader.bin', 0x0],
  ['partitions.bin', 0x8000],
  ['boot_app0.bin', 0xe000],
  ['firmware.bin', 0x10000],
];

function toBinaryString(u8) {
  let s = '';
  const CH = 0x8000;
  for (let i = 0; i < u8.length; i += CH) {
    s += String.fromCharCode.apply(null, u8.subarray(i, i + CH));
  }
  return s;
}

export async function flashFirmware(port, { log, progress }) {
  const mod = await import(ESPTOOL_URL);
  const { ESPLoader, Transport } = mod;
  const transport = new Transport(port, true);
  const terminal = {
    clean() {},
    writeLine(d) { log(d); },
    write(d) { log(d); },
  };
  const loader = new ESPLoader({ transport, baudrate: 921600, romBaudrate: 115200, terminal });

  log('Menghubungkan ke ESP32 (tahan tombol BOOT bila gagal)…');
  await loader.main();

  const fileArray = [];
  for (const [name, address] of PARTS) {
    const res = await fetch('/firmware/' + name);
    if (!res.ok) throw new Error('File ' + name + ' tidak ditemukan di /firmware/');
    const u8 = new Uint8Array(await res.arrayBuffer());
    fileArray.push({ data: toBinaryString(u8), address });
  }

  await loader.writeFlash({
    fileArray,
    flashSize: 'keep',
    flashMode: 'keep',
    flashFreq: 'keep',
    eraseAll: false,
    compress: true,
    reportProgress: (fileIndex, written, total) => {
      progress(Math.round((written / total) * 100), fileIndex);
    },
  });

  log('Flash selesai, perangkat di-reset…');
  try { await loader.hardReset(); } catch (e) {}
  try { await transport.disconnect(); } catch (e) {}
}
