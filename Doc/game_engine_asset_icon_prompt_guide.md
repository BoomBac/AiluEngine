# 游戏引擎编辑器资产图标 AI 生图提示词规范

## 1. 目标

本文档用于为游戏引擎编辑器中的不同资产类型生成一套统一、现代、易于识别的图标，包括：

- Mesh
- Material
- Shader
- Texture
- Sprite
- Animation Clip
- Skeleton / Rig
- Prefab
- Scene
- Render Texture
- Audio Clip
- Script

整体视觉方向参考现代 iOS 系统图标设计：

- 圆润、克制、简洁
- 轻微立体感
- 柔和渐变
- 半透明玻璃与阳极氧化金属材质
- 单一主体
- 小尺寸下保持清晰可辨
- 不依赖文字表达资产类型

---

## 2. 提示词组织方式

推荐将提示词拆分为以下部分：

```text
用途
+ 核心视觉隐喻
+ 造型语言
+ 材质与灯光
+ 构图规范
+ 小尺寸可读性
+ 禁止内容
```

不要只写：

```text
iOS style mesh asset icon
```

这种写法容易导致：

- 每种图标的视角和光照不一致
- 图标有时像 App 图标，有时像工具栏图标
- 出现文字、文件夹、齿轮或界面截图
- 不同资产之间轮廓相似，辨识度不足
- 高分辨率下精致，但缩小后细节完全不可读

---

## 3. 统一视觉规范

| 项目 | 推荐规范 |
|---|---|
| 视觉风格 | 现代 iOS 系统图标、轻微立体、简洁 |
| 视角 | 轻微正交 3/4 视角 |
| 构图 | 单个主体、严格居中、紧凑轮廓 |
| 主体数量 | 不超过三个主要形状 |
| 背景 | 透明背景 |
| 材质 | 半透明玻璃、磨砂塑料、阳极氧化金属 |
| 灯光 | 左上方柔和摄影棚灯光 |
| 阴影 | 短而柔和的接触阴影 |
| 边缘 | 圆润、清晰、有足够厚度 |
| 色彩 | 每类资产一个主色，整体低饱和 |
| 细节密度 | 适合 24px、32px 和 64px |
| 文字 | 禁止文字、字母、数字和文件扩展名 |
| 透视 | 禁止强透视和夸张广角 |
| 装饰 | 禁止粒子、复杂背景和无关物体 |

---

## 4. 通用母提示词

将下面模板中的变量替换为对应资产内容即可。

```text
A premium game engine editor asset icon representing [ASSET TYPE].

The central symbol is [CLEAR VISUAL METAPHOR]. It must clearly communicate [CORE FUNCTION] without relying on text.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use [PRIMARY COLOR] as the main accent color with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no file extension, no interface screenshot, no border, no decorative
particles, no photorealism, no excessive details, no thin unreadable lines, no dramatic perspective.
```

---

## 5. 统一图标家族补充描述

在每个提示词末尾加入下面内容，可以提高系列一致性：

