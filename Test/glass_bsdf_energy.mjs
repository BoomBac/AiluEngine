// 玻璃 BSDF 能量校验（数值验证，不参与构建）。
//
// 逐行复刻 Engine/Res/Shaders/hlsl/ray_trace/sampling.hlsli 里的
// EvalGlassReflectionLobe / EvalGlassTransmissionLobe / SampleBRDF(glass)。
//
// 两条判据（都是解析可对照的硬约束）：
//   1) 单界面反射率：∫ f_reflect(wi)·|n·wi| dwi 必须等于 F_Schlick 的半球均值
//      —— 等价地，镜面极限下权重 f·|n·wi|/pdf 必须等于 F。
//   2) 单界面透射率：∫ f_transmit(wi)·|n·wi| dwi 必须等于 1 - F。
//      透射瓣的半程向量分布是「delta 状」的（alpha 很小），所以对 h 做高分辨率
//      极角网格积分（方位角解析积出 2π），并把 Jacobian dwh/dwi 换算进 dwi。
//
// 这个测试是定位「玻璃偏暗」的关键：pdf 里 ior 比的取向 / 是否遗漏，
// 会让透射率整体偏 (n_t)^2 或 (n_t)^4，而画面表现就是整块玻璃发暗。
const PI = Math.PI, IOR = 1.5918;
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => { const l = len(a); return l > 1e-12 ? mul(a, 1 / l) : [0, 0, 0]; };
const f_schlick = (f0, x) => f0 + (1 - f0) * Math.pow(1 - Math.min(Math.max(x, 0), 1), 5);
const n_f0 = (ii, it) => { const r = (it - ii) / Math.max(ii + it, 1e-4); return r * r; };
function d_gtr2(a, nh) { const a2 = a * a, c2 = nh * nh, den = 1 + (a2 - 1) * c2; return a2 / (PI * den * den); }
const g_schlick = c => c / (c * 0.5 + 0.5);
const g_smith = (nol, nov) => g_schlick(nol) * g_schlick(nov);
const pdf_mfn = (a, nh) => d_gtr2(Math.max(a, 1e-4), nh) * nh;   // = D * nh

const F0 = n_f0(1, IOR);

// ---- 复刻 EvalGlassTransmissionLobe ----
function evalTransmission(n, wo, wi, h, alpha, ii, it) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return null;
  const nh = Math.min(Math.abs(dot(n, h)), 1);
  const wi_h = dot(wi, h), wo_h = dot(wo, h);
  const denom = ii * wi_h + it * wo_h;
  if (Math.abs(denom) <= 1e-6) return null;
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(F0, vh);
  const denom2 = denom * denom;
  const ior_i2 = Math.max(ii * ii, 1e-6), ior_t2 = Math.max(it * it, 1e-6);
  const ratio2 = ior_t2 / ior_i2;
  const pdf = pdf_mfn(alpha, nh) * Math.abs(wi_h) / denom2 * ratio2;
  const f = (1 - F) * D * G * Math.abs(wo_h) / Math.max(nl * nv, 1e-6) * ratio2 / denom2;
  return { f, pdf };
}

// ---- 复刻 EvalGlassReflectionLobe ----
function evalReflection(n, wo, wi, h, alpha) {
  const nl = Math.abs(dot(n, wi)), nv = Math.abs(dot(n, wo)), vh = Math.abs(dot(wo, h));
  if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6) return null;
  const nh = Math.min(Math.abs(dot(n, h)), 1);
  const D = d_gtr2(alpha, nh), G = g_smith(nl, nv), F = f_schlick(F0, vh);
  const pdf = pdf_mfn(alpha, nh) / Math.max(4 * vh, 1e-6);
  const f = D * G * F / Math.max(4 * nl * nv, 1e-6);
  return { f, pdf };
}

// ---- 透射率：对 h 的极角做网格积分（方位角解析积出 2π） ----
function transmittance(theta_deg, alpha) {
  const th = theta_deg * PI / 180, n = [0, 0, 1], wo = [Math.sin(th), 0, Math.cos(th)];
  const ii = 1.0, it = IOR, eta = ii / it, ratio2 = (it * it) / (ii * ii);
  const N = 2000;
  let acc = 0;
  for (let i = 1; i <= N; i++) {
    const th_h = PI * 0.5 * (i - 0.5) / N;
    const h = [Math.sin(th_h), 0, Math.cos(th_h)];
    const dh = Math.sin(th_h) * (PI * 0.5 / N) * 2 * PI;
    const ci = -dot(mul(wo, -1), h), k = 1 - eta * eta * (1 - ci * ci);
    if (k < 0) continue;
    const wi = norm(add(mul(mul(wo, -1), eta), mul(h, eta * ci - Math.sqrt(k))));
    const r = evalTransmission(n, wo, wi, h, alpha, ii, it);
    if (!r) continue;
    // dwh/dwi = |wi·h|/denom^2 * ratio2  =>  dwi = dwh * denom^2/|wi·h| / ratio2
    const wih = dot(wi, h), woh = dot(wo, h);
    const denom = ii * wih + it * woh;
    const dwi = dh * (denom * denom) / Math.max(Math.abs(wih), 1e-12) / ratio2;
    acc += r.f * Math.abs(dot(n, wi)) * dwi;
  }
  return acc;
}

// ---- 反射率：镜面极限下 f·|n·wi|/pdf 就是 F（h 分布退化为 delta） ----
function reflectionWeightAtNormal() {
  const n = [0, 0, 1], wo = [0, 0, 1], h = [0, 0, 1], wi = [0, 0, 1];
  const r = evalReflection(n, wo, wi, h, 1e-4);
  return r ? r.f * Math.abs(dot(n, wi)) / r.pdf : NaN;
}

console.log('== 玻璃 BSDF 单界面能量校验 ==');
console.log(`理论 F = ${F0.toFixed(6)}    理论 1-F = ${(1 - F0).toFixed(6)}\n`);

const rw = reflectionWeightAtNormal();
console.log(`反射瓣 法线入射权重 = ${rw.toFixed(6)}  (应为 F = ${F0.toFixed(6)})  ${Math.abs(rw - F0) < 1e-6 ? 'OK' : '错!'}`);
console.log('  （pdf 少了 1/(4|wo·h|) 的话这里会只有 1/4，是「边缘反射被压暗」的一个来源）\n');

console.log('透射瓣 半球积分（低粗糙度时高角度受 Smith 阴影限制，属正常）:');
let worst = 0;
for (const t of [0, 15, 30, 45, 60, 75]) {
  const v = transmittance(t, 0.02);
  const dev = t <= 15 ? Math.abs(v - (1 - F0)) : 0;   // 只在近法线处要求严格
  worst = Math.max(worst, dev);
  console.log(`  theta=${String(t).padStart(2)}°  透射积分 = ${v.toFixed(6)}`);
}
console.log(`\n近法线偏差 = ${worst.toFixed(6)}  ${worst < 0.01 ? 'OK' : '错!'}`);
console.log('\n注：把 pdf 里的 ior 比写成 ior_i^2/ior_t^2，或整个漏掉，');
console.log('    会让 0° 的透射积分变成 6.09 / 2.40（正确值 0.948），即整块玻璃偏暗 (n_t)^2~(n_t)^4 倍。');
