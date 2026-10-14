# 数值 Reference 与 GPU 对照说明

这是额外的验收辅助资料，不替代每个 Milestone 的 Validation/Acceptance Criteria。

- `cloud_reference_tests.py`：纯 Python、无需第三方包；运行 `python validation/cloud_reference_tests.py`，验证几何、Coverage、HG、均匀介质、合成、分时覆盖与风场的独立参考公式。
- `gpu_reference_vectors.json`：可复用的确定性输入及 CPU double 期望输出。**每个字段值不是 AiluEngine 的实测结果**。

## GPU Test 的最小工作流

1. 使用 Shader Debug Kernel 或现有 Test ImageCapture 读取相同常量输入。禁止让 Shader 取当前 Weather 资产或动态 `_Time`，否则条件不相同。
2. 用 R32F/RGBA32F 捕获测量。含 RGBAHalf 的结果先记录浮点量化上界，超出误差再视作实现缺陷。
3. 每一项保存 `input.json`、`expected.json`、`observed.json`、`diff.json` 和原始 RenderDoc/PIX 抓帧引用。
4. 相对误差只在 expected 非零时使用；expected≈0 必须用**绝对误差**，不得用随机偏差除以 0。
5. 算法无关的噪声/多次散射视觉近似不能靠这组数学向量独自验收；遵循 M3/M5 的频谱、极限、参考场景和连续性验收。
6. 不要将脚本里的 `ShellRayIntervals` 当作可复制到 HLSL 的生产代码；它是独立 double oracle，用来发现 HLSL 区间错误。真实渲染要额外裁剪 Earth/scene occlusion。

## 状态

参考向量与 CPU 测试可在开发环境之外运行；**没有任何 GPU 或 Shader 的结果被本文件声明为 PASS**。
