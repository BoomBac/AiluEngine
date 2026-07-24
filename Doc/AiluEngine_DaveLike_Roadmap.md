# AiluEngine《潜水员戴夫》类项目开发路线图

> 目标：将当前偏渲染技术型的 AiluEngine，补齐为能够高效制作“潜水探索 + 捕获战斗 + 餐厅经营 + 剧情任务”项目的完整开发环境。  
> 范围：仅覆盖引擎运行时、编辑器与内容生产工具；暂不讨论打包、商店发布和平台部署。

---

## 1. 总体原则

当前引擎已经具备较好的渲染、资源、场景、反射序列化、ECS、基础 UI 和动画框架。下一阶段不应继续优先扩展高级渲染，而应建立完整的游戏内容生产链路：

```text
DataAsset
    ↓
Prefab
    ↓
Entity + Components
    ↓
Gameplay System / Lua
    ↓
Animation + Audio + VFX
    ↓
UI + Dialogue
    ↓
SaveGame
```

开发过程中遵循以下原则：

1. 先修复底层硬阻塞，再开始批量制作玩法内容。
2. 先完成一个闭环垂直切片，再扩展通用性。
3. 优先实现 2D/2.5D 项目真正需要的能力。
4. 不追求一次性做成 Unity 或 Unreal 级别的通用系统。
5. 每个阶段都必须有可验证的验收标准。

---

# 2. 开发阶段总览

| 阶段 | 目标 | 优先级 |
|---|---|---:|
| Phase 0 | 修复底层硬阻塞 | P0 |
| Phase 1 | 建立基础运行时服务 | P0 |
| Phase 2 | 建立 2D/2.5D 游戏基础 | P0 |
| Phase 3 | 建立内容生产与数据驱动能力 | P0 |
| Phase 4 | 建立潜水玩法闭环 | P1 |
| Phase 5 | 建立餐厅经营闭环 | P1 |
| Phase 6 | 建立剧情、任务和教程系统 | P1 |
| Phase 7 | 完成垂直切片并验证架构 | P0 |
| Phase 8 | 性能、调试和内容生产优化 | P2 |

---

# 3. Phase 0：修复底层硬阻塞

本阶段完成前，不建议大规模编写玩法或制作内容。

## 3.1 ECS 扩容与实体安全

### 当前问题

- 最大实体数量只有 200。
- Entity 缺少 generation。
- 删除后复用实体 ID，旧引用可能错误指向新实体。
- Component Signature 数量较小。
- 实体耗尽时缺少安全检查。
- 运行时 Entity ID 无法直接用于存档引用。

### 任务

- [ ] 将固定 200 实体改为动态容量。
- [ ] Entity 改为 `index + generation`。
- [ ] 增加实体有效性校验。
- [ ] 所有组件访问前验证 Entity generation。
- [ ] 实体池耗尽时返回明确错误。
- [ ] Signature 扩展至至少 64/128 位，或改为动态类型集合。
- [ ] 增加 `PersistentIdComponent`。
- [ ] 区分 Runtime Entity ID 与 Persistent GUID。
- [ ] 增加批量创建和批量销毁接口。
- [ ] 增加延迟销毁队列，避免系统遍历期间修改容器。

### 验收标准

- 可稳定创建和销毁至少 10,000 个实体。
- 销毁实体后，旧 Entity Handle 必须失效。
- 场景保存和加载后，Persistent GUID 保持一致。
- 不允许因为实体池耗尽发生未定义行为。

---

## 3.2 时间系统统一

### 当前问题

- 不同系统对 `delta_time` 单位理解不一致。
- 某些系统将秒再次乘以 `0.001f`。
- 固定更新逻辑未形成稳定闭环。
- 部分 Layer 更新使用固定 `1.0f`。

### 任务

- [ ] 全引擎统一使用秒作为时间单位。
- [ ] 明确 `Update(f32 delta_time_sec)`。
- [ ] 实现 `FixedUpdate(f32 fixed_delta_time_sec)`。
- [ ] 实现 `LateUpdate(f32 delta_time_sec)`。
- [ ] 增加最大补帧次数。
- [ ] 增加 scaled time。
- [ ] 增加 unscaled time。
- [ ] 增加暂停状态。
- [ ] 增加 time scale。
- [ ] 清理所有重复 `* 0.001f` 或 `/ 1000.0f` 的错误逻辑。
- [ ] 增加帧号、固定帧号和总运行时间。

### 推荐接口

