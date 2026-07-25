**AiluEngine**

# ALMath.hpp 拆分执行文档

*面向 Claude 的可实施重构说明 · 保持 API 与数学行为优先*

文档版本：1.0
分析基线：BoomBac/AiluEngine，commit 33b8e38c93256ae22901fd0b8f23829ec131ce17
目标文件：Engine/Inc/Framework/Math/ALMath.hpp

*交付目的：直接交给 Claude 按阶段实施、编译、测试并提交变更*

## 文档使用说明

本文档是一份执行规范，不是仅供讨论的架构建议。实现者应按照阶段顺序操作，每个阶段完成后先编译和测试，再进入下一阶段。

| 项目 | 说明 |
| --- | --- |
| 实施目标 | 将约 3000 行的 ALMath.hpp 按职责拆分，降低公共头依赖、编译放大和模块耦合。 |
| 首要原则 | 第一阶段只移动代码并保持行为；不要同时重写矩阵约定、四元数语义或向量 API。 |
| 兼容策略 | 保留 ALMath.hpp 作为兼容聚合头；旧代码先不要求一次性修改。 |
| 主要限制 | Math 基础层不得依赖 Objects/Reflection、Render 或具体图形后端。 |
| 交付方式 | 建议分多个可编译提交：结构拆分、非数学职责迁移、精确 include、独立缺陷修复。 |

> 重要：如果仓库当前分支与上述 commit 存在差异，应以当前分支代码为准，但仍保持本文的依赖边界和迁移顺序。

## 目录

1. 重构目标与非目标

2. 当前问题与设计约束

3. 目标目录与依赖架构

4. 文件职责与代码迁移映射

5. 分阶段实施步骤

6. 具体实现要求

7. CMake、PCH 与 include 迁移

8. 测试与回归验证

9. 已发现但应独立修复的问题

10. 最终验收清单

附录 A：Claude 执行指令

## 1. 重构目标与非目标

### 1.1 重构目标

- 将 ALMath.hpp 从单一超大头文件拆分为职责明确、依赖单向的数学组件。
- 让仅使用 Vector2f、Vector3f 或 Color 的公共头不再解析矩阵求逆、四元数、投影、DCT、通用 Hash 和反射代码。
- 移除 Math 对 Objects/Reflection 的反向依赖。
- 将复杂、非模板、固定 float 类型的算法迁移到 .cpp，降低增量编译成本。
- 保留现有命名空间、类型名和主要函数签名，使迁移可以分阶段完成。
- 建立可自动检查的 include 规则，避免 ALMath.hpp 再次成为默认万能入口。

### 1.2 本次明确不做的事项

- 不改变矩阵的行主序/列主序约定、向量乘法顺序、左手/右手坐标系或 Reversed-Z 行为。
- 不在首个结构拆分提交中统一重命名所有历史 API，例如 Multipy、Determinat、Tanspose 等拼写。
- 不同时迁移到 C++20 Module；现有 core.math.aabb.ixx 暂不纳入本轮主线。
- 不引入第三方数学库替换现有实现。
- 不在结构迁移提交中顺便修复全部数学缺陷；缺陷应有独立测试和独立提交。

## 2. 当前问题与设计约束

### 2.1 当前职责混合

当前 ALMath.hpp 同时承载以下内容：

- 基础常量、对齐、NextPowOfTwo、角度转换和比较函数。
- Rect、Swizzle、Vector2D/3D/4D 及全部向量算法。
- 任意维度 Matrix 模板、矩阵运算、矩阵求逆和具体 Matrix4x4f 图形变换。
- Quaternion 以及 Quaternion 与 Matrix 的双向转换。
- View、Projection、LH/RH、Reversed-Z 等渲染约定相关函数。
- DCT/IDCT、Color、向量 Hash、通用 bitset Hash。
- 数学类型 StaticClass 特化声明。

### 2.2 必须建立的依赖边界

```text
Math 基础层允许依赖：
    GlobalMarco / 标准库

Math 基础层禁止依赖：
    Objects / Reflection
    Render / RHI / DX12
    Editor

Vector 不依赖 Matrix
Matrix 不依赖 Quaternion
Quaternion 不依赖 Matrix
QuaternionMatrix 可以同时依赖 Matrix 与 Quaternion
```