```text
This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

固定以下项目：

```text
same icon family
same camera angle
same lighting direction
same corner radius
same visual weight
same material language
same object scale
same amount of detail
```

---

## 6. 通用负面提示词

支持 Negative Prompt 的模型，可以使用：

```text
text, letters, numbers, logo, watermark, file extension, interface screenshot, toolbar, window frame, photorealistic,
real photograph, overly detailed, complex background, multiple unrelated objects, dramatic perspective, long shadow,
hard shadow, sharp corners, thin unreadable lines, excessive glow, neon cyberpunk, cartoon face, mascot, clutter,
border, frame, badge, bevel text, tiny details, dense wireframe, low contrast, ambiguous silhouette
```

---

## 7. 背景与版式选择

### 7.1 透明背景独立主体

适合游戏引擎资产浏览器。

```text
isolated icon object, true transparent background, clean alpha edges, no square background plate
```

优点：

- 可适配深色和浅色主题
- 更像资产图标，而不是 App 图标
- 方便编辑器自行绘制悬停和选中背景

### 7.2 半透明圆角底板

可以增强 iOS 风格，但不要做成完整 App 图标。

```text
a subtle translucent rounded backing plate behind the central object, occupying 78 percent of the canvas
```

### 7.3 完整圆角方形底板

更接近标准 iOS App 图标。

```text
contained inside a subtle rounded-square glass tile, consistent 18 percent corner radius, no outer border
```

对于游戏引擎编辑器，优先推荐：

```text
透明背景 + 主体后方非常淡的半透明几何底板
```

---

## 8. 小尺寸可读性要求

建议在所有提示词中加入：

```text
designed to remain recognizable at 24px, 32px and 64px
```

并限制：

```text
one dominant silhouette
no more than three major shapes
thick readable edges
high local contrast
minimal internal details
```

生成后建议检查：

- 128 × 128
- 64 × 64
- 32 × 32
- 24 × 24
- 16 × 16

16 × 16 通常需要单独简化或人工重绘，不建议仅依赖高分辨率缩放。

---

# 9. 各资产类型可直接使用的提示词

## 9.1 Mesh Asset

### 视觉隐喻

- 低多边形球体
- 清晰三角拓扑
- 少量高亮边
- 少量圆润顶点
- 强调可编辑的三维几何结构

### 推荐主色

```text
soft sapphire blue
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Mesh Asset.

The central symbol is a faceted low-poly sphere with clearly visible triangular topology, several emphasized edges
and a few rounded vertex points. It must clearly communicate editable polygonal 3D geometry without relying on text.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft sapphire blue as the main accent color with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no file extension, no interface screenshot, no border, no decorative
particles, no photorealism, no excessive details, no thin unreadable lines, no dramatic perspective.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.2 Material Asset

### 视觉隐喻

- 材质预览球
- 哑光、光泽、金属三种表面区域
- 强调反射、粗糙度和表面属性
- 不突出拓扑结构

### 推荐主色

```text
soft violet
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Material Asset.

The central symbol is a smooth material preview sphere divided into three elegant surface regions: matte, glossy
and metallic. The differences in roughness, reflectivity and surface response must be immediately visible. It must
clearly communicate configurable surface appearance rather than editable geometry.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft violet as the main accent color with low saturation and controlled highlights. Single dominant silhouette,
no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent visual
weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft contact
shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no file extension, no interface screenshot, no border, no decorative
particles, no photorealism, no excessive details, no visible polygon topology, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.3 Shader Asset

### 视觉隐喻

- 程序化表面球体
- 简化节点连接图案
- 一束光经过后改变表面明暗
- 不使用可读代码或终端窗口

### 推荐主色

```text
soft cyan and indigo
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Shader Asset.

The central symbol is a compact glowing procedural sphere with an abstract connected-node pattern embedded into its
surface and a subtle directional light beam transforming the shading across it. It must clearly communicate
programmable surface computation without using readable code or text.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft cyan and indigo as the main accent colors with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and restrained glow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no readable code, no terminal window, no logo, no file extension, no interface
screenshot, no border, no excessive glow, no neon cyberpunk style, no decorative particles, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.4 Texture Asset

### 视觉隐喻

- 圆角图像样片
- 简化表面纹理
- 翘起一角显示像素网格
- 不使用真实风景照片

### 推荐主色

```text
soft orange and coral
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Texture Asset.

The central symbol is a rounded square image tile containing a simplified colorful surface pattern, with a subtly
lifted corner revealing a clean pixel grid underneath. It must clearly communicate stored pixel image data used as
a surface input.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft orange and coral as the main accent colors with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no file extension, no landscape photograph, no interface screenshot,
no border, no decorative particles, no excessive details, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.5 Sprite Asset

### 视觉隐喻

- 2D 角色或对象剪影
- 棋盘格透明背景
- 裁切控制点
- 中央 Pivot 点
- 强调透明、裁切和二维用途

### 推荐主色

```text
soft teal and blue
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Sprite Asset.

The central symbol is a simple stylized 2D character silhouette placed on a translucent checkerboard tile, surrounded
by four minimal crop handles and one small central pivot point. It must clearly communicate a cropped transparent
2D image asset used for sprite rendering.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft teal and blue as the main accent colors with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no file extension, no interface screenshot, no border, no detailed
character face, no decorative particles, no photorealism, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.6 Animation Clip Asset