```cpp
class Time
{
public:
    static f32 DeltaTime();
    static f32 UnscaledDeltaTime();
    static f32 FixedDeltaTime();
    static f64 ElapsedTime();
    static u64 FrameIndex();
    static u64 FixedFrameIndex();
};
```

### 验收标准

- 30、60、120 FPS 下角色移动距离基本一致。
- 暂停时 Gameplay 停止，但 UI 动画可以继续运行。
- 物理和动画不再依赖不明确的毫秒/秒转换。

---

## 3.3 修复 Sprite 多批次实例偏移

### 当前问题

Sprite Batch 保存了实例偏移，但绘制命令没有将其传递给 DX12 的 `start_instance_location`。

### 任务

- [ ] 在 Draw Command 中增加 `_start_instance`。
- [ ] 修改 `DrawIndexedInstanced` 接口。
- [ ] DX12 后端传入 `start_instance_location`。
- [ ] 增加多材质、多纹理 Sprite Batch 测试。
- [ ] 检查间接绘制路径是否也需要实例偏移。

### 验收标准

- 同一帧绘制多个 Sprite Batch 时，各批次读取正确的实例数据。
- 多纹理、多材质排序后不会重复显示第一批 Sprite。

---

## 3.4 修复基础物理明显错误

### 当前问题

- 自身碰撞判断使用 `return`，可能提前结束整个检测。
- `delta_time` 单位可能错误。
- 当前碰撞响应仅接近 Y 轴弹力实验。
- 缺少碰撞层、Trigger 事件和空间加速结构。

### 任务

- [ ] 将自身碰撞的 `return` 修改为 `continue`。
- [ ] 修正物理时间单位。
- [ ] 为旧物理系统标记 Experimental。
- [ ] 停止继续扩展当前通用 3D 刚体实现。
- [ ] 后续由轻量 2D/2.5D Kinematic World 替代项目核心碰撞需求。

### 验收标准

- 现有测试场景不再因提前退出漏检碰撞。
- 不再出现重复缩放 `delta_time` 的问题。

---

# 4. Phase 1：基础运行时服务

## 4.1 Input Action 系统

### 目标

将当前直接读取 Win32 键码的方式，升级为设备无关的动作输入系统。

### 核心类型

```text
InputDevice
InputAction
InputBinding
InputActionMap
InputContext
InputInteraction
```

### 任务

- [ ] 定义 Button、Axis1D、Axis2D Action。
- [ ] 支持键盘。
- [ ] 支持鼠标。
- [ ] 支持 XInput 手柄。
- [ ] 支持摇杆死区。
- [ ] 支持按键重映射。
- [ ] 支持点击、长按、按住、释放等 Interaction。
- [ ] 支持 Gameplay 与 UI Action Map。
- [ ] 支持输入上下文切换。
- [ ] 支持当前输入设备检测。
- [ ] 支持按键提示图标动态切换。
- [ ] 支持输入配置保存。

### 建议 Action

```text
Gameplay
  Move
  Aim
  Interact
  Attack
  SecondaryAttack
  Dash
  UseItem
  OpenInventory
  Pause

UI
  Navigate
  Submit
  Cancel
  TabLeft
  TabRight
```

### 验收标准

- 键鼠和手柄可完整控制同一套玩法。
- 游戏逻辑中不再直接判断 `kW`、`kSPACE` 等具体键值。
- 切换到菜单时 Gameplay Action 自动禁用。

---

## 4.2 音频系统

### 最小模块

```text
AudioClip
AudioSourceComponent
AudioListenerComponent
AudioMixer
AudioBus
AudioSystem
```

### 任务

- [ ] 支持短音效加载和播放。
- [ ] 支持长音乐流式播放。
- [ ] 支持 OneShot。
- [ ] 支持 Loop。
- [ ] 支持 2D/3D 音效。
- [ ] 支持音量、Pitch 和 Spatial Blend。
- [ ] 支持 Master/Music/SFX/Ambient/UI Bus。
- [ ] 支持 Bus 音量持久化。
- [ ] 支持淡入淡出。
- [ ] 支持音乐切换。
- [ ] 支持最大 Voice 数量。
- [ ] 支持 Voice Stealing。
- [ ] 支持暂停快照。
- [ ] 支持水下音频滤镜或低通效果。

### 验收标准

- 水下场景与餐厅场景拥有独立音乐和环境音。
- UI、武器、命中和拾取拥有完整音效反馈。
- 暂停后音频行为符合预期。

---

## 4.3 SaveGame 系统

### 数据分层