### 2.3 头文件实现规则

| 代码类别 | 放置位置 | 要求 |
| --- | --- | --- |
| 类模板、函数模板 | .hpp | 完整定义保留在头文件。 |
| constexpr 常量/短函数 | .h/.hpp | 使用 inline constexpr 或 constexpr。 |
| 固定 Matrix4x4f 的复杂算法 | .cpp | 头文件只保留声明。 |
| 内部辅助函数 | .cpp 匿名命名空间 | 不得用头文件 static 规避 ODR。 |
| 反射注册 | Objects 层 .h/.cpp | Math 文件不得 include ReflectTemplate.h。 |

## 3. 目标目录与依赖架构

### 3.1 推荐目录结构

```text
Engine/Inc/Framework/Math/
├── MathConstants.h
├── MathCommon.hpp
├── Vector.hpp
├── VectorMath.hpp
├── Matrix.hpp
├── MatrixMath.h
├── Quaternion.h
├── QuaternionMatrix.h
├── TransformMath.h
├── Projection.h
├── Rect.h
├── Color.h
├── MathHash.hpp
├── Geometry.h
└── ALMath.hpp

Engine/Src/Framework/Math/
├── MatrixMath.cpp
├── Quaternion.cpp
├── QuaternionMatrix.cpp
├── TransformMath.cpp
├── Projection.cpp
└── Geometry.cpp

Engine/Inc/Framework/Common/
└── Hash.hpp

Engine/Inc/Framework/Image/
└── DCT.h

Engine/Src/Framework/Image/
└── DCT.cpp

Engine/Inc/Objects/
└── MathTypeRegistration.h

Engine/Src/Objects/
└── MathTypeRegistration.cpp
```

### 3.2 目标依赖图

```text
GlobalMarco
    ├── MathConstants
    ├── MathCommon
    └── Vector
          ├── VectorMath
          ├── Rect
          ├── Color
          ├── Matrix
          │     ├── MatrixMath
          │     ├── TransformMath
          │     └── Projection
          ├── Quaternion
          ├── QuaternionMatrix
          ├── Geometry
          └── MathHash

Objects/MathTypeRegistration
    └── Vector / Matrix / Quaternion

Framework/Image/DCT
    └── Matrix
```

> Quaternion 与 Matrix 的转换必须集中在 QuaternionMatrix 中，避免 Matrix.hpp 与 Quaternion.h 互相包含。

## 4. 文件职责与代码迁移映射

### 4.1 核心文件职责

| 目标文件 | 主要内容 | 允许依赖 | 禁止内容 |
| --- | --- | --- | --- |
| MathConstants.h | kPi、kHalfPi、kTwoPi、kFloatEpsilon、角度换算常量 | GlobalMarco、<limits> | DCT 专用常量、宏覆盖 |
| MathCommon.hpp | AlignTo、NextPowOfTwo、标量 Clamp/NearbyEqual、ToRadians/ToDegrees | MathConstants、标准库 | Vector/Matrix/Reflection |
| Vector.hpp | Swizzle、Vector2D/3D/4D、别名、成员运算符、静态常量 | MathConstants、MathCommon | 矩阵、投影、DCT、反射 |
| VectorMath.hpp | Normalize、Distance、Dot、Cross、Min/Max、Floor/Ceil/Round 等 | Vector.hpp | Matrix4x4f 专用逻辑 |
| Matrix.hpp | Matrix 模板、Matrix3x3f/4x4f、基础运算和转置 | Vector.hpp、VectorMath.hpp | Quaternion、View/Projection |
| MatrixMath.h/.cpp | Determinant、Inverse、InverseTranspose、固定矩阵复杂算法 | Matrix.hpp | 相机投影、反射 |
| Quaternion.h/.cpp | Quaternion 数据与纯四元数算法 | Vector.hpp、VectorMath.hpp | Matrix.hpp |
| QuaternionMatrix.h/.cpp | QuaternionToMatrix、MatrixToQuaternion、DecomposeMatrix | Matrix.hpp、Quaternion.h | 业务层逻辑 |
| TransformMath.h/.cpp | TransformVector/Coord/Normal、平移/旋转/缩放矩阵构造 | Matrix.hpp、Vector.hpp | View/Projection |
| Projection.h/.cpp | LookAt/LookTo、Perspective、Orthographic、Reverse-Z | Matrix.hpp、VectorMath.hpp | 通用向量定义 |
| Rect.h | 通用 Rect 或渲染 Scissor 类型 | GlobalMarco | 矩阵、Quaternion |
| Color.h | Color、Color32、Colors 常量 | Vector.hpp | DCT、Hash |
| MathHash.hpp | VectorHash、VectorEqual、HashCombine | Vector.hpp、<functional> | 通用 bitset Hash |
| Common/Hash.hpp | template<u8 Size> Hash 及通用 Hasher | GlobalMarco、<bitset> | 数学向量专用逻辑 |
| Image/DCT.h/.cpp | DCT8X8、IDCT8X8 和 DCT 辅助常量 | Matrix.hpp | 基础数学聚合入口 |
| Objects/MathTypeRegistration | StaticClass<MathType> 特化声明与实现 | Math 类型、Objects/Type | 被 Math 反向 include |

