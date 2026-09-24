# Surfel GI Phase 1：Surfel Placement

## 数据流

每帧在 GBuffer 完成后，Surfel Placement 按以下顺序执行：

```text
Clear Grid
  -> Prepare indirect dispatch
  -> Count persistent surfels into cascaded cells
  -> Prefix sum cell offsets
  -> Fill cell indices
  -> Compute screen-space CoverageTexture
  -> 16 x 16 tile reduction and spawn
  -> Prepare indirect debug draw arguments
```

Surfel 保持世界空间位置、法线和半径，不随 Camera 移动。Grid 是围绕 Camera 的临时空间索引，每帧从 persistent pool 重建；本阶段不做 scrolling、eviction 或多级混合。新生成的 Surfel 从下一帧开始参与 Grid 查询。

## 数据与参数

`SurfelData` 为 56 字节结构，与 HLSL StructuredBuffer 对齐。`spawn_pixel` 记录生成时的屏幕像素坐标（当前渲染目标左上角为原点）：

```text
position, radius, normal, flags, cascade_level,
last_touched_frame, object_id, padding, spawn_pixel (uint2)
```

默认设置：

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| Base Cell Size | 0.125 m | L0 cell size，后续级别按 2 倍增长 |
| Cascade Count | 5 | L0-L4，最大支持 5 级 |
| Grid Resolution | 64 | 每级 64³ cell |
| Target Pixel Radius | 8 | 由距离、垂直 FOV 和屏幕高度换算 |
| Spawn Coverage Threshold | 0.5 | Coverage 低于该值才允许 spawn |
| Max Surfel Count | 262144 | 固定容量，只增不减 |
| Debug Cascade | -1 | 显示所有级别；0-4 显示单级 |

Camera grid center 使用 `floor(camera_position / cell_size) * cell_size` 对齐，避免 Camera 小幅移动造成地址抖动。根据目标像素半径先计算 desired radius，再选择同时满足半径上限且网格包含该位置的最小 cascade；实际 radius 限制在所属 cell size 的 0.2-0.7 倍。若最高 cascade 网格仍不包含该位置，则该屏幕候选无效，不生成 Surfel。

## Coverage

每个屏幕像素只查询最终选定 cascade 的当前 cell。Coverage 是该 cell 中所有相交 Surfel 权重之和：

```text
delta = position - surfel.position
plane_distance = abs(dot(delta, surfel.normal))
tangent_distance = sqrt(max(dot(delta, delta) - plane_distance², 0))
radial_weight = saturate(1 - tangent_distance / radius)
normal_weight = saturate(dot(pixel_normal, surfel.normal))
plane_weight = saturate(1 - plane_distance / radius)
weight = radial_weight * normal_weight * plane_weight
```

结果写入正式的 RFloat `SurfelGI_Coverage` 资源。Coverage Debug 直接将 `saturate(coverage)` 映射为灰度：黑色表示未覆盖，白色表示 Coverage 至少为 1。

## Editor 与 Debug

向场景添加 `SurfelGIComponent` 后，可在 Inspector 调整上述参数。`Debug Draw Surfels` 绘制所有有效 Surfel 的 16 边形圆盘，圆盘切平面由 Surfel 法线构造；不同 cascade 使用不同颜色。`Debug Coverage` 显示 Coverage 灰度图，`Debug Cascade` 可筛选圆盘级别。

本阶段只包含 placement、grid、coverage 和 debug，不接入 irradiance、radiance、ray tracing、relocation 或 Deferred Lighting。