```text
Asset         开发期只读内容
Scene         场景模板
SaveGame      玩家长期进度
RuntimeState  当前场景短期状态
```

### 核心类型

```text
SaveGameSubsystem
SaveGameHeader
PlayerProgress
DiveProgress
RestaurantProgress
NarrativeProgress
SettingsData
```

### 任务

- [ ] 定义 SaveGame 文件格式。
- [ ] 增加版本号。
- [ ] 增加存档迁移接口。
- [ ] 增加原子写入。
- [ ] 使用临时文件替换正式文件。
- [ ] 增加备份存档。
- [ ] 增加自动存档。
- [ ] 增加多存档槽。
- [ ] 增加存档摘要信息。
- [ ] 支持 Persistent GUID 引用。
- [ ] 缺失字段自动使用默认值。
- [ ] 增加损坏存档回退。
- [ ] 区分游戏进度与用户设置。

### 首批存档内容

- [ ] 当前天数。
- [ ] 金钱。
- [ ] 解锁区域。
- [ ] 背包和仓库。
- [ ] 武器和升级。
- [ ] 菜谱和食材。
- [ ] 员工状态。
- [ ] 餐厅等级。
- [ ] 任务状态。
- [ ] 剧情 Flag。
- [ ] 图鉴。
- [ ] 音量、语言、输入配置。

### 验收标准

- 完成一天流程后退出游戏，重新进入可准确恢复。
- 新版本新增字段后，旧存档仍可读取。
- 写入过程中强制终止程序，不会破坏最后一个有效存档。

---

## 4.4 场景生命周期与游戏流程

### 任务

- [ ] 实现场景异步加载。
- [ ] 实现场景卸载。
- [ ] 支持 Persistent Scene。
- [ ] 支持 Additive Zone。
- [ ] 实现 Loading Screen。
- [ ] 支持场景预加载。
- [ ] 场景卸载时释放资源引用。
- [ ] 增加 Scene Transition Subsystem。
- [ ] 增加 Game Flow Subsystem。

### 建议流程

```text
Boot
MainMenu
DayStart
DivePreparation
Diving
DiveSummary
RestaurantPreparation
RestaurantServing
DaySummary
StoryEvent
```

### 验收标准

- 可从潜水场景切换到餐厅场景。
- Persistent Scene 中的存档、音频、UI 和流程状态不会被销毁。
- 场景切换过程中无明显卡死和残留实体。

---

# 5. Phase 2：2D/2.5D 游戏基础

## 5.1 Sprite Animation

### 核心类型

```text
SpriteAnimationClip
SpriteAnimationFrame
SpriteAnimatorController
SpriteAnimatorComponent
```

### 任务

- [ ] 支持逐帧 Sprite 动画。
- [ ] 每帧支持独立持续时间。
- [ ] 支持 Loop。
- [ ] 支持 Once。
- [ ] 支持 Ping-Pong。
- [ ] 支持动画事件。
- [ ] 支持状态切换。
- [ ] 支持参数条件。
- [ ] 支持方向动画。
- [ ] 支持播放速度。
- [ ] 支持动画结束回调。
- [ ] 编辑器提供帧序列预览。

### 首批状态

```text
Idle
Swim
SwimUp
SwimDown
Attack
Hit
Dead
Interact
Carry
```

### 验收标准

- 玩家和鱼类可通过 Animator 切换状态。
- Gameplay 脚本不需要逐帧手动替换 Sprite。
- 动画事件可触发攻击判定和音效。

---

## 5.2 Texture Atlas 与 Sprite Sheet

### 任务

- [ ] Sprite Sheet 自动切片。
- [ ] 支持网格切片。
- [ ] 支持透明边界自动检测。
- [ ] 支持 Pivot 批量设置。
- [ ] 支持 Atlas 打包。
- [ ] 支持 Padding 和 Extrusion。
- [ ] 支持 Atlas 页面。
- [ ] 支持 Atlas 重建。
- [ ] 依赖变更后自动更新 Sprite。
- [ ] 保证 Sprite GUID 稳定。

### 验收标准

- 一张角色序列帧图片可快速生成 Sprite Asset 和 Animation Clip。
- Atlas 重建后，已有动画引用不丢失。

---

## 5.3 Sprite 排序与裁剪

### 任务

- [ ] Sprite 世界 AABB。
- [ ] Camera Frustum Culling。
- [ ] 2D Rect/Circle Culling。
- [ ] Sorting Layer 资产。
- [ ] Order in Layer。
- [ ] Y Sort。
- [ ] Sorting Group。
- [ ] Pixel Snap。
- [ ] Pixel Perfect Camera。
- [ ] Point Sampling 规范。
- [ ] 增加可视化调试工具。

