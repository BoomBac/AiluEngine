# AiluEngine Ailu Shader Tools 开发文档

> 目标：为 AiluEngine 当前基于 HLSL 注释块的 Shader 元数据 DSL 提供 VS Code 自动补全、提示、诊断与跳转能力，同时逐步统一 DSL 规范，避免编辑器规则与运行时解析规则长期漂移。

## 1. 背景

AiluEngine 当前直接在 `.hlsl` 文件顶部通过注释块描述 Shader Pass 元数据，例如：

```hlsl
//info bein
//pass begin::
//name: ToneMapping
//vert: FullscreenVSMain
//pixel: ToneMappingPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//info end
```

运行时由 `Shader::PreProcessShader()` 读取 `//info bein` ~ `//info end`，再按 `//pass begin::` / `//pass end::` 拆分 Pass，并解析管线状态、入口函数、关键字等信息。

相关实现主要位于：

- `Engine/Inc/Render/Shader.h`
  - `ShaderCommand`
  - `ShaderPass`
- `Engine/Src/Render/Shader.cpp`
  - `ReadFileToLines()`
  - `ExtractPassBlocks()`
  - `Shader::PreProcessShader()`
  - `Shader::ParserShaderProperty()`

当前这种格式足够轻量，但编辑体验依赖人工记忆，容易出现：

- Key 拼写错误；
- 枚举值拼写错误；
- Shader entry 名称与实际 HLSL 函数不一致；
- `pass begin/end`、`info begin/end` 不配对；
- 新增 DSL 字段后，运行时与编辑器规则不一致；
- 历史拼写和实际用法逐渐漂移。

因此增加一个独立的 VS Code 插件 `Ailu Shader Tools`。

---

## 2. 总体目标

### 2.1 第一阶段目标

在完全兼容当前 `.hlsl` 文件的前提下提供：

1. Shader 元数据 Key 自动补全；
2. 根据 Key 提供 Value 自动补全；
3. `vert / pixel / geometry` Shader Entry 自动补全；
4. Shader Entry 定义跳转；
5. 元数据 Hover 文档；
6. 实时语法与语义诊断；
7. Pass / Info Snippet；
8. `Stencil` 等复合字段的上下文补全；
9. `multi_compile` 关键字辅助；
10. 对旧 DSL 写法保持兼容。

### 2.2 第二阶段目标

建立唯一的 Shader DSL Schema，使：

```text
Shader Meta Schema
        |
        +--> VS Code Extension
        |
        +--> AiluEngine Shader Parser / Generated Header
```

最终避免 C++ Parser 与 VS Code 插件各自维护一套字符串规则。

### 2.3 非目标

第一版明确不做：

- 不实现完整 HLSL Language Server；
- 不替代已有 HLSL IntelliSense 插件；
- 不实现完整 HLSL AST；
- 不引入复杂 Parser Generator；
- 不改造成 Unity ShaderLab 一类独立 Shader 语言；
- 不为了插件强制一次性修改所有现有 Shader 文件。

插件只负责 AiluEngine 自定义 DSL，HLSL 语义继续交给现有 HLSL 扩展。

---

## 3. 当前 DSL 能力整理

当前 `ShaderCommand` / `PreProcessShader()` 已支持或实际使用以下 Pass 字段。

| Key | 示例 | 作用 |
| --- | --- | --- |
| `name` | `//name: ToneMapping` | Pass 名称 |
| `vert` | `//vert: FullscreenVSMain` | Vertex Shader Entry |
| `pixel` | `//pixel: ToneMappingPS` | Pixel Shader Entry |
| `geometry` | `//geometry: GSMain` | Geometry Shader Entry |
| `Cull` | `Back / Front / Off` | Rasterizer Cull Mode |
| `Queue` | `Opaque / Transparent` | Render Queue |
| `Topology` | `Triangle / TriangleStrip / Line` | Primitive Topology |
| `Blend` | `Src,OneMinusSrc` | Blend Factors |
| `Fill` | `Solid / Wireframe` | Fill Mode |
| `ZTest` | `LEqual / Always / Off ...` | Depth Compare |
| `ZWrite` | `On / Off` | Depth Write |
| `Stencil` | `{Ref:1,Comp:Always,Pass:Replace}` | Stencil State |
| `ColorMask` | `RGBA / RGB / A ...` | Color Write Mask |
| `Conservative` | `On / Off` | Conservative Raster |
| `multi_compile` | `_ _CIRCLE` | Variant Keyword Group |

