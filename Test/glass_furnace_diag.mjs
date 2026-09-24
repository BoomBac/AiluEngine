// 诊断：分瓣统计白炉检验的权重，定位 60°/80° 的能量损失来自哪里。
const PI = Math.PI;
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => { const l = len(a); return l > 1e-12 ? mul(a, 1 / l) : [0, 0, 0]; };
const sat = v => Math.min(Math.max(v, 0), 1);
const f_schlick = (f0, x) => f0 + (1 - f0) * Math.pow(1 - sat(x), 5);
const n_f0 = (ii, it) => { const r = (it - ii) / Math.max(ii + it, 1e-4); return r * r; };
function d_gtr2(a, nh) { const a2 = a * a, den = 1 + (a2 - 1) * nh * nh; return a2 / (PI * den * den); }
const g_schlick = c => c / (c * 0.5 + 0.5);
const g_smith = (nol, nov) => g_schlick(nol) * g_schlick(nov);
const pdf_mfn = (a, nh) => d_gtr2(Math.max(a, 1e-4), nh) * nh;
const rsp = (f0, vh) => sat(f_schlick(f0, Math.max(vh, 1e-4)));
let rng = 24680;
const rnd = () => { rng ^= rng << 13; rng ^= rng >>> 17; rng ^= rng << 5; return ((rng >>> 0) % 1000000) / 1000000; };
function sGGX(a, u1, u2) { const c = Math.sqrt((1 - u1) / (1 + (a * a - 1) * u1)); const s = Math.sqrt(Math.max(0, 1 - c * c)), p = 2 * PI * u2; return [s * Math.cos(p), s * Math.sin(p), c]; }
function refract(wi, n, eta) { const ci = -dot(wi, n), k = 1 - eta * eta * (1 - ci * ci); if (k < 0) return null; return norm(add(mul(wi, eta), mul(n, eta * ci - Math.sqrt(k)))); }
const IOR = 1.5918, ROUGH = 0.04, F0 = n_f0(1, IOR);
const alpha = Math.max(ROUGH * ROUGH, 1e-6);

function lobeTrans(n, wo, wi, h, ii, it, f0) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return null;
  const nh = sat(Math.abs(dot(n, h)));
  const wih = dot(wi, h), woh = dot(wo, h);
  const den = ii * wih + it * woh;
  if (Math.abs(den) <= 1e-6) return null;
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0, vh);
  const r2 = (it * it) / (ii * ii);
  return { f: (1 - F) * D * G * Math.abs(woh) / Math.max(nl * nv, 1e-6) * r2 / (den * den),
           pdf: pdf_mfn(alpha, nh) * Math.abs(wih) / (den * den) * r2 };
}
function lobeRefl(n, wo, wi, h, f0) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return null;
  const nh = sat(Math.abs(dot(n, h)));
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0, vh);
  return { f: D * G * F / Math.max(4 * nl * nv, 1e-6), pdf: pdf_mfn(alpha, nh) / Math.max(4 * vh, 1e-6) };
}

console.log('theta  样本数    反射: 数量/平均权重/期望平均pR   折射: 数量/平均权重/期望平均(1-pR)   总E   丢弃');
for (const deg of [0, 30, 60, 80]) {
  const th = deg * PI / 180, n = [0, 0, 1], wo = [Math.sin(th), 0, Math.cos(th)];
  let nR = 0, sR = 0, pRsum = 0, nT = 0, sT = 0, pTsum = 0, dropped = 0;
  const M = 400000;
  for (let i = 0; i < M; i++) {
    const r1 = rnd(), r2 = rnd();
    let h = sGGX(alpha, r1, r2); if (dot(h, n) < 0) h = mul(h, -1);
    const pR = rsp(F0, Math.abs(dot(wo, h)));
    if (r2 < pR) {
      const wi = add(mul(h, 2 * dot(wo, h)), mul(wo, -1));
      if (dot(wi, n) <= 0) { dropped++; continue; }
      const e = lobeRefl(n, wo, wi, h, F0);
      if (!e || e.pdf <= 1e-12) { dropped++; continue; }
      const w = e.f * pR * Math.abs(dot(n, wi)) / (e.pdf * pR);
      nR++; sR += w; pRsum += pR;
    } else {
      const rf = refract(mul(wo, -1), h, 1 / IOR);
      if (!rf) { dropped++; continue; }
      const e = lobeTrans(n, wo, rf, h, 1.0, IOR, F0);
      if (!e || e.pdf <= 1e-12) { dropped++; continue; }
      const w = e.f * (1 - pR) * Math.abs(dot(n, rf)) / (e.pdf * (1 - pR));
      nT++; sT += w; pTsum += (1 - pR);
    }
  }
  const total = (sR + sT) / M;
  console.log(`${String(deg).padEnd(6)} ${M}    ${String(nR).padStart(7)} / ${(sR / Math.max(nR, 1)).toFixed(4)} / ${(pRsum / M).toFixed(4)}      ${String(nT).padStart(7)} / ${(sT / Math.max(nT, 1)).toFixed(4)} / ${(pTsum / M).toFixed(4)}      ${total.toFixed(5)}  ${dropped}`);
}
console.log('');
console.log('判据：总 E 应等于 1。');
console.log('反射样本权重应恒为 1；折射样本权重应恒为 (1-F)，两者按各自数量加权后求和 = 1。');