### 验收标准

- 大量离屏 Sprite 不再提交绘制。
- 角色、鱼、前景和背景之间排序稳定。
- 像素画在相机移动时无明显抖动。

---

## 5.4 2D/2.5D Kinematic Collision

### 设计目标

优先服务于玩家、鱼、敌人、鱼叉、掉落物和 Trigger，不追求通用刚体模拟。

### 需要支持

```text
Circle
Box
Capsule
Segment
RayCast
ShapeCast
Overlap
Trigger
Collision Layer
Collision Mask
```

### 任务

- [ ] 实现 Spatial Hash broad phase。
- [ ] 实现静态和动态 Collider 分类。
- [ ] 实现 Layer/Mask。
- [ ] 实现 RayCast。
- [ ] 实现 Circle/Box/Capsule Overlap。
- [ ] 实现 ShapeCast。
- [ ] 实现 Trigger Enter/Stay/Exit。
- [ ] 实现 Collision Enter/Stay/Exit。
- [ ] 实现 Kinematic Character Controller。
- [ ] 支持外部速度和击退。
- [ ] 支持 One-Way Platform，若关卡需要。
- [ ] 增加碰撞调试绘制。

### 验收标准

- 玩家可稳定在水下移动并与障碍发生滑动碰撞。
- 鱼叉能够检测命中。
- Trigger 可用于交互区、出口、水流和剧情区域。
- 500 个动态对象下性能稳定。

---

## 5.5 Camera 系统

### 任务

- [ ] Camera Follow。
- [ ] Dead Zone。
- [ ] Camera Bounds。
- [ ] Smooth Damp。
- [ ] Zoom。
- [ ] Camera Shake。
- [ ] Target Group。
- [ ] 场景切换淡入淡出。
- [ ] 水下深度参数。
- [ ] 调试预览。

### 验收标准

- 玩家移动时镜头稳定。
- 命中、爆炸和大型敌人行为可触发 Camera Shake。
- 镜头不会越出关卡边界。

---

## 5.6 粒子与 Trail

### 最小能力

- [ ] Burst。
- [ ] Continuous Emission。
- [ ] Lifetime。
- [ ] Velocity。
- [ ] Gravity。
- [ ] Drag。
- [ ] Color over Lifetime。
- [ ] Size over Lifetime。
- [ ] Texture Sheet Animation。
- [ ] World/Local Space。
- [ ] Trail。
- [ ] Object Pool。
- [ ] 粒子编辑器预览。

### 首批用途

- [ ] 气泡。
- [ ] 水流。
- [ ] 鱼叉轨迹。
- [ ] 命中特效。
- [ ] 拾取反馈。
- [ ] 菜品完成反馈。
- [ ] 金钱反馈。
- [ ] UI 星光。

### 验收标准

- 主要玩法行为均有基础视觉反馈。
- 大量短生命周期特效不会频繁分配内存。

---

# 6. Phase 3：内容生产与数据驱动

## 6.1 Prefab / Entity Template

### 最小数据结构

```text
EntityPrefab
  Components[]
  Children[]
  AssetDependencies[]
```

### 任务

- [ ] 从场景实体创建 Prefab。
- [ ] Prefab 实例化。
- [ ] 支持子节点。
- [ ] 支持 Component 默认值。
- [ ] 支持资源引用。
- [ ] Spawn 后返回根 Entity。
- [ ] 支持场景内 Override。
- [ ] 支持 Apply。
- [ ] 支持 Revert。
- [ ] 支持 Prefab 依赖扫描。
- [ ] 支持缺失组件和缺失资源提示。
- [ ] 第一版暂不实现复杂 Variant。

### 验收标准

- 鱼、敌人、顾客、员工、掉落物和特效均通过 Prefab 生产。
- 修改 Prefab 后，实例可正确更新。
- 场景局部 Override 不会被错误覆盖。

---

## 6.2 DataAsset 体系

### 首批数据资产

```text
ItemDefinition
FishDefinition
EnemyDefinition
WeaponDefinition
RecipeDefinition
IngredientDefinition
CustomerDefinition
EmployeeDefinition
RestaurantUpgradeDefinition
QuestDefinition
DialogueDefinition
LevelDefinition
```

### 任务