另外 `//Properties` 区域存在独立的 Shader Property DSL，可作为插件第二批能力继续支持。

---

## 4. 当前 DSL 存在的历史问题

插件开发时不要简单把现有 `ShaderCommand` 常量照抄一遍，而应顺手定义“规范写法 + 兼容别名”。

### 4.1 `info bein` 拼写问题

当前运行时精确搜索：

```text
//info bein
```

其中 `bein` 是历史拼写。

建议规范写法改为：

```text
//info begin
```

但第一阶段运行时与插件都接受：

```text
//info bein
//info begin
```

插件对旧写法只给弱提示，不报错。

### 4.2 `Never / Nevel`

当前 `ShaderCommand::Comparison` 中存在 `Nevel` 历史拼写。

规范值：

```text
Never
```

兼容值：

```text
Nevel
```

建议 Parser 第二阶段同时接受二者，插件只补全 `Never`。

### 4.3 Cull Off / none

当前常量定义与实际 Shader 用法不完全一致。

规范写法：

```text
Off
Front
Back
```

兼容：

```text
None
none
```

不再依赖“非 Back/Front 的任何值都当作 None”这种隐式容错。

### 4.4 Queue

当前实际 Shader 普遍使用：

```text
Opaque
Transparent
```

建议 Schema 正式定义这两个值。

如果未来需要 `Geometry / AlphaTest` 等 Queue，应明确加入 Schema，而不是依赖默认值。

### 4.5 Key 大小写

当前 Parser 基本采用不区分大小写比较。

插件建议：

- Parser 继续大小写不敏感；
- 自动补全输出统一规范格式；
- 不因为 `//cull:` 报错；
- 可给 Code Action：`Normalize Ailu Shader metadata casing`。

---

## 5. 推荐架构

### 5.1 总体结构

```text
AiluEngine/
├── Engine/
│   ├── Config/
│   │   └── shader_meta_schema.json
│   └── ...
│
└── Tools/
    └── AiluShaderTools/
        ├── package.json
        ├── tsconfig.json
        ├── src/
        │   ├── extension.ts
        │   ├── shader_meta_schema.ts
        │   ├── shader_meta_parser.ts
        │   ├── completion_provider.ts
        │   ├── hover_provider.ts
        │   ├── diagnostic_provider.ts
        │   ├── definition_provider.ts
        │   ├── document_index.ts
        │   └── utils.ts
        ├── snippets/
        │   └── hlsl.json
        └── test/
            ├── parser.test.ts
            ├── completion.test.ts
            └── diagnostics.test.ts
```

### 5.2 不注册新语言

插件直接挂在 `hlsl` 文档上：

```text
.hlsl
  |
  +--> 现有 HLSL Extension
  |      +--> HLSL syntax / semantic / IntelliSense
  |
  +--> Ailu Shader Tools
         +--> metadata completion
         +--> diagnostics
         +--> hover
         +--> entry navigation
```

不要定义 `ailushader` 新语言，避免失去现有 HLSL 工具链能力。

### 5.3 不使用 LSP

第一版直接使用 VS Code Extension API：

- `registerCompletionItemProvider`
- `registerHoverProvider`
- `registerDefinitionProvider`
- `DiagnosticCollection`
- `onDidOpenTextDocument`
- `onDidChangeTextDocument`
- `onDidSaveTextDocument`

当前 DSL 很小，TypeScript Extension 足够，LSP 会带来不必要复杂度。

---

## 6. Shader Meta Schema

### 6.1 Schema 是唯一规则来源

建议新增：

```text
Engine/Config/shader_meta_schema.json
```

示例：

