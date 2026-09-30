# Validation
## Build Validation

每个 Milestone：

```text
Debug or RelWithDebInfo full build
Shader compile no new errors
Shader compile no new warnings
```

确认 shader cache 已正确失效。

---

## GPU Validation

记录至少：

```text
VolumetricFog_LightInject
VolumetricFog_LightIntegration
VolumetricFog_Composite
```

对比：

```text
Baseline
M1
M2
M3
M4
```

M1 原则上不应明显增加 GPU cost。

---

## Visual Validation Matrix

| Case | Temporal Off | Temporal On |
|---|---:|---:|
| Static camera | Required | Required |
| Slow pan | Required | Required |
| Fast rotate | Optional | Required |
| Camera cut | N/A | Required |
| Wide FOV | Required | Required |
| Near geometry | Required | Required |
| Shadow edge | Required | Required |
| Look toward sun | Required | Required |
| Look away from sun | Required | Required |
| No ShadowMap | Required | Required |
| No Directional Light | Required | Required |
| Second camera | Optional | Required |

---

## Numerical Checks

重点保证：

```text
phase
injected lighting
history
extinction
optical depth
accumulated scattering
transmittance
```

为有限值。

公式：

```text
T = exp(-sigma_t * distance)
```

要求：

```text
0 <= T <= 1
```

随 distance 增加：

```text
scattering >= 0
transmittance monotonically decreases
```

`g = 0`：

```text
phase ~= 1 / (4 PI)
```

且与方向无关。

---

## Regression Validation

检查：

- Deferred lighting 正常。
- Skybox 无 double composite。
- RenderGraph barrier/state 正常。
- Resize 不 crash。
- Feature enable/disable 不 crash。
- Temporal toggle 不 crash。
- Shadow enable/disable 不 crash。
- No-light scene 不 black frame。
- Debug voxel 不读取 invalid resource。
