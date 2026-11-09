import { chromium } from 'playwright';
import fs from 'fs';
const names = process.argv[2].split(',');
const secs = +process.argv[3] || 3;
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage();
await page.goto('http://localhost:8765/download.html'); if (process.env.NOCOMP) await page.evaluate(() => { window.__nocomp = true; });
fs.mkdirSync('/tmp/ref', { recursive: true });
for (const nm of names) {
  const arr = await page.evaluate(async ({ nm, secs, i }) => {
    const OAC = window.OfflineAudioContext; let inst = null;
    window.AudioContext = class extends OAC { constructor() { super(2, Math.floor(44100 * (secs + 1)), 44100); inst = this; } };
    if (window.__nocomp) OAC.prototype.createDynamicsCompressor = function(){ const g=this.createGain(); g.threshold={};g.knee={};g.ratio={};g.attack={};g.release={}; return g; };
    const M = await import('/js/audio.js?v=' + i + Math.random());
    M.Audio.init();
    if (nm.startsWith('amb')) { const k = ['rain','wind','night','water'][+nm.slice(3)]; const o = {rain:0,wind:0,night:0,water:0}; o[k] = 1; M.Audio.ambient(o); }
    const p = inst.suspend(1.0).then(() => { if (!nm.startsWith('amb')) M.Audio.play(nm, { vol: 1, pitch: 1, pan: 0 }); inst.resume(); });
    const buf = await inst.startRendering();
    const L = buf.getChannelData(0).subarray(44100), R = buf.getChannelData(1).subarray(44100); const out = new Int16Array(L.length * 2);
    for (let j = 0; j < L.length; j++) { out[j * 2] = Math.max(-1, Math.min(1, L[j])) * 32767; out[j * 2 + 1] = Math.max(-1, Math.min(1, R[j])) * 32767; }
    return Array.from(out);
  }, { nm, secs, i: names.indexOf(nm) });
  const b = Buffer.from(Int16Array.from(arr).buffer);
  const hdr = Buffer.alloc(44); hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + b.length, 4); hdr.write('WAVEfmt ', 8); hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(2, 22); hdr.writeUInt32LE(44100, 24); hdr.writeUInt32LE(176400, 28); hdr.writeUInt16LE(4, 32); hdr.writeUInt16LE(16, 34); hdr.write('data', 36); hdr.writeUInt32LE(b.length, 40);
  fs.writeFileSync(`/tmp/ref/${nm}.wav`, Buffer.concat([hdr, b]));
  let ss = 0, pk = 0; const v = Int16Array.from(arr); for (const x of v) { ss += x * x; pk = Math.max(pk, Math.abs(x)); }
  console.log(nm, 'rms=' + (Math.sqrt(ss / v.length) / 32768).toFixed(4), 'peak=' + (pk / 32768).toFixed(3));
}
await browser.close();