- [ ] 建立统一 DataAsset 基类。
- [ ] Definition 与 Runtime Instance 分离。
- [ ] 支持 GUID 引用。
- [ ] 支持编辑器 Inspector。
- [ ] 支持数据校验。
- [ ] 支持依赖扫描。
- [ ] 支持批量编辑。
- [ ] 支持 CSV/JSON 导入，若策划数据量较大。
- [ ] 提供查找和过滤窗口。
- [ ] 支持运行时只读访问。

### 设计约束

```cpp
struct ItemDefinition;
struct ItemInstance;
```

Definition 保存配置，Instance 保存运行时数量、耐久度、品质等状态。

### 验收标准

- 新增鱼类、武器和菜谱时，无需修改核心系统代码。
- 编辑器可检测重复 ID、缺失资源和非法数值。

---

## 6.3 Lua / Gameplay Script 扩展

### 当前定位

- C++：稳定系统、通用组件和性能敏感逻辑。
- Lua：任务、NPC 行为、关卡事件、小游戏流程。
- DataAsset：静态内容配置。

### 任务

- [ ] 获取和修改组件。
- [ ] Spawn/Destroy Entity。
- [ ] 查找 Entity。
- [ ] 访问 Input Action。
- [ ] Timer。
- [ ] Coroutine。
- [ ] Gameplay Event。
- [ ] Physics Query。
- [ ] 播放动画。
- [ ] 播放音效。
- [ ] 控制 UI。
- [ ] 访问 DataAsset。
- [ ] 场景切换。
- [ ] Trigger/Collision 回调。
- [ ] 可序列化脚本字段。
- [ ] 热重载后恢复状态或明确重置策略。
- [ ] Lua 错误调用栈和实体上下文。

### 验收标准

- 一个简单任务流程可以完全通过 Lua 和 DataAsset 编写。
- Lua 错误不会导致引擎崩溃。
- Script Component 的可配置字段可在 Inspector 中编辑。

---

## 6.4 Gameplay Event 与 Timer

### 任务

- [ ] 全局 Event Bus。
- [ ] Entity 局部 Event。
- [ ] 强类型事件。
- [ ] 延迟事件。
- [ ] Timer Handle。
- [ ] 一次性 Timer。
- [ ] 循环 Timer。
- [ ] scaled/unscaled Timer。
- [ ] Coroutine 或 Sequence Runner。
- [ ] 事件订阅自动解绑。

### 验收标准

- UI、音频、任务和玩法系统不依赖硬编码互相调用。
- Entity 销毁后，不会保留悬空回调。

---

## 6.5 资源依赖与异步加载

### 任务

- [ ] Hard Dependency。
- [ ] Soft Dependency。
- [ ] 依赖扫描。
- [ ] 加载顺序。
- [ ] 循环依赖检测。
- [ ] 引用计数。
- [ ] Unload。
- [ ] Reimport 传播。
- [ ] 丢失引用提示。
- [ ] Asset Validator。
- [ ] Worker Thread 负责 IO 和解析。
- [ ] Main Thread 负责 Object 注册和 ECS 修改。
- [ ] Render Thread 负责 GPU 资源创建和上传。
- [ ] 异步 Callback 默认返回主线程。

### 验收标准

- 异步加载期间不会在工作线程直接修改 ECS 或创建不安全的 GPU 对象。
- 场景卸载后，无引用资源可以正确释放。
- 修改 Texture 后，关联 Sprite 和 Atlas 能够更新。

---

# 7. Phase 4：潜水玩法闭环

## 7.1 玩家控制

- [ ] 水下移动。
- [ ] 朝向和瞄准。
- [ ] 冲刺或闪避。
- [ ] 交互。
- [ ] 鱼叉攻击。
- [ ] 受击。
- [ ] 死亡或撤退。
- [ ] 氧气。
- [ ] 背包容量。
- [ ] 外部水流影响。
- [ ] 相机跟随。
- [ ] 动画、音效和特效反馈。

---

## 7.2 战斗与捕获

- [ ] Damage System。
- [ ] Health Component。
- [ ] HitBox。
- [ ] HurtBox。
- [ ] Projectile。
- [ ] Weapon Definition。
- [ ] 攻击冷却。
- [ ] 命中反馈。
- [ ] 击退。
- [ ] 捕获状态。
- [ ] 掉落物。
- [ ] 稀有度和品质。
- [ ] 捕获结果写入背包。

---

## 7.3 鱼类与敌人 AI

### 建议状态

```text
Idle
Wander
Flee
Chase
Attack
Stunned
Caught
Dead
```

### 任务

