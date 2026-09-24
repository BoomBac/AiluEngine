// 定位玻璃能量损失到底出在哪条路径。
//
// 白炉检验用的是「采样 + 求值」，两条路径混在一起看不出问题：
//   A) 采样路径：求值端使用采样端带下来的 (lobe, h)
//   B) NEE 路径：求值端从 (wo, wi) 解析反推半程向量（EvalGlassBSDFAnalytic）
// 这里把两者分开跑，还额外实现一份「参照实现」（直接对半程向量做数值积分），
// 用来判定到底是公式错还是反推错。
//
// 关键量：权重 w = f * |cos(theta_i)| / pdf。
// 对折射界面上的辐射传输，正确的权重是
//   w_refract = eta^2 = (ior_i / ior_t)^2      （辐射亮度按 n^2 缩放）
//   w_reflect = 1
// 若某个瓣的权重不满足这个关系，说明 f 与 pdf 的 Jacobian 不配对。
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
function refract(wi, n, eta) { const ci = -dot(wi, n), k = 1 - eta * eta * (1 - ci * ci); if (k < 0) return null; return norm(add(mul(wi, eta), mul(n, eta * ci - Math.sqrt(k)))); }

const IOR = 1.5918, ROUGH = 0.04;
const alpha = Math.max(ROUGH * ROUGH, 1e-6);

// ---- shader 里的 EvalGlassTransmissionLobe（逐行） ----
function lobeTrans(n, wo, wi, h, ii, it, f0) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return { f: 0, pdf: 0 };
  const nh = sat(Math.abs(dot(n, h)));
  const wih = dot(wi, h), woh = dot(wo, h);
  const den = ii * wih + it * woh;
  if (Math.abs(den) <= 1e-6) return { f: 0, pdf: 0 };
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0, vh);
  const r2 = (it * it) / (ii * ii);
  return { f: (1 - F) * D * G * Math.abs(woh) / Math.max(nl * nv, 1e-6) * r2 / (den * den),
           pdf: pdf_mfn(alpha, nh) * Math.abs(wih) / (den * den) * r2 };
}
// ---- 参照：Walter et al. 2007 论文里的原始折射 BRDF（不带任何 ior 比值因子） ----
function lobeTransRef(n, wo, wi, h, ii, it, f0) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return { f: 0, pdf: 0 };
  const nh = sat(Math.abs(dot(n, h)));
  const wih = dot(wi, h), woh = dot(wo, h);
  const den = ii * wih + it * woh;
  if (Math.abs(den) <= 1e-6) return { f: 0, pdf: 0 };
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0, vh);
  return { f: (1 - F) * D * G * Math.abs(woh) / (nl * nv) / (den * den), pdf: pdf_mfn(alpha, nh) * Math.abs(wih) / (den * den) };
}
function lobeRefl(n, wo, wi, h, f0) {
  if (len(h) === 0) return { f: 0, pdf: 0 };
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return { f: 0, pdf: 0 };
  const nh = sat(Math.abs(dot(n, h)));
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(f0, vh);
  return { f: D * G * F / Math.max(4 * nl * nv, 1e-6), pdf: pdf_mfn(alpha, nh) / Math.max(4 * vh, 1e-6) };
}

const th_deg = 60, th = th_deg * PI / 180;
const n = [0, 0, 1], wo = [Math.sin(th), 0, Math.cos(th)];
const eta = 1 / IOR, eta2 = eta * eta;

// 直接固定一个半程向量，做单瓣精确检查（剔除采样噪声）
function single(h) {
  const rr = refract(mul(wo, -1), h, eta);
  if (!rr) return null;
  const wi = rr;
  const pR = rsp(n_f0(1, IOR), Math.abs(dot(wo, h)));
  const shader = lobeTrans(n, wo, wi, h, 1.0, IOR, n_f0(1, IOR));
  const ref = lobeTransRef(n, wo, wi, h, 1.0, IOR, n_f0(1, IOR));
  const ct = Math.abs(dot(n, wi));
  return {
    pR, ct,
    w_shader: shader.f * ct / shader.pdf,
    w_ref: ref.f * ct / ref.pdf,
    ratio: shader.f / ref.f,
  };
}
// GTR2(alpha=0.0016) 半程向量几乎平行于 n，取几个略微偏斜的 h 看比值
console.log(`theta_i = ${th_deg}°, IOR = ${IOR}, eta = ${eta.toFixed(6)}, eta^2 = ${eta2.toFixed(6)}`);
console.log('');
console.log('h 与 n 的夹角   p_R       透射权重(shader)  透射权重(无比值参照)  比值');
for (const dth of [0, 0.1, 0.5, 1, 3, 10]) {
  const a = dth * PI / 180;
  const h = [Math.sin(a), 0, Math.cos(a)];
  const r = single(h);
  if (!r) { console.log(`${String(dth).padEnd(14)} 全反射`); continue; }
  console.log(`${String(dth).padEnd(14)} ${r.pR.toFixed(4)}     ${r.w_shader.toFixed(6)}          ${r.w_ref.toFixed(6)}              ${r.ratio.toFixed(4)}`);
}
console.log('');
console.log('期望：折射的辐射亮度权重 w = f*|cos|/pdf 应等于 eta^2（无比值版本会多出 n_t^2 倍）。');
console.log('如果 shader 的 w 与 eta^2 不符，说明 f 和 pdf 的 Jacobian 没有配对。');