### 视觉隐喻

- 三个重叠动作姿态
- 简短时间轴
- 少量关键帧标记
- 不使用胶片卷轴

### 推荐主色

```text
soft green
```

### 完整提示词

```text
A premium game engine editor asset icon representing an Animation Clip Asset.

The central symbol is a simplified character silhouette shown in three overlapping motion poses, accompanied by a
subtle curved timeline and two compact keyframe markers. It must clearly communicate recorded motion over time.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft green as the main accent color with low saturation and controlled highlights. Single dominant silhouette,
no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent visual
weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft contact
shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no file extension, no film reel, no video player symbol, no interface
screenshot, no border, no decorative particles, no excessive motion blur, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.7 Skeleton / Rig Asset

### 视觉隐喻

- 抽象人体骨骼层级
- 圆形关节
- 圆润骨段
- 强调绑定结构
- 禁止骷髅头和恐怖元素

### 推荐主色

```text
soft amber
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Skeleton Rig Asset.

The central symbol is a clean simplified humanoid joint hierarchy constructed from rounded bone segments and
circular joints. It must be symmetrical, anatomically suggestive but highly abstract, clearly communicating a
character rig and bone hierarchy.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft amber as the main accent color with low saturation and controlled highlights. Single dominant silhouette,
no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent visual
weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft contact
shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no skull, no horror imagery, no anatomical realism, no file extension,
no interface screenshot, no border, no decorative particles, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.8 Prefab Asset

### 视觉隐喻

- 多个模块组合为一个整体
- 后方有轻微嵌套轮廓
- 强调可复用对象组合
- 不使用链条或文件夹

### 推荐主色

```text
soft blue violet
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Prefab Asset.

The central symbol is a compact assembled object made from three clean interlocking modular blocks, with a subtle
nested-object outline behind them. It must clearly communicate a reusable predefined object composition.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft blue violet as the main accent color with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no chain link, no folder, no file extension, no interface screenshot,
no border, no decorative particles, no excessive details, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.9 Scene Asset

### 视觉隐喻

- 微缩三维舞台
- 立方体和球体
- 小型灯光
- 简化摄像机
- 强调完整场景集合

### 推荐主色

```text
soft teal blue
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Scene Asset.

The central symbol is a miniature layered 3D stage containing one cube, one sphere, a small directional light and a
simplified camera arranged into a coherent scene composition. It must clearly communicate a complete collection of
objects, lighting and camera setup.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft teal blue as the main accent color with low saturation and controlled highlights. Single dominant silhouette,
no more than three major visual groups, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no landscape photograph, no file extension, no interface screenshot,
no border, no decorative particles, no excessive details, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.10 Render Texture Asset

### 视觉隐喻

- 简化摄像机
- 将图像投射到纹理平面
- 强调渲染目标而不是源图像

### 推荐主色

```text
soft bright cyan
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Render Texture Asset.

The central symbol is a simplified compact camera projecting a soft image beam onto a floating rounded texture tile.
It must clearly communicate a rendered image target generated by a camera rather than a source image file.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft bright cyan as the main accent color with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a restrained
projection glow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no monitor, no file extension, no interface screenshot, no border,
no decorative particles, no excessive glow, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.11 Audio Clip Asset

### 视觉隐喻

- 圆润声波带
- 小型扬声器振膜
- 不使用音符
- 强调音频波形数据

### 推荐主色

```text
soft magenta and violet
```

### 完整提示词

```text
A premium game engine editor asset icon representing an Audio Clip Asset.

The central symbol is a clean rounded waveform ribbon emerging from a compact circular speaker membrane. It must
clearly communicate stored audio waveform data.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft magenta and violet as the main accent colors with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No text, no letters, no numbers, no logo, no musical notes, no media player controls, no file extension,
no interface screenshot, no border, no decorative particles, no thin unreadable waveform lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

## 9.12 Script Asset

### 视觉隐喻

- 圆角文档样片
- 三条抽象代码线
- 小型组件符号
- 禁止可读代码和终端窗口

### 推荐主色

