# RenderGraph Architecture

## Purpose

本文档定义 AiluEngine RenderGraph 的长期使用边界。

主要代码：

```text
Engine/Inc/Render/RenderGraph
Engine/Src/Render/RenderGraph
```

RenderGraph 是 rendering layer 的资源和 pass 调度系统，不是 GPU resource manager 的替代品。

## Responsibilities

RenderGraph 负责：

```text
pass declaration
resource import/create
read/write declaration
dependency construction
compile
resource lifetime within graph
resource state/barrier planning
execution ordering
graph validation/debug information
```

底层 GPU resource、descriptor、queue/fence 等仍由 GraphicsContext/RHI 管理。

## Resource model

RenderGraph 中必须区分：

### Imported resource

资源在 graph 外部已经存在。

Graph 只接管当前 graph execution 所需的访问/状态语义，不默认拥有其长期生命周期。

典型例子：

- persistent texture；
- frame resource；
- externally managed buffer；
- presentation/output target。

### Graph-created/transient resource

由 graph 描述创建并在 graph lifetime 内调度。

适合：

- intermediate render target；
- temporary buffer；
- pass-local working resource。

## Access declaration

Pass 在 record/setup 阶段应显式声明：

```text
Read
Write
ReadWrite when truly required
```

不要仅在 Execute 内部突然访问一个未声明资源。

资源访问声明是以下功能的依据：

- dependency；
- state transition；
- barrier；
- validation；
- compile cache；
- lifetime analysis。

## Resource identity

不要仅用资源名字假设 identity。

对于 imported resource：

```text
logical graph name
```

和：

```text
actual GpuResource pointer/handle
```

是不同概念。

同名 external resource 在不同 frame 可能重新绑定到不同实际对象。

因此 compile/cache 逻辑必须明确区分：

- graph topology/configuration 是否改变；
- external binding 是否改变；
- resource descriptor 是否改变。

## Compile and cache

RenderGraph compile optimization 的目标是避免 topology 没变化时重复执行高成本分析。

但不能因为“graph structure 没变”就跳过所有 per-frame binding/state work。

可缓存内容和每帧内容应分开：

```text
Topology / pass dependency
Resource logical lifetime
Static validation results
```

vs.

```text
Imported physical resource binding
Current resource state
Frame-specific resource handle
Dynamic dimensions/configuration when changed
```

具体 cache 规则以当前 RenderGraph 实现为准。

## Resource state and barriers

RenderGraph barrier 规划必须与底层 `GpuResource` / RHI state tracking 一致。

修改状态系统时至少考虑：

```text
whole resource vs subresource
graphics/compute/copy queue
initial state
imported resource state
read-after-write
write-after-read
write-after-write
UAV ordering
implicit/promotion rules if backend relies on them
```

不要在 RenderGraph 和 RHI 中各自维护互相矛盾的“真实状态”。

应明确谁负责：

```text
logical required state
```

以及谁负责：

```text
actual backend transition/submission
```

## Pass execution

RenderGraph pass 的 Execute/record code 应只依赖已经声明的数据和 context。

避免：

- 隐式读取另一个 pass 的 mutable global；
- 在 execute 时创建无法追踪的跨帧 resource；
- 通过名字查找未声明依赖；
- 在 graph 外偷偷执行影响 graph resource state 的 command。

## Static resource reads

长期静态纹理/缓冲也应通过显式 graph import/read 表达。

“它不会变化”不代表“它不需要 resource dependency/state declaration”。

## Validation

RenderGraph validation 应尽可能在 Debug 下捕获：

- undeclared access；
- invalid handle；
- read before producer/import；
- conflicting usage；
- missing/invalid resource binding；
- state mismatch。

不要为了绕过 validation 在 feature 中增加隐藏路径。

## Debug and image capture

需要验证某个 pass/resource 输出时，使用 repository render capture skill。

Capture 完成只证明数据被写出，不自动证明：

- capture 的是正确 resource；
- frame 连续；
- resource 内容正确；
- temporal behavior 正确。

## Change checklist

修改 RenderGraph 时检查：

- graph topology；
- imported binding；
- compile cache invalidation；
- descriptor/dimension changes；
- access declaration；
- barrier；
- subresource；
- frame resource；
- capture/debug tooling；
- multi-thread record path。