### 4.2 ALMath.hpp 最终形态

ALMath.hpp 保留为兼容聚合头，但不得继续承载实现，也不得包含反射、DCT 或通用 Hash。

```cpp
#pragma once

#include "Framework/Math/Color.h"
#include "Framework/Math/MathCommon.hpp"
#include "Framework/Math/MathConstants.h"
#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/MatrixMath.h"
#include "Framework/Math/Projection.h"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/QuaternionMatrix.h"
#include "Framework/Math/Rect.h"
#include "Framework/Math/TransformMath.h"
#include "Framework/Math/Vector.hpp"
#include "Framework/Math/VectorMath.hpp"
```

### 4.3 符号迁移映射

| 原 ALMath.hpp 内容 | 迁移目标 |
| --- | --- |
| LIKELY / UNLIKELY | Framework/Common/Compiler.h 或 Platform.h；不保留在 Math。 |
| FLT_EPSILON 宏覆盖 | 删除；调用点改用 Math::kFloatEpsilon 或 numeric_limits。 |
| CountOf | 优先使用 std::size；过渡期放 MathCommon.hpp。 |
| NormalizeScaleFactor、one_over_four、Pi_over_sixteen | Image/DCT.cpp 内部。 |
| Rect / ScissorRect | Rect.h；若仅用于硬件裁剪，则 ScissorRect 移到 RenderTypes。 |
| Swizzle、Vector2D/3D/4D | Vector.hpp。 |
| 向量数学函数与通用向量运算符 | VectorMath.hpp。 |
| Matrix 模板与基础模板运算 | Matrix.hpp。 |
| 矩阵逆、行列式等复杂函数 | MatrixMath.cpp。 |
| TransformVector/Coord/Normal、TRS 构造 | TransformMath.cpp。 |
| View/Projection/Reversed-Z | Projection.cpp。 |
| Quaternion 纯算法 | Quaternion.cpp。 |
| Quaternion 与 Matrix 转换、矩阵分解 | QuaternionMatrix.cpp。 |
| DCT8X8 / IDCT8X8 | Framework/Image/DCT.cpp。 |
| Color / Colors | Color.h。 |
| Vector Hash/Equal | MathHash.hpp。 |
| 通用 Hash<Size> | Framework/Common/Hash.hpp。 |
| StaticClass<MathType> | Objects/MathTypeRegistration.h/.cpp。 |

## 5. 分阶段实施步骤

### 阶段 0：建立基线与保护测试

1. 确认当前分支可完整编译，并记录构建命令、配置和已有测试结果。
1. 记录 ALMath.hpp 的直接 include 数量；保留列表用于后续逐步替换。
1. 为核心行为补充最小测试：向量运算、矩阵乘法、矩阵逆、Quaternion 转换、Projection、DecomposeMatrix。
1. 确认 _REVERSED_Z、_SIMD 等条件编译配置至少覆盖当前主用组合。