```json
{
  "version": 1,
  "markers": {
    "info_begin": "//info begin",
    "info_begin_aliases": ["//info bein"],
    "info_end": "//info end",
    "pass_begin": "//pass begin::",
    "pass_end": "//pass end::"
  },
  "fields": {
    "name": {
      "type": "string",
      "required": true,
      "description": "Shader pass name"
    },
    "vert": {
      "type": "shader_entry",
      "stage": "vertex",
      "required": true
    },
    "pixel": {
      "type": "shader_entry",
      "stage": "pixel"
    },
    "geometry": {
      "type": "shader_entry",
      "stage": "geometry"
    },
    "Cull": {
      "type": "enum",
      "values": ["Off", "Front", "Back"],
      "aliases": {
        "None": "Off",
        "none": "Off"
      }
    },
    "Queue": {
      "type": "enum",
      "values": ["Opaque", "Transparent"]
    },
    "Topology": {
      "type": "enum",
      "values": ["Triangle", "TriangleStrip", "Line"]
    },
    "Fill": {
      "type": "enum",
      "values": ["Solid", "Wireframe"]
    },
    "ZWrite": {
      "type": "enum",
      "values": ["On", "Off"]
    },
    "ZTest": {
      "type": "enum",
      "values": ["Off", "Never", "Less", "LEqual", "Equal", "Greater", "GEqual", "NotEqual", "Always"],
      "aliases": {
        "Nevel": "Never"
      }
    },
    "Conservative": {
      "type": "enum",
      "values": ["On", "Off"]
    }
  }
}
```

### 6.2 Schema 与 C++ 的关系

第一阶段可以先做到：

```text
C++ Parser         -> 保持现状
VS Code Extension  -> 读取 schema
```

第二阶段再增加轻量 CodeGen：

```text
shader_meta_schema.json
        |
        +--> ShaderMetaSchema.gen.h
```

将字符串常量与枚举别名生成到 C++，让运行时和插件真正共享规则。

不要让 TypeScript 去解析 `Shader.h`，也不要让 C++ 运行时去解析复杂插件配置。

---

## 7. Parser 设计

### 7.1 使用轻量行解析器

不需要完整 AST。

解析流程：

```text
document text
    |
    +--> scan info block
            |
            +--> scan pass blocks
                    |
                    +--> parse `//key: value`
                    +--> parse multi_compile
                    +--> record source ranges
```

需要保留每个节点的 `Range`，供：

- Completion；
- Hover；
- Diagnostics；
- Definition；
- Code Action。

### 7.2 基础数据结构

建议 TypeScript 内部使用：

```ts
interface shader_meta_field {
    key: string;
    value: string;
    line: number;
    key_range: vscode.Range;
    value_range: vscode.Range;
}

interface shader_pass_block {
    begin_line: number;
    end_line: number;
    fields: shader_meta_field[];
}

interface shader_meta_document {
    info_begin_line: number;
    info_end_line: number;
    passes: shader_pass_block[];
}
```

解析器必须容忍正在编辑中的半成品，例如：

```hlsl
//ZTest:
```

或：

```hlsl
//pass begin::
```

不能因为块尚未结束就完全放弃 Completion。

---

## 8. 自动补全

## 8.1 Key Completion

当光标处于 Pass Block 内，并输入：

```hlsl
//C|
```

建议：

```text
Cull
ColorMask
Conservative
```

选择后插入：

```hlsl
//Cull: 
```

### 8.2 Value Completion

根据字段类型提供上下文补全。

#### Cull

```hlsl
//Cull: |
```

```text
Off
Front
Back
```

#### ZTest

```text
Off
Never
Less
LEqual
Equal
Greater
GEqual
NotEqual
Always
```

#### ZWrite

```text
On
Off
```

#### Fill

```text
Solid
Wireframe
```

#### Topology

```text
Triangle
TriangleStrip
Line
```

### 8.3 Blend Completion

支持逗号两侧上下文。

```hlsl
//Blend: Src,|
```

建议：

```text
Src
OneMinusSrc
One
Zero
```

同时可以在第一个参数处提供相同候选。

### 8.4 Shader Entry Completion

例如：

```hlsl
//vert: Full|
```

插件扫描当前文档的 HLSL 函数并推荐：

```text
FullscreenVSMain
```

第一版只要求当前文件。

第二版可以解析：

```hlsl
#include "common.hlsli"
```

并建立 Workspace Shader Symbol Index。

### 8.5 Entry Stage 过滤

如果能从签名或语义可靠判断 Stage，则过滤；如果判断成本较高，第一版可以不强制过滤，只优先显示名称匹配结果。

不要为了 Stage 分类实现完整 HLSL Parser。

---

## 9. Definition 跳转

支持：

```hlsl
//vert: FullscreenVSMain
//pixel: ToneMappingPS
```

在 `FullscreenVSMain` / `ToneMappingPS` 上执行 Go To Definition，跳转到当前文件或已索引 include 中的函数定义。

匹配策略第一版可采用：

```text
<identifier> (...)
```

配合简单注释过滤即可。

如果存在多个同名符号，返回多个 `Location` 交给 VS Code 选择。

---

## 10. Hover

Hover 用于解释 DSL，而不是重复显示字段名。

例如：

```hlsl
//ZTest: LEqual
```

Hover：

```text
Depth comparison: Less or Equal.

