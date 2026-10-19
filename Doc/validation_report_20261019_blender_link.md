# Blender Live Link 验证报告

## 运行信息

- 场景：`flower_scattering.blend`
- 运行 ID：`20261019-223535-1f9c75b3`
- 固定输出根目录：`F:\AiluEngine`
- 证据目录：`F:\AiluEngine\artifacts\out\editor\bin\x64\debug\blender_link_validation_20261019_oracle_fix_full\20261019-223535-1f9c75b3`
- Blender：5.0.1
- 原始场景 SHA256：`a22fc66c7bcaad45fdd548b8b1ecd9ac1a5f1268bed34fba1a95c9ac9cd862d9`

## 结论

| 验证层级 | 结果 | 说明 |
| --- | --- | --- |
| Debug 构建 | PASS | `cmake --build --preset build-debug` |
| Native Full Sync | PASS | Begin/End 各 1 次，Blender 和 Editor 正常退出 |
| Engine 材料应用 | PASS | 165 个材料包，Apply failure/exception 均为 0 |
| 静态批次同步 | PASS | 116,633 个源实例合并为 414 个批次 |
| 结构材料绑定 | PASS | 7,011 项检查，0 失败，0 unsupported |
| NodeOracle | PASS | 检查 415 个 Engine 实体 |
| RenderGraph 捕获传输 | PASS | Albedo 和 Final 均完成连续 3 帧捕获 |
| 增量同步 | SKIPPED | 真实场景运行限制为 Full Sync |

## 关键数据

- 协议包总数：1003
- 材料包：165
- 静态网格批次包：414
- 实例批次包：414
- 静态合并顶点：69,215,584
- 静态合并三角形：34,607,792
- FullSync Begin：1 次，序列号 2
- FullSync End：1 次，序列号 1004
- 原始场景未修改：是
- 运行期配置和用户文件恢复：是

## 修复内容

1. 缩短材料纹理资产通道后缀，避免 Windows 路径过长导致材料应用异常。
2. 独立材料参考增加对象级烘焙 GUID、网格数据 GUID、空间坐标/修改器身份和生成组件材料规则。
3. 材料绑定验证器增加静态批次网格协议 type 32 的解析。

## 证据文件

- [运行元数据](F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation_20261019_oracle_fix_full/20261019-223535-1f9c75b3/run.json)
- [修复后材料绑定验证](F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation_20261019_oracle_fix_full/20261019-223535-1f9c75b3/manual_after_parser_fix/material_assertions.json)
- [Engine/NodeOracle 日志](F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation_20261019_oracle_fix_full/20261019-223535-1f9c75b3/editor/oracle.log)
- [Albedo 捕获状态](F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation_20261019_oracle_fix_full/20261019-223535-1f9c75b3/material_albedo_status.json)
- [Final 捕获状态](F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation_20261019_oracle_fix_full/20261019-223535-1f9c75b3/material_final_status.json)

说明：原始 `run.json` 的 `status` 保留了验证器修复前的 FAIL；该运行产生的同一份 packets 和 Engine snapshot 已使用修复后的验证器重新检查并 PASS。该场景未进行增量、重连和持久化验证，也未将 Blender 与 Engine 像素结果宣称为完全一致。