> 阶段 0 不修改生产实现。没有基线测试时，不允许开始大范围移动代码。

### 阶段 1：纯结构拆分，保持行为

1. 创建 MathConstants.h、MathCommon.hpp、Vector.hpp、VectorMath.hpp、Matrix.hpp。
1. 按原代码顺序移动对应模板和函数，尽量不修改函数体。
1. 创建 Quaternion.h、QuaternionMatrix.h、TransformMath.h、Projection.h 及对应 .cpp。
1. 将固定 Matrix4x4f 的复杂普通函数迁移到 .cpp；模板继续保留在 .hpp。
1. 将 ALMath.hpp 改为聚合头，确保所有旧 include 仍可编译。
1. 更新 CMake 源文件列表并执行全量编译和测试。

**阶段 1 完成条件：**

- 旧调用点无需修改即可编译。
- 公开类型名和命名空间不变。
- 所有基线测试结果与拆分前一致。
- ALMath.hpp 中只剩 include。

### 阶段 2：迁移非数学职责并纠正依赖方向

1. 把 StaticClass<MathType> 特化声明和实现迁移到 Objects/MathTypeRegistration。
1. 从所有 Math 头文件删除 Objects/ReflectTemplate.h。
1. 把 DCT/IDCT 迁移到 Framework/Image。
1. 把通用 Hash<Size> 迁移到 Framework/Common/Hash.hpp。
1. 把 Vector Hash/Equal 保留在 MathHash.hpp。
1. 把 LIKELY/UNLIKELY 迁移到 Compiler/Platform 层。
1. 删除对 FLT_EPSILON 的 #undef/#define，并替换所有调用点。

### 阶段 3：精确 include 迁移

优先修改公共头文件，因为公共头的传递依赖会放大编译成本。建议按以下顺序处理：

1. Engine/Inc/UI：UIStyle.h、UISlot.h、UIElement.h、UIRenderer.h。
1. Engine/Inc/Render：Buffer.h、Texture.h、Mesh.h、Shader.h、PipelineState.h、Camera.h。
1. Engine/Inc/Animation：Skeleton.h、Pose.h、Curve.hpp、TransformTrack.h。
1. Engine/Inc/Physics、Scene、RHI 和 Editor 公共头。
1. 最后处理 .cpp 文件和测试代码。

**常见替换方式：**

```cpp
// 仅存储 Vector3f
#include "Framework/Math/Vector.hpp"

// 使用 Normalize、DotProduct、CrossProduct
#include "Framework/Math/Vector.hpp"
#include "Framework/Math/VectorMath.hpp"

// 使用 Matrix4x4f 及 TRS
#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/TransformMath.h"

// Camera / Projection
#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/Projection.h"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/Vector.hpp"
```

### 阶段 4：收紧规则并清理兼容入口

1. 新增 lint/脚本：Engine/Inc 下禁止直接 include ALMath.hpp。
1. Engine/Src 可暂时允许 ALMath.hpp，随后按需继续收敛。
1. PCH 是否包含 ALMath.hpp 应基于编译分析决定；即使 PCH 包含，公共头仍应精确 include。
1. 删除不再使用的头文件 include、重复宏和历史注释。
1. 记录拆分前后全量构建和增量构建耗时。

### 阶段 5：独立缺陷修复

仅在结构迁移稳定后处理第 9 节列出的数学缺陷。每个缺陷必须有独立测试，建议单独提交。

## 6. 具体实现要求

### 6.1 命名和 API 兼容

- 现有 Vector2D/Vector3D/Vector4D、Matrix、Quaternion 类型名保持不变。
- 现有 Ailu::Math 命名空间保持不变。
- 成员变量继续使用前导下划线；局部变量使用小写和下划线。
- 新增函数使用驼峰命名；常量使用 k 前缀加驼峰命名。
- 首轮拆分不批量修正历史拼写；若必须提供新名称，先保留旧名称转发。

### 6.2 常量和宏处理