- [ ] 基础有限状态机。
- [ ] 感知范围。
- [ ] 视野或距离检测。
- [ ] Steering。
- [ ] 障碍规避。
- [ ] 群游的简化行为。
- [ ] 攻击前摇。
- [ ] 受击反应。
- [ ] 脱战。
- [ ] 调试状态显示。

### 验收标准

- 普通鱼会游荡并逃跑。
- 攻击型敌人会追击和攻击。
- AI 状态和目标可在编辑器中查看。

---

## 7.4 水下关卡系统

- [ ] 出生点。
- [ ] 出口。
- [ ] 区域边界。
- [ ] 水流区域。
- [ ] 危险区域。
- [ ] 交互物。
- [ ] 可破坏物。
- [ ] 资源刷新点。
- [ ] 剧情 Trigger。
- [ ] 环境参数。
- [ ] 水深与视觉参数。
- [ ] 水下音频快照。

### 验收标准

- 玩家可进入水下、捕获目标、离开并生成结算结果。
- 关卡设计人员可通过摆放 Prefab 完成简单区域制作。

---

# 8. Phase 5：餐厅经营闭环

## 8.1 导航与移动

- [ ] 简单 NavMesh 或 2D Navigation Grid。
- [ ] 路径查找。
- [ ] 动态占用。
- [ ] 座位点。
- [ ] 服务点。
- [ ] 厨房点。
- [ ] 队列点。
- [ ] 路径调试绘制。

---

## 8.2 顾客流程

### 状态示例

```text
Enter
Queue
MoveToSeat
Order
WaitForFood
Eat
Pay
Leave
AngryLeave
```

### 任务

- [ ] 顾客生成。
- [ ] 队列管理。
- [ ] 座位分配。
- [ ] 点单。
- [ ] 耐心值。
- [ ] 等待。
- [ ] 用餐。
- [ ] 结账。
- [ ] 离开。
- [ ] 评价。
- [ ] 特殊顾客规则。

---

## 8.3 菜单与食材

- [ ] Recipe Definition。
- [ ] Ingredient Definition。
- [ ] 当日菜单配置。
- [ ] 食材库存。
- [ ] 食材消耗。
- [ ] 售价。
- [ ] 制作时间。
- [ ] 品质。
- [ ] 解锁条件。
- [ ] 售罄状态。

---

## 8.4 员工系统

- [ ] Employee Definition。
- [ ] 厨师状态机。
- [ ] 服务员状态机。
- [ ] 工作速度。
- [ ] 移动速度。
- [ ] 技能。
- [ ] 升级。
- [ ] 分配岗位。
- [ ] 疲劳或复杂属性可后置。

---

## 8.5 餐厅结算

- [ ] 营业时间。
- [ ] 收入。
- [ ] 成本。
- [ ] 顾客满意度。
- [ ] 菜品销量。
- [ ] 食材消耗。
- [ ] 员工表现。
- [ ] 餐厅等级经验。
- [ ] 当日总结 UI。
- [ ] 数据写入 SaveGame。

### 验收标准

- 至少三名顾客可完成完整用餐流程。
- 至少两道菜可配置、制作、服务和结算。
- 餐厅结果可以影响下一天的玩家进度。

---

# 9. Phase 6：剧情、任务和教程

## 9.1 Dialogue 系统

### 数据

```text
Speaker
TextKey
Portrait
Expression
Voice
Choices
Conditions
Actions
NextNode
```

### 任务

- [ ] 对话节点。
- [ ] 分支选择。
- [ ] 条件。
- [ ] 动作。
- [ ] 头像。
- [ ] 表情。
- [ ] 打字机效果。
- [ ] 自动播放。
- [ ] 跳过。
- [ ] 对话历史。
- [ ] 语音或语音片段。
- [ ] 编辑器节点图或列表编辑器。

---

## 9.2 Quest 系统

```text
QuestDefinition
QuestRuntimeState
QuestObjective
QuestCondition
QuestReward
```

### 任务

- [ ] 接取任务。
- [ ] 任务条件。
- [ ] 多目标。
- [ ] 进度更新。
- [ ] 完成和失败。
- [ ] 奖励。
- [ ] 前置任务。
- [ ] 剧情 Flag。
- [ ] 任务追踪 UI。
- [ ] 任务状态持久化。

---

## 9.3 轻量 Sequence 系统

### 首批命令

```text
ShowDialogue
MoveEntity
PlayAnimation
Wait
SetFlag
PlayAudio
FadeScreen
LoadScene
GiveItem
StartQuest
```

