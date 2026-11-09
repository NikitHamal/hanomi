import fs from 'fs';
function rd(p){const b=fs.readFileSync(p);const n=(b.length-44)/4;const L=new Float32Array(n);for(let i=0;i<n;i++)L[i]=(b.readInt16LE(44+i*4)+b.readInt16LE(46+i*4))/65536;return L;}
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}
for(let len=2;len<=n;len<<=1){const a=-2*Math.PI/len,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=len){let cr=1,ci=0;for(let j=0;j<len/2;j++){const ur=re[i+j],ui=im[i+j],vr=re[i+j+len/2]*cr-im[i+j+len/2]*ci,vi=re[i+j+len/2]*ci+im[i+j+len/2]*cr;re[i+j]=ur+vr;im[i+j]=ui+vi;re[i+j+len/2]=ur-vr;im[i+j+len/2]=ui-vi;const t=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=t;}}}}
const edges=[0,100,200,400,800,1600,3200,6400,22050];
function bands(x){const N=2048;const acc=new Float64Array(edges.length-1);let cnt=0;for(let s=0;s+N<=x.length;s+=N){const re=new Float64Array(N),im=new Float64Array(N);for(let i=0;i<N;i++)re[i]=x[s+i]*(0.5-0.5*Math.cos(2*Math.PI*i/N));fft(re,im);for(let k=1;k<N/2;k++){const f=k*44100/N;for(let b=0;b<acc.length;b++)if(f>=edges[b]&&f<edges[b+1]){acc[b]+=re[k]*re[k]+im[k]*im[k];break;}}cnt++;}return acc.map(v=>v/Math.max(1,cnt));}
const [,, a, b]=process.argv;const A=bands(rd(a)),B=bands(rd(b));
console.log(process.argv[4]||'', edges.slice(0,-1).map((e,i)=>`${e}:${(10*Math.log10((B[i]+1e-12)/(A[i]+1e-12))).toFixed(1)}`).join(' '));