```cpp
namespace Ailu::Math
{
    inline constexpr f32 kEpsilon = 1.19209e-07f;
    inline constexpr f32 kPi = 3.14159265358979323846f;
    inline constexpr f32 kHalfPi = kPi * 0.5f;
    inline constexpr f32 kTwoPi = kPi * 2.0f;
    inline constexpr f32 kFloatEpsilon = 1e-6f;
    inline constexpr f32 kRadToDeg = 180.0f / kPi;
    inline constexpr f32 kDegToRad = kPi / 180.0f;
}
```

- 不得重新定义 FLT_EPSILON。
- 浮点近似比较使用 kFloatEpsilon；机器精度使用 std::numeric_limits<T>::epsilon()。
- DCT 专用常量放在 DCT.cpp 匿名命名空间。
- LIKELY/UNLIKELY 不属于 MathConstants。

### 6.3 Matrix 与 Quaternion 解耦

```cpp
// QuaternionMatrix.h
#pragma once

#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/Quaternion.h"

namespace Ailu::Math
{
    Matrix4x4f QuaternionToMatrix(const Quaternion &quaternion);
    Quaternion MatrixToQuaternion(const Matrix4x4f &matrix);
    void DecomposeMatrix(const Matrix4x4f &matrix, Vector3f &translation, Quaternion &rotation, Vector3f &scale);
    Matrix4x4f MatrixRotationQuaternion(const Quaternion &quaternion);
}
```

处理方式：

- Quaternion.h 中不包含 Matrix.hpp。
- Matrix.hpp 中不包含 Quaternion.h。
- 原 Quaternion::ToMat4f 和 Quaternion::FromMat4f 可暂时保留声明，并在 QuaternionMatrix.cpp 中转发；后续再评估是否弃用成员形式。
- DecomposeMatrix 和 MatrixRotationQuaternion 放入桥接文件。

### 6.4 Rect 与 ScissorRect

实施者应先搜索 Rect 和 ScissorRect 的全部使用点，然后选择以下方案之一：

| 方案 | 适用条件 | 处理方式 |
| --- | --- | --- |
| 通用 Rect | UI、布局和渲染均使用 | 实现 Rect<T> 或保留整数 Rect，并将 UI 浮点矩形单独定义。 |
| 渲染专用 ScissorRect | 当前 Rect 仅服务 RHI/Render | 移动到 RenderTypes.h，Math 不再包含。 |

### 6.5 Geometry.h 同步调整

Geometry.h 已经独立存在，但目前包含完整 ALMath.hpp。拆分后改为精确依赖：

```cpp
#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/Vector.hpp"
#include "Framework/Math/VectorMath.hpp"
```

- Plane、Ray、AABB、OBB、Sphere 等类型继续保留在 Geometry.h。
- 复杂相交和变换实现尽量迁移到 Geometry.cpp。
- 本轮不把 Geometry 同时迁移为 C++ Module。

### 6.6 反射注册迁移

```cpp
// Objects/MathTypeRegistration.h
#pragma once

#include "Framework/Math/Vector.hpp"
#include "Objects/ReflectTemplate.h"

namespace Ailu
{
    template<> AILU_API Type *StaticClass<Math::Vector2f>();
    template<> AILU_API Type *StaticClass<Math::Vector3f>();
    template<> AILU_API Type *StaticClass<Math::Vector4f>();
    // 其余类型保持现有列表
}
```

需要显式包含 MathTypeRegistration.h 的位置：

- 依赖 StaticClass<MathType>() 的反射注册源文件。
- 序列化或编辑器属性系统中真正调用该特化的位置。
- 不要为了“方便”重新从 Vector.hpp 或 ALMath.hpp 间接包含。

## 7. CMake、PCH 与 include 迁移

### 7.1 CMake 更新

- 将新增 .cpp 文件加入 Engine 目标源列表。
- 头文件可加入 IDE source_group，但不依赖 header-only 自动发现。
- 若项目使用 GLOB，仍需确认新增目录会被纳入，并触发 CMake configure。
- 保持现有导出宏 AILU_API 的边界；跨 DLL 的非模板函数声明应带 AILU_API。

### 7.2 PCH 策略

PCH 可以包含稳定且广泛使用的数学基础，但不能替代显式依赖。建议：

