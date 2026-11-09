import { chromium } from 'playwright';
import fs from 'fs';
const modes = process.argv[2].split(','); const secs = +process.argv[3] || 25;
const browser = await chromium.launch(); const page = await browser.newPage(); await page.goto('http://localhost:8765/download.html');
for (const m of modes) {
  const arr = await page.evaluate(async ({ m, secs }) => {
    const OAC = window.OfflineAudioContext; let inst = null; let cb = null;
    const oldSI = window.setInterval; window.setInterval = (f, ms) => { cb = f; return 1; };
    window.AudioContext = class extends OAC { constructor() { super(2, Math.floor(44100 * secs), 44100); inst = this; Object.defineProperty(this, 'state', { get: () => 'running' }); } };
    const M = await import('/js/audio.js?v=' + Math.random()); M.Audio.init(); M.Audio.music(m);
    for (let i = 1; i < secs * 10; i++) inst.suspend(i / 10 + 0.0001).then(() => { cb && cb(); inst.resume(); });
    const buf = await inst.startRendering(); const L = buf.getChannelData(0), R = buf.getChannelData(1); const out = new Int16Array(L.length * 2);
    for (let j = 0; j < L.length; j++) { out[j * 2] = Math.max(-1, Math.min(1, L[j])) * 32767; out[j * 2 + 1] = Math.max(-1, Math.min(1, R[j])) * 32767; }
    return Array.from(out);
  }, { m, secs });
  const v = Int16Array.from(arr); const b = Buffer.from(v.buffer);
  const hdr = Buffer.alloc(44); hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + b.length, 4); hdr.write('WAVEfmt ', 8); hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(2, 22); hdr.writeUInt32LE(44100, 24); hdr.writeUInt32LE(176400, 28); hdr.writeUInt16LE(4, 32); hdr.writeUInt16LE(16, 34); hdr.write('data', 36); hdr.writeUInt32LE(b.length, 40);
  fs.mkdirSync('/tmp/ref', { recursive: true }); fs.writeFileSync(`/tmp/ref/music_${m}.wav`, Buffer.concat([hdr, b]));
  let ss = 0, pk = 0; for (const x of v) { ss += x * x; pk = Math.max(pk, Math.abs(x)); } console.log(m, 'rms=' + (Math.sqrt(ss / v.length) / 32768).toFixed(4), 'peak=' + (pk / 32768).toFixed(3));
}
await browser.close();
