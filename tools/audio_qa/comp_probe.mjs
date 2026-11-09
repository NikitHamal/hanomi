import { chromium } from 'playwright';
const browser = await chromium.launch(); const page = await browser.newPage(); await page.goto('http://localhost:8765/download.html');
const res = await page.evaluate(async () => {
  const out = [];
  for (const db of [-60,-50,-40,-30,-24,-20,-18,-15,-12,-9,-6,-3,0]) {
    const ctx = new OfflineAudioContext(1, 44100*2, 44100); const o = ctx.createOscillator(); o.frequency.value = 1000; const g = ctx.createGain(); g.gain.value = Math.pow(10, db/20);
    const c = ctx.createDynamicsCompressor(); c.threshold.value=-18; c.knee.value=12; c.ratio.value=3; c.attack.value=0.01; c.release.value=0.25;
    o.connect(g); g.connect(c); c.connect(ctx.destination); o.start(); const b = await ctx.startRendering(); const d = b.getChannelData(0);
    let s=0; for (let i=44100*1.5;i<44100*2;i++) s+=d[i]*d[i]; const rms=Math.sqrt(s/(44100*0.5)); const inr=Math.pow(10,db/20)/Math.SQRT2;
    out.push([db, (20*Math.log10(rms/inr)).toFixed(2)]);
  }
  // burst response: 1kHz at -12dB for 60ms after silence; report peak out vs in
  const ctx = new OfflineAudioContext(1, 44100, 44100); const o = ctx.createOscillator(); o.frequency.value=1000; const g=ctx.createGain(); g.gain.setValueAtTime(0,0); g.gain.setValueAtTime(0.25,0.1); g.gain.setValueAtTime(0,0.16);
  const c = ctx.createDynamicsCompressor(); c.threshold.value=-18; c.knee.value=12; c.ratio.value=3; c.attack.value=0.01; c.release.value=0.25; o.connect(g); g.connect(c); c.connect(ctx.destination); o.start(); const b = await ctx.startRendering(); const d=b.getChannelData(0);
  const env=[]; for (let k=0;k<30;k++){ let pk=0; for(let i=4410+k*88;i<4410+(k+1)*88;i++) pk=Math.max(pk,Math.abs(d[i])); env.push(pk.toFixed(3)); }
  return { steady: out, burst: env.join(' '), lateNote: 'burst peaks per 2ms starting at 100ms (in=0.25)' };
});
console.log(JSON.stringify(res)); await browser.close();