- PCH 最多包含 MathConstants.h、MathCommon.hpp、Vector.hpp；是否包含 Matrix.hpp 应基于编译统计决定。
- 不要把 Projection、QuaternionMatrix、DCT、MathTypeRegistration 放入 PCH。
- 所有公共头必须在关闭 PCH 时仍能独立编译。

### 7.3 独立头编译检查

```cpp
// 示例：为每个公共头生成最小编译单元
#include "Framework/Math/Vector.hpp"
int main() { return 0; }

#include "Framework/Math/Matrix.hpp"
int main() { return 0; }
```

建议建立一个 HeaderCompileTest 目标，至少覆盖所有新增公共头，防止依赖被 PCH 或包含顺序掩盖。

## 8. 测试与回归验证

### 8.1 必须覆盖的测试

| 测试组 | 测试内容 | 关键断言 |
| --- | --- | --- |
| Vector | 构造、运算符、Normalize、Dot、Cross、Distance、Min/Max | 结果与拆分前一致；零向量行为保持。 |
| Matrix | Identity、加减乘、Transpose、Determinant、Inverse | M * Inverse(M) 近似 Identity。 |
| Transform | 平移、旋转、缩放、TransformCoord/Normal | 点 w=1，法线 w=0；方向和坐标约定不变。 |
| Quaternion | 乘法、AngleAxis、Euler、FromTo、LookRotation、SLerp | 归一化和方向等价行为保持。 |
| Bridge | QuaternionToMatrix、MatrixToQuaternion、DecomposeMatrix | TRS 往返误差在容差内。 |
| Projection | LH/RH、Perspective、Orthographic、Reversed-Z | 近远平面映射与旧实现一致。 |
| Hash | VectorHash、VectorEqual、Hash<Size> | 迁移前后哈希行为一致，除已确认缺陷外。 |
| Build | 无 PCH 公共头编译、Debug/Release、SIMD 开关 | 所有配置可编译。 |

### 8.2 构建验证矩阵

| 配置 | PCH | SIMD | 要求 |
| --- | --- | --- | --- |
| Debug | 开 | 当前默认 | 完整 Engine + Editor 编译。 |
| Release | 开 | 当前默认 | 完整 Engine + Editor 编译。 |
| Debug | 关 | 当前默认 | 至少 HeaderCompileTest + MathTest。 |
| Debug | 开 | 关闭 | 验证非 SIMD 路径。 |

### 8.3 性能指标

- 记录修改 Vector.hpp 后触发的重编译文件数。
- 记录修改 Projection.cpp 后触发的重编译文件数；理想情况下仅重编译 Projection.cpp 及链接目标。
- 对比全量构建时间和典型增量构建时间。
- 对比公共头预处理体积，可使用 MSVC /showIncludes 或 clang -ftime-trace 辅助分析。

## 9. 已发现但应独立修复的问题

以下问题在当前代码中疑似存在。结构拆分时应保持原行为，先记录并添加测试；完成结构迁移后再单独修复。

| 问题 | 当前表现 | 建议修复 | 提交策略 |
| --- | --- | --- | --- |
| R32G32B32A32Float 别名 | 当前疑似指向 Vector3D<float> | 改为 Vector4D<float> | 独立提交 + sizeof/分量测试 |
| Quaternion(Vector3f, float) | 构造函数接收 s，但疑似使用 w | 使用传入的 s | 独立提交 + 构造测试 |
| VectorEqual | 循环疑似从索引 1 开始，忽略 x 分量 | 从索引 0 开始 | 独立提交 + unordered 容器测试 |
| Angle | 疑似调用 Radian(a.b) | 改为 Radian(a, b) | 独立提交 + 夹角测试 |
| Matrix/Quaternion 若干拼写 | Multipy、Determinat、Tanspose | 新增正确名称并保留旧名称转发 | 可单独兼容性提交 |
| FLT_EPSILON 覆盖 | 覆盖标准宏并改变所有包含者环境 | 彻底删除，改用引擎常量 | 阶段 2 单独提交 |

> 不要在“移动代码”的提交中悄悄修复这些问题，否则无法判断回归来自文件拆分还是数学行为变化。

## 10. 最终验收清单