### 任务

- [ ] 顺序执行。
- [ ] 等待条件。
- [ ] 可取消。
- [ ] 可跳过。
- [ ] scaled/unscaled Wait。
- [ ] 与 Lua 互操作。
- [ ] 执行状态可调试。

### 验收标准

- 可制作一段进入水下前的剧情。
- 可制作一个完成捕获后返回餐厅的任务。
- Sequence 中断后不会遗留锁定输入或错误 UI 状态。

---

## 9.4 本地化与文本

### 当前重点

现有字符映射不能只使用 `char`，需要支持 Unicode codepoint。

### 任务

- [ ] UTF-8 解码。
- [ ] 使用 `u32` codepoint。
- [ ] CJK 字形。
- [ ] 字体 fallback。
- [ ] 自动换行。
- [ ] 文本裁剪。
- [ ] 对齐。
- [ ] Rich Text。
- [ ] Localization Key。
- [ ] 参数格式化。
- [ ] 语言切换。
- [ ] 缺失文本检测。
- [ ] 字体字符集构建工具。

### 验收标准

- 中文、日文和英文文本均可正常显示。
- 对话、任务和 UI 不直接硬编码展示文本。

---

# 10. Phase 7：垂直切片

本阶段不是继续扩展系统，而是验证前面所有系统能否形成一个完整闭环。

## 10.1 潜水场景

- [ ] 玩家键鼠和手柄移动。
- [ ] 氧气。
- [ ] 一种普通鱼。
- [ ] 一种攻击型敌人。
- [ ] 鱼叉。
- [ ] 捕获。
- [ ] 背包容量。
- [ ] 离开水下场景。
- [ ] 潜水结算。

## 10.2 餐厅场景

- [ ] 三名顾客。
- [ ] 两道菜。
- [ ] 点单。
- [ ] 制作。
- [ ] 服务。
- [ ] 用餐。
- [ ] 结账。
- [ ] 当日收入结算。

## 10.3 连接流程

- [ ] 一段对话。
- [ ] 一个任务。
- [ ] 水下获得食材。
- [ ] 餐厅消耗食材。
- [ ] 完成一天。
- [ ] 自动存档。
- [ ] 退出并重新加载。
- [ ] 正确恢复下一天状态。

## 10.4 垂直切片通过标准

- [ ] 整个流程不需要手工修改资源文件。
- [ ] 新增一种鱼只需要创建 DataAsset、Prefab 和动画资源。
- [ ] 新增一道菜只需要创建 Recipe 与 Ingredient 数据。
- [ ] 不存在因场景切换产生的资源泄漏和悬空实体。
- [ ] 存档可准确恢复。
- [ ] 键鼠和手柄均可完成全部流程。
- [ ] 主要行为均有动画、音效和特效反馈。
- [ ] 可通过编辑器定位 AI、任务和事件状态。

---

# 11. Phase 8：性能、调试和内容生产优化

在垂直切片通过后再进入本阶段。

## 11.1 调试工具

- [ ] Entity Inspector。
- [ ] ECS Archetype/Component 统计。
- [ ] AI 状态调试。
- [ ] Navigation 调试。
- [ ] Physics Shape 调试。
- [ ] Input Action 调试。
- [ ] Audio Voice 调试。
- [ ] SaveGame 查看器。
- [ ] Quest/Dialogue 状态查看器。
- [ ] Asset Dependency Viewer。
- [ ] Scene Loading Profiler。
- [ ] Lua 调用栈和性能统计。

## 11.2 性能优化

- [ ] Sprite Culling。
- [ ] Sprite Batch。
- [ ] Spatial Hash。
- [ ] 对象池。
- [ ] 粒子池。
- [ ] 异步 IO。
- [ ] 纹理和音频内存预算。
- [ ] 场景卸载资源回收。
- [ ] UI 重建频率控制。
- [ ] SaveGame 写入节流。
- [ ] 大量顾客和鱼类的更新分帧。

## 11.3 内容生产优化

- [ ] DataAsset 批量编辑。
- [ ] Prefab 批量校验。
- [ ] Sprite Sheet 批量导入。
- [ ] 动画批量生成。
- [ ] 关卡对象刷子。
- [ ] 鱼类生成区域工具。
- [ ] 顾客流程模拟器。
- [ ] 菜谱经济数值预览。
- [ ] 对话和任务引用检查。
- [ ] 一键运行指定玩法阶段。

---

# 12. 暂缓开发项