```text
soft cool gray and blue
```

### 完整提示词

```text
A premium game engine editor asset icon representing a Script Asset.

The central symbol is a rounded layered document tile containing three abstract horizontal code strokes and a small
modular component symbol. It must clearly communicate executable game logic without relying on readable code.

Designed as part of a unified professional icon family for a modern game engine editor. Inspired by modern iOS
system icon design: clean rounded geometry, restrained gradients, subtle translucent glass, soft anodized metal,
polished but minimal appearance.

Use soft cool gray and blue as the main accent colors with low saturation and controlled highlights. Single dominant
silhouette, no more than three major shapes, compact centered composition, slight orthographic 3/4 view, consistent
visual weight, rounded edges, soft studio lighting from the upper left, subtle ambient occlusion and a short soft
contact shadow.

The icon must remain immediately recognizable at 24px, 32px and 64px. Transparent background with clean alpha edges.
No readable text, no letters, no numbers, no logo, no terminal window, no file extension, no interface screenshot,
no border, no decorative particles, no excessive details, no thin unreadable lines.

This icon must belong to the same coherent icon family as the other game engine asset icons, using identical camera
angle, lighting direction, corner softness, object scale, material treatment and visual density.
```

---

# 10. 推荐颜色映射

| 资产类型 | 主色 |
|---|---|
| Mesh | 宝石蓝 |
| Material | 紫色 |
| Shader | 靛青 / 青色 |
| Texture | 橙色 / 珊瑚色 |
| Sprite | 青绿色 |
| Animation Clip | 绿色 |
| Skeleton / Rig | 琥珀黄 |
| Prefab | 蓝紫色 |
| Scene | 青蓝色 |
| Render Texture | 亮青色 |
| Audio Clip | 粉紫色 |
| Script | 灰蓝色 |
| Font | 红橙色 |
| Physics Material | 黄绿色 |
| UI Asset | 粉色 |

颜色只能辅助识别，不能成为不同资产类型之间的唯一差异。每种图标仍应拥有明确不同的主体轮廓。

---

# 11. 使用参考图保持系列一致

先生成一个最满意的基准图标，例如 Mesh Asset，再将它作为其他图标的风格参考。

参考图提示词：

```text
Use the attached icon only as the visual system reference. Preserve its camera angle, lighting, corner radius,
material quality, gradient softness, object scale and composition density. Replace the central object with
[NEW ASSET SYMBOL]. Do not copy the original object's semantic shape.
```

建议继承：

- 摄像机角度
- 主体占画布比例
- 灯光方向
- 圆角程度
- 材质表现
- 渐变强度
- 阴影长度
- 图标细节密度

不要继承：

- 原始图标的具体语义轮廓
- 原始主体结构
- 与新资产无关的装饰元素

---

# 12. 批量生成时的建议流程

1. 先生成 Mesh、Material、Shader 三种基础图标。
2. 确认三者在 32px 下可以快速区分。
3. 选择质量最好的一个作为系列风格参考图。
4. 后续图标统一使用相同参考图。
5. 每次只替换核心视觉隐喻和主色。
6. 每生成一个图标，都检查 64px、32px 和 24px 效果。
7. 删除缩小后不可见的细节。
8. 对 16px 图标单独制作简化版本。
9. 最终统一校正：
   - 主体大小
   - 视觉重心
   - 阴影强度
   - 色彩饱和度
   - 边缘清晰度
   - 透明边缘质量

---

# 13. 最重要的原则

不要让生图模型仅通过 `Mesh`、`Material` 或 `Shader` 这些名词自行猜测图标含义。

应明确描述：

- 中央主体是什么
- 需要保留哪些关键结构
- 它要表达什么资产能力
- 它不能与哪些资产混淆
- 小尺寸下需要依赖什么轮廓识别

例如 Mesh Asset 不应只写：

```text
a mesh icon
```

而应写：

```text
a faceted low-poly sphere with clearly visible triangular topology, several emphasized edges and a few rounded
vertex points, clearly communicating editable polygonal 3D geometry
```

核心原则：

> 使用具体、可见、可绘制的视觉元素描述资产类型，而不是只提供抽象资产名称。
