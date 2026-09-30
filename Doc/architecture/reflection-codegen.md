# Reflection and Code Generation

## Purpose

本文档描述 AiluEngine 当前 reflection/code-generation 的基本工作方式和修改规则。

主要入口：

- `AiluHeadTool`
- `AiluHeadTool/aht.ini`
- `Engine/Inc/Framework/Core/ReflectionMacros.h`
- `Engine/Inc/Objects`
- `*/generated/*.gen.h`
- `*/generated/*.gen.cpp`
- root `CMakeLists.txt`

## Input roots

当前 `AiluHeadTool/aht.ini` 配置扫描：

```text
Engine/Inc
Editor/Inc
```

CMake 也会扫描包含：

```text
#include "generated/<name>.gen.h"
```

的 header，以收集 reflection input/output。

## Reflection markers

仓库当前使用的宏包括：

```text
ACLASS
ASTRUCT
AENUM
APROPERTY
GENERATED_BODY
```

这些宏驱动类型、属性、serialization/editor metadata 等生成逻辑。

新增 reflected type 时优先复制当前相同类型的成熟用法，不要凭空设计一套新的 metadata 语法。

## Generated files

典型输出：

```text
Engine/Inc/.../generated/X.gen.h
Engine/Inc/.../generated/X.gen.cpp
Editor/Inc/.../generated/X.gen.h
Editor/Inc/.../generated/X.gen.cpp
```

当前 generated files 位于源码树，并参与 build。

### Rule

除非任务本身是在修改 generator：

**不要手工修改 generated files。**

应修改：

```text
source header
→ run normal codegen/build path
→ inspect generated diff
```

## CMake integration

Root CMake 当前：

- 收集 reflection input headers；
- 计算 generated output；
- 定义 `ailu_reflection_codegen`；
- 使用 build-tree stamp，避免 clean 删除 source-tree generated files；
- Engine/Editor target 依赖 codegen。

因此 generated file 的存在与 CMake dependency 是构建链的一部分。

修改 codegen 时需要检查：

- clean build；
- incremental build；
- header change；
- no-op rebuild；
- generated files 不被 CMake clean 误删。

## Serialization impact

Reflection 不是单纯 Editor UI 功能。

修改 reflected field/type 时可能影响：

- JSON archive；
- asset document；
- scene；
- component；
- inspector；
- old assets；
- generated registration。

字段重命名/删除尤其需要考虑 migration，而不是只让新代码编译。

## Type registration and Object

`Object` 是核心 reflected runtime base type之一。

`ObjectRegister` 还维护 runtime object register/reference 信息。

不要将 reflection type identity、runtime Object ID、asset GUID、entity GUID 混为同一种 identity。

它们服务于不同生命周期。

## Generated-code review

修改 reflection header 后，generated diff 也应被视为 review 内容。

检查：

- 是否生成预期类型；
- property 名称；
- namespace；
- serializer；
- include；
- duplicate registration；
- unexpected full-regeneration。

## AiluHeadTool changes

修改 generator 本身时优先创建独立 ExecPlan。

至少验证：

```text
AiluHeadTool build
reflection generation
Engine build-debug
Editor build-debug
no-op second build
representative serialization
```

不要在普通 gameplay/rendering 任务中顺手重写 generator。

## Current limitations

Reflection/codegen 与 source tree、CMake、serialization 有较紧密耦合。

在进行未来 module/plugin boundary 设计时，需要明确：

- plugin reflected type 是否需要 build-time generation；
- registration 何时发生；
- DLL unload 前如何 unregister；
- generated code 是否跨 DLL；
- Type 指针生命周期。

这些属于 plugin architecture 任务，不是当前系统已经解决的事实。