AiluEngine uses reversed-Z when enabled; runtime mapping may become GreaterEqual.
```

`Queue`：

```text
Opaque
Render queue for opaque geometry. Default queue: 2000.
```

`Blend`：

```text
Blend: Src,OneMinusSrc
Enables alpha blending using source alpha and one-minus-source-alpha.
```

Hover 文本也建议由 Schema 提供基础描述，特殊运行时行为可以放在插件文档表中。

---

## 11. Diagnostics

Diagnostics 是第一版最有价值的能力之一。

### 11.1 非法 Key

```hlsl
//ZWirte: Off
```

错误：

```text
AILU_SHADER_001 Unknown metadata field "ZWirte".
Did you mean "ZWrite"?
```

### 11.2 非法 Value

```hlsl
//ZWrite: False
```

错误：

```text
AILU_SHADER_002 Invalid ZWrite value "False".
Expected: On | Off
```

### 11.3 Entry 不存在

```hlsl
//pixel: ToneMapingPS
```

错误：

```text
AILU_SHADER_003 Shader entry "ToneMapingPS" was not found.
Did you mean "ToneMappingPS"?
```

### 11.4 Block 不匹配

检测：

- `info begin` 没有 `info end`；
- `pass begin` 没有 `pass end`；
- Pass Block 出现在 Info Block 外；
- Pass 嵌套。

### 11.5 重复字段

例如：

```hlsl
//ZWrite: On
//ZWrite: Off
```

给 Warning：

```text
AILU_SHADER_005 Duplicate metadata field "ZWrite" in the same pass.
```

不要默默依赖“后一个覆盖前一个”。

### 11.6 必填字段

默认要求：

```text
name
vert
```

`pixel` 是否必填应根据实际引擎允许的 Pass 类型确定；第一版不要贸然将所有 Pass 强制要求 Pixel Shader。

### 11.7 旧写法提示

例如：

```hlsl
//info bein
```

只给 Hint / Information：

```text
Legacy spelling. Prefer "//info begin".
```

不要产生 Error。

---

## 12. Stencil 复合字段

当前格式：

```hlsl
//Stencil: {Ref:5,Comp:Always,Pass:Replace}
```

插件提供分层 Completion。

输入：

```hlsl
//Stencil: {|
```

候选：

```text
Ref
Comp
Pass
```

输入：

```hlsl
//Stencil: {Ref:1,Comp:|
```

候选：

```text
Never
Less
LEqual
Equal
Greater
GEqual
NotEqual
Always
```

输入：

```hlsl
//Stencil: {Ref:1,Comp:Always,Pass:|
```

候选：

```text
Replace
Keep
Zero
Incr
Decr
```

同时校验：

- Ref 是否为整数；
- 子字段是否重复；
- Comp / Pass 是否属于合法枚举。

---

## 13. multi_compile 支持

当前格式：

```hlsl
//multi_compile _ _CIRCLE
```

第一版：

- 高亮/识别该行；
- Hover 解释该行会生成一个关键字组；
- 检查空组；
- 检查同一组重复 keyword。

后续可进一步：

- Workspace Keyword Completion；
- 从 `Properties` 的 Toggle / Enum 自动推荐生成的 keyword；
- 查看当前 Pass Variant 数量。

可以在 Hover 中显示：

```text
Variant group: 2 options
Current pass total variants: 8
```

用于及时发现 Variant 数量爆炸。

---

## 14. Shader Properties 支持

不是第一版阻塞项，但建议预留 Parser 接口。

当前 `Shader::ParserShaderProperty()` 已支持：

- `Texture2D`
- `Texture3D`
- `Color`
- `Range`
- `Float`
- `Vector`
- `[Toggle]`
- `[Enum(...)]`
- `[HDR]`

后续插件可以提供：

- Property Type Completion；
- `[Toggle] / [Enum] / [HDR]` Completion；
- Range 参数校验；
- 默认值维度校验；
- Enum 参数成对校验；
- Property 与 HLSL binding 名称匹配提示。

此部分独立为第二阶段，避免第一版范围失控。

---

## 15. Snippet

### 15.1 `ailupass`

输入：

```text
ailupass
```

展开：

```hlsl
//pass begin::
//name: ${1:pass_name}
//vert: ${2:VSMain}
//pixel: ${3:PSMain}
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: LEqual
//ZWrite: On
//pass end::
```

### 15.2 `ailuinfo`

展开：

```hlsl
//info begin
//pass begin::
//name: ${1:pass_name}
//vert: ${2:VSMain}
//pixel: ${3:PSMain}
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: LEqual
//ZWrite: On
//pass end::
//info end
```

Snippet 使用新规范拼写，但运行时需要先支持 `//info begin` 后再正式推荐。