在垂直切片完成前，以下能力不应成为主要投入方向：

- [ ] Ray Tracing。
- [ ] RTXDI。
- [ ] VXGI。
- [ ] 体积云。
- [ ] GPU Terrain。
- [ ] 完整通用 3D 刚体物理。
- [ ] 完整 Behavior Tree。
- [ ] Visual Scripting。
- [ ] World Partition。
- [ ] 复杂骨骼 IK。
- [ ] 通用动画图编辑器。
- [ ] 复杂 Prefab Variant。
- [ ] 大型开放世界无缝流送。
- [ ] 高级 GPU 粒子。

已有模块可以保留，但不继续优先扩展。

---

# 13. 推荐的实际执行顺序

以下顺序综合考虑了依赖关系和尽早获得可玩结果的需求。

## 第一批：必须先完成

1. ECS 动态容量与 generation。
2. 时间单位统一与 FixedUpdate。
3. Sprite Batch 实例偏移修复。
4. Input Action 与手柄。
5. 音频系统。
6. SaveGame 与 Persistent GUID。
7. Prefab / Entity Template。

## 第二批：建立可玩基础

8. Sprite Animation。
9. Texture Atlas 与 Sprite Sheet。
10. 2D/2.5D Kinematic Collision。
11. Gameplay Event、Timer 与 Coroutine。
12. Camera 系统。
13. Sprite Particle 与 Trail。
14. Sprite Culling、Sorting Layer 与 Y Sort。

## 第三批：建立内容生产能力

15. DataAsset 体系。
16. Lua Gameplay API。
17. 资源依赖和安全异步加载。
18. 场景异步切换与 Persistent Scene。
19. UTF-8、CJK 与本地化。
20. UI Focus、手柄导航和 Modal Stack。

## 第四批：实现核心循环

21. 玩家潜水控制。
22. 武器、伤害和捕获。
23. 鱼类和敌人 AI。
24. 水下区域与结算。
25. 餐厅导航。
26. 顾客状态机。
27. 菜单、库存和食材。
28. 员工与营业结算。

## 第五批：连接内容

29. Dialogue。
30. Quest。
31. Sequence。
32. 教程。
33. 完整一天流程。
34. 自动存档和重新加载。
35. 垂直切片性能与工具完善。

---

# 14. 每项任务的完成定义

任何任务只有同时满足以下条件，才视为完成：

- [ ] 运行时功能可用。
- [ ] 编辑器可以创建和修改相关资源。
- [ ] 数据可以保存和重新加载。
- [ ] 有最小测试场景或自动测试。
- [ ] 有错误日志和非法状态检查。
- [ ] 有基础调试可视化。
- [ ] 不依赖手工修改 JSON。
- [ ] 不存在明显的线程安全问题。
- [ ] 不引入无法追踪的资源生命周期。
- [ ] 已在键鼠和手柄路径下验证，若该系统涉及输入。

---

# 15. 当前最重要的里程碑

## Milestone 1：稳定运行时

完成：

- ECS
- Time
- Input
- Audio
- SaveGame
- Prefab
- Sprite Batch 修复

结果：引擎具备正式编写玩法的底层条件。

## Milestone 2：基础潜水原型

完成：

- Sprite Animation
- 2D Collision
- Camera
- Particle
- 玩家控制
- 一种鱼
- 鱼叉和捕获

结果：可以验证水下核心操作体验。

## Milestone 3：基础餐厅原型

完成：

- Navigation
- 顾客状态机
- 菜谱和库存
- 服务流程
- 营业结算

结果：可以验证经营核心循环。

## Milestone 4：完整一天

完成：

- Dialogue
- Quest
- Scene Flow
- 潜水结算
- 餐厅结算
- SaveGame

结果：形成第一版完整垂直切片。

---

# 16. 最终目标

引擎应最终能够支持以下高频内容生产操作：

1. 创建一个 FishDefinition。
2. 从 Sprite Sheet 生成 Sprite 和动画。
3. 创建 Fish Prefab。
4. 配置 AI 参数和掉落数据。
5. 将 Fish Prefab 放入关卡生成区域。
6. 创建 Ingredient 和 Recipe。
7. 将捕获结果自动转换为餐厅食材。
8. 通过 Dialogue、Quest 和 Sequence 连接剧情。
9. 在不修改核心 C++ 系统的情况下完成内容扩展。
10. 保存玩家进度，并在下一次运行中准确恢复。

当这一流程可以顺畅完成时，AiluEngine 才真正具备制作该项目的完整生产能力。
