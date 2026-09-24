// 逐行复刻 sampling.hlsli 里「透射率混合」的采样与求值，验证估计量无偏。
//
// 判据：均匀环境下 E[f*|cos|/pdf] 必须等于总反射率（无损表面应当恰好为 1）。
// 0 和 1 正常、中间值过曝，是典型的「采样分布与求值 pdf 不一致」——
// 两者的比例在混合权重上不成对，中间比例偏差最大。
const PI = Math.PI, IOR = 1.5918;
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => { const l = len(a); return l > 1e-12 ? mul(a, 1 / l) : [0, 0, 0]; };
const sat = v => Math.min(Math.max(v, 0), 1);
const f_schlick = (f0, x) => f0 + (1 - f0) * Math.pow(1 - Math.min(Math.max(x, 0), 1), 5);
const n_f0 = (ii, it) => { const r = (it - ii) / Math.max(ii + it, 1e-4); return r * r; };
function d_gtr2(a, nh) { const a2 = a * a, c2 = nh * nh, den = 1 + (a2 - 1) * c2; return a2 / (PI * den * den); }
const g_schlick = c => c / (c * 0.5 + 0.5);
const g_smith = (nol, nov) => g_schlick(nol) * g_schlick(nov);
const pdf_mfn = (a, nh) => d_gtr2(Math.max(a, 1e-4), nh) * nh;
const rsp = (f0, vh) => sat(f_schlick(f0, Math.max(vh, 1e-4)));
// V_SmithGGXCorrelated（brdf.hlsli）
function v_smith_corr(nv, nl, alpha) {
  const a2 = alpha * alpha;
  const lv = nl * Math.sqrt((nv - a2 * nv) * nv + a2);
  const ll = nv * Math.sqrt((nl - a2 * nl) * nl + a2);
  return Math.min(0.5 / (lv + ll), 65504);
}
let rng = 1234567;
const rnd = () => { rng ^= rng << 13; rng ^= rng >>> 17; rng ^= rng << 5; return ((rng >>> 0) % 1000000) / 1000000; };
function sGGX(a, u1, u2) { const c = Math.sqrt((1 - u1) / (1 + (a * a - 1) * u1)); const s = Math.sqrt(Math.max(0, 1 - c * c)), p = 2 * PI * u2; return [s * Math.cos(p), s * Math.sin(p), c]; }
function refract(wi, n, eta) { const ci = -dot(wi, n), k = 1 - eta * eta * (1 - ci * ci); if (k < 0) return null; return norm(add(mul(wi, eta), mul(n, eta * ci - Math.sqrt(k)))); }
// 与 shader 的 build_onb 一致的正交基
function onb(n) {
  const sign = n[2] >= 0 ? 1 : -1;
  const a = -1 / (sign + n[2]);
  const b2 = n[0] * n[1] * a;
  return [[1 + sign * n[0] * n[0] * a, sign * b2, -sign * n[0]], [b2, sign + n[1] * n[1] * a, -n[1]]];
}
function sampleCosHemi(n, r1, r2) {
  const rs = Math.sqrt(r1), phi = 2 * PI * r2;
  const x = rs * Math.cos(phi), y = rs * Math.sin(phi), z = Math.sqrt(Math.max(0, 1 - r1));
  const [t, b] = onb(n);
  return norm(add(add(mul(t, x), mul(b, y)), mul(n, z)));
}
const ROUGH = 0.04, ALBEDO = 1.0, METALLIC = 0.0;
const alpha = Math.max(ROUGH * ROUGH, 1e-6), f0d = 0.04;
function specProb() {
  const dw = ALBEDO * (1 - METALLIC), sw = f0d;
  return Math.min(Math.max(sw / Math.max(dw + sw, 1e-5), 0.05), 0.95);
}
function evalOpaque(n, wo, wi) {
  const h = norm(add(wo, wi));
  if (len(h) === 0) return null;
  const nv = sat(dot(n, wo)), nl = sat(dot(n, wi)), nh = sat(dot(n, h)), vh = Math.max(sat(dot(wo, h)), 1e-4);
  if (nv <= 0 || nl <= 0) return null;
  const D = d_gtr2(alpha, nh), V = v_smith_corr(nv, nl, alpha), F = f_schlick(f0d, vh);
  const fspec = D * V * F, fdiff = ALBEDO / PI, kd = 1 - F;
  const dpdf = nl / PI, spdf = D * nh / (4 * vh + 1e-6);
  return { f: kd * fdiff + fspec, pdf: dpdf + (spdf - dpdf) * specProb() };
}
function evalGlassTrans(n, wo, wi, h, ii, it) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return null;
  const nh = Math.min(Math.abs(dot(n, h)), 1);
  const wih = dot(wi, h), woh = dot(wo, h);
  const den = ii * wih + it * woh;
  if (Math.abs(den) <= 1e-6) return null;
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0d, vh);
  const r2 = (it * it) / (ii * ii);
  return { f: (1 - F) * D * G * Math.abs(woh) / Math.max(nl * nv, 1e-6) * r2 / (den * den),
           pdf: pdf_mfn(alpha, nh) * Math.abs(wih) / (den * den) * r2 };
}
function evalGlassRefl(n, wo, wi) {
  const h = norm(add(wo, wi));
  if (len(h) === 0) return null;
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return null;
  const nh = Math.min(Math.abs(dot(n, h)), 1);
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0d, vh);
  return { f: D * G * F / Math.max(4 * nl * nv, 1e-6), pdf: pdf_mfn(alpha, nh) / Math.max(4 * vh, 1e-6) };
}
// ---- 复刻 EvalGlassBSDF ----
function evalGlassBSDF(n, wo, wi, tr) {
  const ow = 1 - tr;
  const eo = ow > 0 ? evalOpaque(n, wo, wi) : null;
  const f_opaque = eo ? ow * eo.f : 0, pdf_opaque = eo ? ow * eo.pdf : 0;
  const hrs = norm(add(wo, wi));
  const hr = dot(hrs, wo) < 0 ? mul(hrs, -1) : hrs;
  let er = evalGlassRefl(n, wo, wi);
  if (!er) er = { f: 0, pdf: 0 };
  const ht = norm(add(mul(wi, IOR), mul(wo, 1.0)));
  let et = null;
  if (len(ht) > 0) {
    const vh = Math.max(Math.abs(dot(wo, ht)), 1e-4);
    const tp = 1 - rsp(f0d, vh);
    const rp = 1 - tp;
    pdfmix: {
      // EvalGlassBSDF 用「按选择概率加权」的联合量
      const fr = rp * er.f, pr = rp * er.pdf;
      const t = evalGlassTrans(n, wo, wi, ht, 1.0, IOR);
      const ft = t && tp > 0 ? tp * t.f : 0, pt = t && tp > 0 ? tp * t.pdf : 0;
      return { f: tr * (fr + ft) + f_opaque, pdf: tr * (pr + pt) + pdf_opaque };
    }
  }
  return { f: tr * er.f + f_opaque, pdf: tr * er.pdf + pdf_opaque };
}
// ---- 复刻 SampleBRDF ----
function sampleBRDF(n, wo, tr) {
  const r0 = rnd(), r1 = rnd(), r2 = rnd();
  const sp = specProb();
  if (r0 >= tr) {
    if (r1 < sp) { const h = sGGX(alpha, r1, r2); return { wi: sub(mul(h, 2 * dot(wo, h)), wo), opaque: true }; }
    const wi = sampleCosHemi(n, r1, r2); return { wi, opaque: true };
  }
  const rg = tr > 1e-6 ? Math.min(Math.max(r0 / tr, 0), 1) : 0;
  let h = sGGX(alpha, r1, r2); if (dot(h, n) < 0) h = mul(h, -1);
  const rp = rsp(f0d, Math.abs(dot(wo, h)));
  let doR = rg < rp;
  const rf = refract(wo, h, 1 / IOR);
  if (rf === null || dot(rf, rf) < 1e-12) doR = true;
  const wi = doR ? sub(mul(h, 2 * dot(wo, h)), wo) : rf;
  return { wi, opaque: false, h };
}
function measure(theta_deg, tr, M = 400000) {
  const th = theta_deg * PI / 180, n = [0, 0, 1], wo = [Math.sin(th), 0, Math.cos(th)];
  let sum = 0, nZeroPdf = 0, nOpaque = 0, sumOpaque = 0, sumGlass = 0;
  for (let i = 0; i < M; i++) {
    const s = sampleBRDF(n, wo, tr);
    if (!s.wi || len(s.wi) === 0) continue;
    const ct = Math.abs(dot(n, s.wi));
    if (ct <= 1e-6) continue;
    // 与 shader 一致：采样路径直接用采样到的 h
    const e = s.opaque ? evalOpaque(n, wo, s.wi) : null;
    let f, pdf;
    if (s.opaque) {
      if (!e) { nZeroPdf++; continue; }
      f = (1 - tr) * e.f; pdf = (1 - tr) * e.pdf;
      nOpaque++; sumOpaque += f * ct / Math.max(pdf, 1e-30);
    } else {
      const ec = evalGlassBSDFWithH(n, wo, s.wi, s.h, tr);
      if (!ec || ec.pdf <= 1e-12) { nZeroPdf++; continue; }
      f = ec.f; pdf = ec.pdf;
      sumGlass += f * ct / pdf;
    }
    if (pdf <= 1e-12) { nZeroPdf++; continue; }
    sum += f * ct / pdf;
  }
  return { e: sum / M, nZeroPdf, nOpaque, avgOpaque: sumOpaque / Math.max(nOpaque, 1), avgGlass: sumGlass / Math.max(M - nOpaque - nZeroPdf, 1) };
}
// 采样路径版本：直接用采样到的 h，且只求值被选中的那个瓣
function evalGlassBSDFWithH(n, wo, wi, h, tr) {
  const ow = 1 - tr;
  let f = 0, pdf = 0;
  // 判断走的是哪个瓣：与采样端一致——折射成功就是透射瓣
  const isRefl = Math.abs(dot(h, n)) > 0 && dot(wi, h) * dot(wo, h) >= 0;
  if (isRefl) {
    const er = evalGlassRefl(n, wo, wi);
    if (er) { f = tr * er.f; pdf = tr * er.pdf; }
  } else {
    const et = evalGlassTrans(n, wo, wi, h, 1.0, IOR);
    if (et) { f = tr * et.f; pdf = tr * et.pdf; }
    else { const er = evalGlassRefl(n, wo, wi); if (er) { f = tr * er.f; pdf = tr * er.pdf; } }
  }
  return { f, pdf };
}
console.log('均匀环境下的 E[f*cos/pdf]，无损表面应恒等于 1：');
console.log('tr      theta=0    theta=30   theta=60');
for (const tr of [0.0, 0.25, 0.5, 0.54, 0.75, 0.9, 1.0]) {
  const a = measure(0, tr), b = measure(30, tr), c = measure(60, tr);
  console.log(`${tr.toFixed(2)}    ${a.e.toFixed(4)}     ${b.e.toFixed(4)}     ${c.e.toFixed(4)}   (0°: opaque样本=${a.nOpaque} 均权=${a.avgOpaque.toFixed(3)} / glass均权=${a.avgGlass.toFixed(3)})`);
}