在运行时尚未兼容之前，Snippet 临时仍生成 `//info bein`，避免新文件无法加载。

---

## 16. Document Index

为了让 Completion / Definition 不在每次按键时重新全文扫描，维护轻量文档缓存。

```text
Uri
  +--> version
  +--> parsed metadata
  +--> HLSL function symbols
```

更新时机：

- Document Open；
- Document Change，使用 100~200 ms debounce；
- Document Close 清理缓存。

Workspace include 索引可以后做。

第一版单文件通常只有几百到几千行，不需要复杂增量 AST。

---

## 17. 性能要求

插件必须保持“无感”。

目标：

- 单个 Shader 文档重新解析 < 5 ms；
- Completion 不执行 Workspace 全量扫描；
- 文档修改采用 debounce；
- Workspace Index 后台构建，但不阻塞输入；
- Schema 仅初始化读取一次；
- 不依赖启动外部进程完成普通补全。

避免：

- 每次按键读取 `Shader.h`；
- 每次按键扫描整个 `Engine/Res/Shaders`；
- 为简单 DSL 引入大型 Parser Runtime。

---

## 18. 兼容策略

### 阶段 A：插件先行

运行时不改：

```text
//info bein
Nevel
Cull none
```

插件：

- 全部识别；
- 新补全使用推荐格式；
- 只给轻提示。

### 阶段 B：运行时增加 Alias

让 C++ Parser 同时接受：

```text
info bein  -> info begin
Nevel      -> Never
none/None  -> Off
```

### 阶段 C：新文件全部生成规范格式

老 Shader 无需一次性迁移。

### 阶段 D：可选批量 Normalize

插件提供命令：

```text
Ailu Shader: Normalize Metadata
```

只处理明确无歧义的历史别名。

---

## 19. 推荐 VS Code Commands

第一版保留少量命令：

```text
Ailu Shader: Validate Current File
Ailu Shader: Normalize Metadata
Ailu Shader: Insert Pass
```

未来可以增加：

```text
Ailu Shader: Show Pass Variants
Ailu Shader: Rebuild Workspace Shader Index
```

不要一开始堆很多命令。

---

## 20. 实施阶段

### Phase 1 - MVP

实现：

- Extension 工程；
- Schema；
- Info / Pass parser；
- Key Completion；
- Enum Value Completion；
- 基础 Diagnostics；
- Pass Snippet。

完成后已经可以日常使用。