- [ ] ALMath.hpp 只包含聚合 include，不再包含类型或函数实现。
- [ ] Framework/Math 下没有任何头文件 include Objects/ReflectTemplate.h 或 Objects/Type.h。
- [ ] Vector.hpp 不包含 Matrix.hpp；Matrix.hpp 不包含 Quaternion.h；Quaternion.h 不包含 Matrix.hpp。
- [ ] Quaternion/Matrix 双向转换集中在 QuaternionMatrix。
- [ ] DCT/IDCT 已从基础数学头移出。
- [ ] 通用 Hash<Size> 已从 Math 移到 Framework/Common。
- [ ] 不再覆盖 FLT_EPSILON。
- [ ] 复杂固定类型函数已进入 .cpp，公共头不再大量使用 static 普通函数。
- [ ] Geometry.h 使用精确 include。
- [ ] 新增公共头在关闭 PCH 时可以独立编译。
- [ ] Debug/Release 构建通过，数学测试通过。
- [ ] 旧代码仍可通过 ALMath.hpp 聚合头编译。
- [ ] Engine/Inc 中新增代码不再直接 include ALMath.hpp。
- [ ] 所有缺陷修复与结构拆分分开提交。
- [ ] 提交说明包含文件迁移表、行为兼容说明和测试结果。

## 附录 A：Claude 执行指令

可将以下内容与本文档一起交给 Claude，作为实施约束：

```text
请严格按照本文档分阶段重构 AiluEngine 的 ALMath.hpp。

执行要求：
1. 先检查当前分支代码和 CMake 结构，不要假定文件内容与文档完全一致。
2. 第一阶段只做结构拆分，保持现有 API、命名空间、矩阵约定和数学行为。
3. 保留 ALMath.hpp 作为兼容聚合头，确保旧调用点暂时无需修改。
4. Math 基础头不得依赖 Objects/Reflection、Render、RHI 或 Editor。
5. Vector.hpp、Matrix.hpp、Quaternion.h 必须保持单向依赖；矩阵与四元数转换放到 QuaternionMatrix。
6. 模板保留在 .hpp；复杂固定类型算法移动到 .cpp。
7. 每完成一个阶段立即编译并运行测试；失败时先修复当前阶段，不要继续堆叠修改。
8. 不要在结构拆分提交中顺便修复第 9 节的数学缺陷。
9. 应用项目代码风格：局部变量小写下划线，成员变量以下划线开头，常量 k 前缀驼峰，类型和函数使用驼峰命名，代码按 120 列格式化。
10. 最终输出：变更文件列表、依赖变化、构建结果、测试结果、仍未处理的问题。
```

### 建议提交拆分

| 提交 | 范围 | 要求 |
| --- | --- | --- |
| 1. math header split | 常量、Vector、Matrix、Quaternion、Projection、聚合头 | 仅移动和必要的链接修正。 |
| 2. decouple reflection and utilities | 反射、DCT、通用 Hash、宏 | 纠正模块依赖方向。 |
| 3. narrow includes | 公共头和源文件精确 include | 每批修改后编译。 |
| 4. math correctness fixes | 第 9 节缺陷 | 每个修复带测试。 |

### 实现完成后的汇报模板

```text
实施结果
- 完成阶段：
- 新增文件：
- 移动的主要符号：
- 删除的反向依赖：
- 仍保留 ALMath.hpp 的调用点：

验证结果
- Debug 构建：
- Release 构建：
- 无 PCH 头文件编译：
- 数学测试：
- 增量编译对比：

未处理事项
- 独立数学缺陷：
- 仍需精确 include 的文件：
- 可能的兼容风险：
```

## 参考代码基线

**仓库：**BoomBac/AiluEngine
**Commit：**33b8e38c93256ae22901fd0b8f23829ec131ce17
**核心文件：**Engine/Inc/Framework/Math/ALMath.hpp
**相关文件：**Engine/Src/Framework/Math/ALMath.cpp、Engine/Inc/Framework/Math/Geometry.h

*说明：本次会话未能从文件库直接恢复此前上传的 Engine.zip，因此文档以已连接 GitHub 仓库的上述代码快照为分析基线。若压缩包内代码更新，应由实施者先比对差异，再沿用本文依赖规则。*