### Phase 2 - Shader Entry

实现：

- 当前文件 HLSL function symbol 扫描；
- `vert / pixel / geometry` Completion；
- Go To Definition；
- entry missing diagnostic。

### Phase 3 - Advanced Metadata

实现：

- Blend 参数 Completion；
- Stencil 子字段 Completion；
- ColorMask 校验；
- multi_compile 校验 / Hover；
- 丰富 Hover 文档。

### Phase 4 - DSL 统一

修改 C++ Parser：

- 支持 `//info begin`；
- 支持 `Never`；
- 明确定义 Cull Off；
- 明确定义 Queue Opaque；
- 去掉依赖 fallback 的隐式语义；
- 保留旧 Alias。

### Phase 5 - 单一 Schema

实现 Schema -> C++ CodeGen。

让：

```text
ShaderCommand / Parser
VS Code Extension
```

共享相同字段、枚举、Alias 定义。

---

## 21. 测试要求

### 21.1 Parser Test

至少覆盖：

- 单 Pass；
- 多 Pass；
- 缺失 pass end；
- 缺失 info end；
- 空 Pass；
- 重复字段；
- 大小写混用；
- Legacy marker。

### 21.2 Completion Test

覆盖：

```text
//C|
//Cull: |
//ZTest: |
//Blend: Src,|
//Stencil: {Ref:1,Comp:|
//vert: Full|
```

### 21.3 Diagnostics Test

覆盖：

```text
ZWirte
ZWrite: False
Cull: Left
pixel: MissingPS
重复 ZWrite
未闭合 pass
```

### 21.4 Repository Regression

将插件 validator 对：

```text
Engine/Res/Shaders/**/*.hlsl
```

跑一遍。

要求：

- 不应因为历史兼容格式产生大量 Error；
- 已知旧格式只能产生 Hint / Warning；
- 真正异常项输出清晰文件名与行号。

---

## 22. 验收标准

MVP 完成后满足：

1. 打开 AiluEngine `.hlsl` 后现有 HLSL IntelliSense 不受影响；
2. `//` 开始输入元数据时能得到字段补全；
3. `Cull / ZTest / ZWrite / Fill / Queue / Topology` 能得到正确值补全；
4. 非法 Key / Value 有实时红线或黄线；
5. `pass begin/end` 不匹配能定位到具体行；
6. `vert / pixel` 能补全当前文件函数；
7. Entry 可以 F12 跳转；
8. 老 Shader 无需修改即可正常编辑；
9. 插件不会替代或破坏 HLSL 插件；
10. 普通输入过程中无明显卡顿。

---

## 23. 开发原则

### 保持轻量

这是一个几十个 DSL 字段规模的问题，不要建设大型编译器基础设施。

### Schema 驱动

字符串规则必须集中，避免：

```text
C++ 一套
TypeScript 一套
文档再一套
```

### Parser 容错，编辑器严格提示

运行时可以为了兼容接受旧格式，但插件应始终推荐规范格式。

### 不破坏现有 HLSL 生态

Ailu Shader Tools 只增强 Ailu DSL。

### 优先解决高频错误

第一版最重要的是：

```text
字段补全
枚举补全
entry 补全
拼写错误诊断
结构错误诊断
```

不是语法高亮动画或复杂 UI。

---

## 24. 最终建议

本功能建议正式命名：

```text
Ailu Shader Tools
```

整体采取：

```text
VS Code TypeScript Extension
+ Lightweight Line Parser
+ Schema-driven Metadata
+ Existing HLSL Language Support
```

第一版不需要 LSP，也不需要完整 HLSL AST。

同时借此机会把 AiluEngine Shader Metadata DSL 从“运行时 Parser 能读懂的一组注释”提升为一套有明确 Schema、编辑器支持和兼容策略的小型正式 DSL。

这样未来继续增加：

```text
BlendOp
DepthClip
StencilWriteMask
StencilReadMask
ColorMask
Pass Tag
Render Queue
Shader Property Annotation
```

时，只需要扩展 Schema 和对应运行时映射，VS Code 的补全、Hover 和诊断即可同步获得支持，而不再依赖人工记忆。
