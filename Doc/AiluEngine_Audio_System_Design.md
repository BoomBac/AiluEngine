# AiluEngine 轻量音频系统设计文档

## 1. 文档目的

本文档用于指导 AiluEngine 实现一套简洁、稳定、可维护的音频系统。

目标项目规模对标《潜水员戴夫》或更小，重点覆盖中小型单机游戏的常见需求：

- 2D 音效
- 3D 音效
- UI 音效
- BGM
- 环境音
- 循环播放
- 音量与音调控制
- 淡入淡出
- 混音总线
- 场景组件
- 资源加载
- 音效并发限制
- 编辑器预览
- 基础调试面板

本系统不追求替代 FMOD、Wwise 等专业音频中间件，也不在第一阶段实现复杂的事件图、DSP 节点图或高级声学传播。

---

## 2. 设计目标

### 2.1 核心目标

1. 对游戏逻辑提供简单、稳定的播放接口。
2. 使用 AiluEngine 自身的 GUID、Asset、Object、ECS 和编辑器体系。
3. 隐藏底层音频库实现，不向 Gameplay 暴露后端对象。
4. 支持短音效内存播放和长音频流式播放。
5. 支持场景中的空间音频。
6. 支持播放实例句柄和安全的生命周期管理。
7. 支持声音分组和统一音量控制。
8. 支持同类音效并发限制，避免 Voice 爆炸。
9. 保持模块规模小，避免过度工程化。

### 2.2 非目标

第一版不实现：

- 完整 FMOD/Wwise 风格事件图
- 任意 DSP 图编辑器
- 音频 Timeline 编辑器
- 多 Listener
- 房间声学传播
- 声音几何反射
- HRTF 配置系统
- Steam Audio 集成
- GPU 声学追踪
- 动态响度标准化
- 音乐节拍同步系统
- Bank 构建系统
- 自定义音频解码器
- 自定义采样率转换器
- 自定义 WASAPI 底层设备管理

---

## 3. 技术选型

### 3.1 后端选择

建议使用 **miniaudio** 作为第一版音频后端。

miniaudio 负责：

- 音频设备初始化
- WAV、MP3、FLAC 等格式解码
- 音频播放
- 流式读取
- 采样率转换
- 基础混音
- 声音分组
- 3D 空间化
- Listener
- 音频资源管理
- 可选异步加载与解码

AiluEngine 负责：

- AudioClip Asset
- AudioEvent Asset
- GUID 引用
- ResourceManager 接入
- AudioHandle
- Voice 池
- ECS 组件
- 资源生命周期
- 并发控制
- 混音总线抽象
- 编辑器 Inspector
- 调试界面
- Gameplay API

### 3.2 后端隔离原则

`miniaudio.h` 只允许出现在音频后端目录。

禁止以下模块直接依赖 miniaudio：

- Gameplay
- ECS Component
- AudioClip
- AudioEvent
- ResourceManager
- Editor Inspector
- Scene
- Script Binding

底层后端必须通过 `IAudioBackend` 接口访问。

---

## 4. 推荐目录结构

```text
Engine/Runtime/Audio/
    AudioTypes.h
    AudioHandle.h
    AudioClip.h
    AudioClip.cpp
    AudioClipDocument.h
    AudioClipDocument.cpp
    AudioEvent.h
    AudioEvent.cpp
    AudioEventDocument.h
    AudioEventDocument.cpp
    AudioBus.h
    AudioVoice.h
    AudioVoicePool.h
    AudioVoicePool.cpp
    AudioDevice.h
    AudioDevice.cpp
    AudioSystem.h
    AudioSystem.cpp
    Audio.h
    Audio.cpp

Engine/Runtime/Audio/Backend/
    IAudioBackend.h
    MiniaudioBackend.h
    MiniaudioBackend.cpp
    MiniaudioImplementation.cpp
    NullAudioBackend.h
    NullAudioBackend.cpp

Engine/Runtime/Scene/Component/
    AudioSourceComponent.h
    AudioListenerComponent.h

Engine/Editor/Audio/
    AudioClipEditor.h
    AudioClipEditor.cpp
    AudioEventEditor.h
    AudioEventEditor.cpp
    AudioDebugger.h
    AudioDebugger.cpp
```

如果 AiluEngine 已有统一的 Component 目录，可将两个音频组件放入现有组件目录。

---

## 5. 系统分层

```text
Gameplay / Script / ECS
        |
        v
Audio API / AudioSystem
        |
        v
AudioDevice / AudioVoicePool
        |
        v
IAudioBackend
        |
        v
MiniaudioBackend / NullAudioBackend
        |
        v
miniaudio / WASAPI
```

各层职责如下。

### 5.1 Gameplay 层

负责：

- 请求播放音效
- 请求播放事件
- 停止声音
- 设置音量
- 设置音调
- 请求淡入淡出
- 设置 Bus 音量

不负责：

- 解码
- 文件读取
- 音频线程
- 后端资源管理
- Voice 内存生命周期

### 5.2 AudioSystem 层

负责：

- ECS 与 AudioDevice 的连接
- Listener 更新
- AudioSourceComponent 更新
- 场景加载与卸载
- Entity 销毁时停止关联声音
- `play_on_awake`
- Source Transform 同步

### 5.3 AudioDevice 层

负责：

- Voice 创建与销毁
- AudioHandle 管理
- Voice 池
- 并发限制
- Voice 优先级
- Bus 状态
- 后端调用
- 音频运行时状态统计

### 5.4 后端层

负责：

- 音频设备
- 音频数据加载
- 音频流
- 底层播放对象
- 3D 参数设置
- Listener 参数设置
- Bus 或 Sound Group
- 后端资源释放

---

## 6. 核心类型

## 6.1 AudioHandle

`AudioHandle` 表示一次具体播放实例。

不能向 Gameplay 返回 `AudioVoice*`、`ma_sound*` 或其他裸指针。

```cpp
namespace Ailu
{
    struct AudioHandle
    {
        u32 _index = 0;
        u32 _generation = 0;

        bool IsValid() const
        {
            return _generation != 0;
        }

        static AudioHandle Invalid()
        {
            return {};
        }

        bool operator==(const AudioHandle &other) const
        {
            return _index == other._index && _generation == other._generation;
        }
    };
}
```

必须使用 index + generation，原因如下：

- Voice 槽位会复用
- 声音可能已经自然播放结束
- 旧句柄可能仍由 Gameplay 保存
- 不能因句柄过期访问到新的 Voice

---

## 6.2 AudioClip

`AudioClip` 表示运行时音频资源，不表示一次播放。

```cpp
namespace Ailu
{
    enum class EAudioLoadMode
    {
        kMemory,
        kStreaming
    };

    enum class EAudioChannelMode
    {
        kAuto,
        kMono,
        kStereo
    };

    ACLASS()
    class AILU_API AudioClip : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        EAudioLoadMode _load_mode = EAudioLoadMode::kMemory;

        APROPERTY()
        EAudioChannelMode _channel_mode = EAudioChannelMode::kAuto;

        APROPERTY()
        bool _force_mono = false;

        APROPERTY()
        f32 _duration = 0.0f;

        APROPERTY()
        u32 _sample_rate = 0;

        APROPERTY()
        u32 _channel_count = 0;

        APROPERTY()
        String _runtime_path;
    };
}
```

### 6.2.1 资源加载建议

短音效使用内存模式：

- UI 点击
- 脚步
- 攻击
- 受击
- 碰撞
- 拾取
- 小型环境音

长音频使用流式模式：

- BGM
- 长对白
- 长环境循环
- 超过约 10～20 秒的声音

第一版只区分 `kMemory` 和 `kStreaming`，不要过早增加过多导入策略。

---

## 6.3 AudioClipDocument

`AudioClipDocument` 用于编辑器序列化和导入设置。

```cpp
namespace Ailu
{
    ACLASS()
    class AILU_API AudioClipDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;

        APROPERTY()
        String _source_file;

        APROPERTY()
        EAudioLoadMode _load_mode = EAudioLoadMode::kMemory;

        APROPERTY()
        EAudioChannelMode _channel_mode = EAudioChannelMode::kAuto;

        APROPERTY()
        bool _force_mono = false;
    };
}
```

职责边界：

```text
AudioClipDocument
    编辑器数据和导入设置

AudioClip
    运行时音频资源

AudioVoice
    某一次具体播放
```

这三者不能混合。

---

## 6.4 AudioEvent

`AudioEvent` 是一层轻量的声音播放配置。

它不是 FMOD 风格的完整事件系统，只负责解决以下问题：

- 多个 Clip 随机选择
- 权重
- 随机音量
- 随机 Pitch
- Bus 指定
- 2D/3D
- 循环
- 并发限制
- 冷却时间
- 最小和最大距离

```cpp
namespace Ailu
{
    struct AudioEventClip
    {
        Guid _clip;
        f32 _weight = 1.0f;
    };

    enum class EAudioConcurrencyScope
    {
        kGlobal,
        kPerEntity
    };

    ACLASS()
    class AILU_API AudioEvent : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        Vector<AudioEventClip> _clips;

        APROPERTY()
        EAudioBus _bus = EAudioBus::kSfx;

        APROPERTY()
        f32 _volume_min = 1.0f;

        APROPERTY()
        f32 _volume_max = 1.0f;

        APROPERTY()
        f32 _pitch_min = 1.0f;

        APROPERTY()
        f32 _pitch_max = 1.0f;

        APROPERTY()
        u32 _max_instances = 0;

        APROPERTY()
        f32 _cooldown = 0.0f;

        APROPERTY()
        f32 _min_distance = 1.0f;

        APROPERTY()
        f32 _max_distance = 30.0f;

        APROPERTY()
        bool _is_3d = false;

        APROPERTY()
        bool _loop = false;

        APROPERTY()
        EAudioConcurrencyScope _concurrency_scope = EAudioConcurrencyScope::kGlobal;
    };
}
```

### 6.4.1 使用示例

```text
event_player_footstep_wood
    clips:
        footstep_wood_01
        footstep_wood_02
        footstep_wood_03
    volume:
        0.85 ~ 1.00
    pitch:
        0.94 ~ 1.06
    max_instances:
        4
    cooldown:
        0.05
    bus:
        Sfx
    is_3d:
        true
```

---

## 6.5 AudioEventDocument

```cpp
namespace Ailu
{
    ACLASS()
    class AILU_API AudioEventDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;

        APROPERTY()
        Vector<AudioEventClip> _clips;

        APROPERTY()
        EAudioBus _bus = EAudioBus::kSfx;

        APROPERTY()
        f32 _volume_min = 1.0f;

        APROPERTY()
        f32 _volume_max = 1.0f;

        APROPERTY()
        f32 _pitch_min = 1.0f;

        APROPERTY()
        f32 _pitch_max = 1.0f;

        APROPERTY()
        u32 _max_instances = 0;

        APROPERTY()
        f32 _cooldown = 0.0f;

        APROPERTY()
        f32 _min_distance = 1.0f;

        APROPERTY()
        f32 _max_distance = 30.0f;

        APROPERTY()
        bool _is_3d = false;

        APROPERTY()
        bool _loop = false;
    };
}
```

`_header._dependencies` 中记录所有引用的 AudioClip GUID。

---

## 6.6 AudioVoice

`AudioVoice` 是内部运行时对象。

不需要反射，不需要序列化，也不是 Asset。

```cpp
namespace Ailu
{
    struct BackendVoiceHandle
    {
        u32 _index = 0;
        u32 _generation = 0;

        bool IsValid() const
        {
            return _generation != 0;
        }
    };

    struct AudioVoice
    {
        AudioHandle _handle;
        BackendVoiceHandle _backend_handle;

        Guid _clip;
        Guid _event;
        Entity _owner;

        EAudioBus _bus = EAudioBus::kSfx;

        f32 _base_volume = 1.0f;
        f32 _pitch = 1.0f;
        f32 _priority = 0.5f;
        f32 _min_distance = 1.0f;
        f32 _max_distance = 30.0f;
        f32 _age = 0.0f;

        Vector3f _position = Vector3f::kZero;
        Vector3f _velocity = Vector3f::kZero;

        bool _is_3d = false;
        bool _loop = false;
        bool _paused = false;
        bool _persistent = false;
        bool _active = false;
    };
}
```

---

## 7. 混音总线

第一版使用固定层级，不实现任意 Bus 图。

```cpp
namespace Ailu
{
    enum class EAudioBus
    {
        kMaster,
        kMusic,
        kSfx,
        kUi,
        kAmbient,
        kVoice,
        kCount
    };
}
```

层级：

```text
Master
├── Music
├── Sfx
├── Ui
├── Ambient
└── Voice
```

最终音量可以概念化为：

```text
final_volume =
    voice_volume *
    event_volume *
    source_volume *
    bus_volume *
    master_volume *
    distance_attenuation *
    fade_volume
```

公开 API：

```cpp
Audio::SetBusVolume(EAudioBus::kMusic, 0.7f);
Audio::SetBusMuted(EAudioBus::kSfx, true);
Audio::FadeBus(EAudioBus::kMusic, 0.3f, 1.0f);
Audio::StopBus(EAudioBus::kAmbient);
```

游戏设置建议暴露：

- Master Volume
- Music Volume
- Sound Volume
- Voice Volume

`Sfx`、`Ui` 和 `Ambient` 可以统一映射到 Sound Volume，但内部仍保留独立 Bus。

---

## 8. ECS 组件

## 8.1 AudioSourceComponent

`AudioSourceComponent` 用于持续附着在 Entity 上的声音。

典型用途：

- 发动机
- 河流
- 篝火
- 机器
- 风
- 环境循环
- 持续技能声音

```cpp
namespace Ailu
{
    ACOMPONENT()
    struct AILU_API AudioSourceComponent
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _audio_event;

        APROPERTY()
        bool _play_on_awake = false;

        APROPERTY()
        bool _loop = false;

        APROPERTY()
        bool _spatial = true;

        APROPERTY()
        f32 _volume = 1.0f;

        APROPERTY()
        f32 _pitch = 1.0f;

        APROPERTY()
        f32 _min_distance = 1.0f;

        APROPERTY()
        f32 _max_distance = 30.0f;

        APROPERTY()
        f32 _priority = 0.5f;

        AudioHandle _runtime_handle;
    };
}
```

一次性音效不应该动态创建组件。

例如攻击、受击、爆炸和 UI 点击应直接调用：

```cpp
Audio::PostEventAt(hit_event, hit_position);
Audio::PostEvent(ui_click_event);
```

---

## 8.2 AudioListenerComponent

第一版仅支持一个有效 Listener。

```cpp
namespace Ailu
{
    ACOMPONENT()
    struct AILU_API AudioListenerComponent
    {
        GENERATED_BODY()

        APROPERTY()
        bool _enabled = true;
    };
}
```

Listener 选择规则：

1. 优先选择激活相机上的 AudioListenerComponent。
2. 没有组件时，可临时使用主相机 Transform。
3. 场景中存在多个有效 Listener 时，编辑器输出警告。
4. Scene View 预览可以使用编辑器相机，但不写入游戏运行状态。

每帧更新：

```cpp
listener_position = transform.GetWorldPosition();
listener_forward = transform.GetForward();
listener_up = transform.GetUp();
listener_velocity = (listener_position - previous_position) / delta_time;
```

第一版可以暂时不启用 Doppler，因此 Listener velocity 可先保留但不上传。

---

## 8.3 AudioSystem

推荐执行顺序：

```text
TransformSystem
    ↓
CameraSystem
    ↓
AudioSystem
    ↓
RenderSystem
```

职责：

- 查询当前 Listener
- 更新 Listener Transform
- 更新活动 AudioSourceComponent
- 执行 `play_on_awake`
- 处理组件禁用
- 处理 Entity 销毁
- 处理 Scene 卸载
- 将 Source 状态同步到 AudioDevice

避免每帧遍历所有历史 Voice。

只遍历：

- 当前活动的 AudioSourceComponent
- 当前活动的 Voice
- 当前有效 Listener

---

## 9. AudioDevice

```cpp
namespace Ailu
{
    struct AudioDeviceConfig
    {
        bool _enabled = true;
        u32 _sample_rate = 48000;
        u32 _max_voices = 128;
        u32 _max_streaming_voices = 8;
        String _device_name;
    };

    struct AudioListenerState
    {
        Vector3f _position = Vector3f::kZero;
        Vector3f _forward = Vector3f::kForward;
        Vector3f _up = Vector3f::kUp;
        Vector3f _velocity = Vector3f::kZero;
    };

    class AILU_API AudioDevice
    {
    public:
        bool Initialize(const AudioDeviceConfig &config);
        void Shutdown();
        void Update(f32 delta_time);

        AudioHandle Play(const Guid &clip, const AudioPlayOptions &options);
        AudioHandle PostEvent(const Guid &event, const AudioPlayOptions &options);

        void Stop(AudioHandle handle);
        void Pause(AudioHandle handle);
        void Resume(AudioHandle handle);

        void SetVolume(AudioHandle handle, f32 volume);
        void SetPitch(AudioHandle handle, f32 pitch);
        void SetPosition(AudioHandle handle, const Vector3f &position);
        void SetVelocity(AudioHandle handle, const Vector3f &velocity);

        void FadeIn(AudioHandle handle, f32 duration);
        void FadeOut(AudioHandle handle, f32 duration);

        void SetListener(const AudioListenerState &listener);

        void SetBusVolume(EAudioBus bus, f32 volume);
        void SetBusMuted(EAudioBus bus, bool muted);
        void StopBus(EAudioBus bus);
        void FadeBus(EAudioBus bus, f32 target_volume, f32 duration);

        bool IsPlaying(AudioHandle handle) const;
        bool IsValid(AudioHandle handle) const;

    private:
        Scope<IAudioBackend> _backend;
        AudioVoicePool _voice_pool;
        AudioListenerState _listener;
        Array<f32, static_cast<u32>(EAudioBus::kCount)> _bus_volumes;
    };
}
```

---

## 10. AudioPlayOptions

```cpp
namespace Ailu
{
    struct AudioPlayOptions
    {
        Entity _owner;

        Vector3f _position = Vector3f::kZero;
        Vector3f _velocity = Vector3f::kZero;

        EAudioBus _bus = EAudioBus::kSfx;

        f32 _volume = 1.0f;
        f32 _pitch = 1.0f;
        f32 _priority = 0.5f;
        f32 _min_distance = 1.0f;
        f32 _max_distance = 30.0f;
        f32 _fade_in_duration = 0.0f;

        bool _is_3d = false;
        bool _loop = false;
        bool _persistent = false;
    };
}
```

`AudioEvent` 的配置和 `AudioPlayOptions` 合并时，建议采用以下优先级：

```text
AudioClip 默认值
    ↓
AudioEvent 配置
    ↓
AudioSourceComponent 配置
    ↓
AudioPlayOptions 显式覆盖
```

---

## 11. Gameplay API

建议提供静态薄封装。

```cpp
namespace Ailu::Audio
{
    AudioHandle Play(const Guid &clip, const AudioPlayOptions &options = {});
    AudioHandle PostEvent(const Guid &event, const AudioPlayOptions &options = {});
    AudioHandle PostEventAt(const Guid &event, const Vector3f &position, const AudioPlayOptions &options = {});

    void Stop(AudioHandle handle);
    void Pause(AudioHandle handle);
    void Resume(AudioHandle handle);

    void SetVolume(AudioHandle handle, f32 volume);
    void SetPitch(AudioHandle handle, f32 pitch);
    void SetPosition(AudioHandle handle, const Vector3f &position);

    void FadeIn(AudioHandle handle, f32 duration);
    void FadeOut(AudioHandle handle, f32 duration);

    void SetBusVolume(EAudioBus bus, f32 volume);
    void SetBusMuted(EAudioBus bus, bool muted);
    void StopBus(EAudioBus bus);
}
```

调用示例：

```cpp
Audio::PostEvent(ui_click_event);

AudioPlayOptions options;
options._is_3d = true;
options._position = hit_position;
options._priority = 0.8f;

AudioHandle handle = Audio::PostEvent(hit_event, options);
Audio::FadeOut(handle, 0.25f);
```

Lua API 未来可映射为：

```lua
Audio.PostEvent("event/ui/click")
Audio.PostEventAt("event/enemy/hit", transform.position)
Audio.SetBusVolume("Music", 0.7)
```

---

## 12. Voice 池

第一版建议：

```cpp
constexpr u32 kMaxAudioVoices = 128;
constexpr u32 kMaxStreamingVoices = 8;
```

### 12.1 Voice 分配规则

1. Voice 池存在空闲槽位时直接分配。
2. 池已满时计算 Voice 优先级。
3. 找到最低优先级的非持久 Voice。
4. 新 Voice 分数更高时，停止旧 Voice 并复用槽位。
5. 新 Voice 分数不高于最低 Voice 时，拒绝播放。
6. Music 和关键 Voice 可标记 `_persistent`，不可被普通 SFX 抢占。

### 12.2 优先级建议

```cpp
f32 AudioDevice::CalculateVoiceScore(const AudioVoice &voice, const Vector3f &listener_position) const
{
    if (!voice._is_3d)
        return voice._priority + 1.0f;

    const f32 distance = Vector3f::Distance(voice._position, listener_position);
    const f32 normalized_distance = Math::Saturate(distance / voice._max_distance);

    return voice._priority * 2.0f + (1.0f - normalized_distance);
}
```

可以在后续版本加入：

- Voice 年龄
- 当前实际音量
- Bus 类型
- 是否循环
- 是否已经接近播放结束

---

## 13. 并发限制

AudioEvent 必须支持并发限制。

建议至少支持：

- 全局 Event 并发
- 每 Entity Event 并发
- 冷却时间

示例：

```text
footstep_player:
    max_instances = 2
    concurrency_scope = PerEntity

footstep_npc:
    max_instances = 12
    concurrency_scope = Global

enemy_hit:
    max_instances = 6
    concurrency_scope = Global

ui_click:
    max_instances = 2
    concurrency_scope = Global

explosion:
    max_instances = 8
    concurrency_scope = Global
```

Event 播放前检查：

```text
1. 检查 cooldown
2. 检查当前实例数
3. 超过限制时：
   - 拒绝新实例
   - 或停止最旧实例
4. 随机选择 Clip
5. 随机生成 volume 和 pitch
6. 创建 Voice
```

第一版默认采用“拒绝新实例”。

未来可以增加：

```cpp
enum class EAudioConcurrencyResolution
{
    kRejectNew,
    kStopOldest,
    kStopLowestPriority
};
```

---

## 14. 3D 音频

第一版只实现：

- 声源位置
- Listener 位置
- Listener 朝向
- 距离衰减
- 左右声像
- 最小距离
- 最大距离
- 可选 Doppler

暂时不实现：

- 遮挡
- 反射
- 几何混响
- 多 Listener
- 高级 HRTF 配置

### 14.1 衰减模型

```cpp
enum class EAudioAttenuationModel
{
    kLinear,
    kInverse
};
```

默认可以使用 inverse 模型。

底层衰减尽量交给 miniaudio，实现层只保存参数。

### 14.2 Force Mono

3D 音频建议使用 Mono。

AudioClip 导入时提供 `_force_mono`：

- 3D SFX 默认建议开启
- BGM 和 UI 不开启
- Ambient 是否开启根据资源类型决定

---

## 15. 线程模型

### 15.1 主线程负责

- Gameplay API
- GUID 解析
- AudioEvent 选择
- 随机音量和 Pitch
- 并发限制
- Voice 分配策略
- ECS Transform
- Source 生命周期
- Listener 状态
- 场景卸载
- 提交后端参数更新

### 15.2 后端负责

- 音频设备
- 实际混音
- 解码
- 流式读取
- 采样率转换
- 后端播放对象
- 后端资源释放

### 15.3 实时音频回调禁止执行

- 动态内存分配
- 等待锁
- Asset 数据库查询
- 字符串查找
- 日志格式化
- 文件系统访问
- ECS 查询
- 反射调用
- ResourceManager 加载

### 15.4 是否创建独立 Audio Thread

第一版不建议额外创建 Ailu Audio Thread。

miniaudio 已有设备回调和资源加载机制。

AiluEngine 第一版只需保证：

- 主线程不在音频回调中执行复杂逻辑
- 所有后端对象只通过 AudioDevice 访问
- 资源释放顺序正确
- 不跨线程访问 ECS 和 Object

后续遇到明确的线程争用或大量参数更新时，再增加 AudioControlThread。

---

## 16. 后端接口

```cpp
namespace Ailu
{
    struct BackendVoiceCreateInfo
    {
        Guid _clip;
        EAudioBus _bus = EAudioBus::kSfx;

        Vector3f _position = Vector3f::kZero;
        Vector3f _velocity = Vector3f::kZero;

        f32 _volume = 1.0f;
        f32 _pitch = 1.0f;
        f32 _min_distance = 1.0f;
        f32 _max_distance = 30.0f;

        bool _is_3d = false;
        bool _loop = false;
        bool _streaming = false;
    };

    class IAudioBackend
    {
    public:
        virtual ~IAudioBackend() = default;

        virtual bool Initialize(const AudioDeviceConfig &config) = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;

        virtual BackendVoiceHandle CreateVoice(const BackendVoiceCreateInfo &create_info) = 0;
        virtual void DestroyVoice(BackendVoiceHandle handle) = 0;

        virtual void Play(BackendVoiceHandle handle) = 0;
        virtual void Stop(BackendVoiceHandle handle) = 0;
        virtual void Pause(BackendVoiceHandle handle) = 0;
        virtual void Resume(BackendVoiceHandle handle) = 0;

        virtual void SetVolume(BackendVoiceHandle handle, f32 volume) = 0;
        virtual void SetPitch(BackendVoiceHandle handle, f32 pitch) = 0;
        virtual void SetPosition(BackendVoiceHandle handle, const Vector3f &position) = 0;
        virtual void SetVelocity(BackendVoiceHandle handle, const Vector3f &velocity) = 0;

        virtual void SetListener(const AudioListenerState &listener) = 0;
        virtual void SetBusVolume(EAudioBus bus, f32 volume) = 0;
        virtual void SetBusMuted(EAudioBus bus, bool muted) = 0;

        virtual bool IsPlaying(BackendVoiceHandle handle) const = 0;
    };
}
```

---

## 17. NullAudioBackend

必须提供 NullAudioBackend。

用途：

- 自动化测试
- 无声构建
- Server 模式
- 音频设备不可用
- 编辑器禁用音频
- CI 环境

```cpp
namespace Ailu
{
    class NullAudioBackend final : public IAudioBackend
    {
    public:
        bool Initialize(const AudioDeviceConfig &) override
        {
            return true;
        }

        void Shutdown() override {}
        void Update() override {}

        BackendVoiceHandle CreateVoice(const BackendVoiceCreateInfo &) override
        {
            return {};
        }

        void DestroyVoice(BackendVoiceHandle) override {}
        void Play(BackendVoiceHandle) override {}
        void Stop(BackendVoiceHandle) override {}
        void Pause(BackendVoiceHandle) override {}
        void Resume(BackendVoiceHandle) override {}
        void SetVolume(BackendVoiceHandle, f32) override {}
        void SetPitch(BackendVoiceHandle, f32) override {}
        void SetPosition(BackendVoiceHandle, const Vector3f &) override {}
        void SetVelocity(BackendVoiceHandle, const Vector3f &) override {}
        void SetListener(const AudioListenerState &) override {}
        void SetBusVolume(EAudioBus, f32) override {}
        void SetBusMuted(EAudioBus, bool) override {}

        bool IsPlaying(BackendVoiceHandle) const override
        {
            return false;
        }
    };
}
```

Null 后端下 Gameplay API 不应报错或崩溃。

---

## 18. 资源系统接入

不要继续把音频播放职责塞进 ResourceManager。

推荐职责拆分：

```text
ResourceManager
    公共资源入口和生命周期

AudioAssetLoader
    AudioClip 和 AudioEvent 加载

AudioDevice
    播放实例、Bus 和后端

AudioSystem
    ECS 和场景同步
```

### 18.1 Asset 依赖

推荐：

```cpp
AssetDocumentHeader
{
    Vector<Guid> _dependencies;
};
```

不推荐：

```cpp
Vector<Ref<Object>> _dep;
```

原因：

- 会将运行时引用和构建依赖混合
- 可能强制加载依赖
- 无法脱离 Object 实例分析 Asset 依赖
- 容易形成循环引用
- 增加所有 Asset 的运行时成本

AudioEventDocument 的依赖列表应包含全部 AudioClip GUID。

---

## 19. 初始化与销毁

### 19.1 ApplicationInitContext

```cpp
struct ApplicationInitContext
{
    bool _enable_audio = true;
    String _audio_device_name;
    u32 _audio_sample_rate = 48000;
    u32 _audio_max_voices = 128;
    u32 _audio_max_streaming_voices = 8;
};
```

### 19.2 初始化顺序

```text
MemorySystem
FileSystem
JobSystem
ResourceManager
AudioDevice
Scene / ECS
Renderer
Editor
```

### 19.3 销毁顺序

```text
Editor
Renderer
Scene / ECS
AudioDevice
ResourceManager
JobSystem
FileSystem
MemorySystem
```

Scene 和组件必须先停止、释放关联 Voice，再销毁 AudioDevice。

---

## 20. 编辑器功能

## 20.1 AudioClip Inspector

第一版需要：

```text
┌ Audio Clip ──────────────────────────┐
│ Source       assets/audio/hit_01.wav │
│ Duration     0.43 s                  │
│ Channels     Mono                    │
│ Sample Rate  48000 Hz                │
│                                      │
│ Load Mode    [Memory ▼]              │
│ Force Mono   [x]                     │
│                                      │
│ [▶ Preview] [■ Stop]                 │
│ ───────── playback progress ──────── │
└──────────────────────────────────────┘
```

功能：

- 显示音频元数据
- 修改加载模式
- Force Mono
- Preview
- Stop
- 播放进度
- 重新导入

第一版不需要波形编辑器。

---

## 20.2 AudioEvent Inspector

```text
┌ Audio Event ─────────────────────────┐
│ Bus             SFX                  │
│ Spatial         [x]                  │
│ Loop            [ ]                  │
│ Volume          0.90 ~ 1.00          │
│ Pitch           0.95 ~ 1.05          │
│ Min Distance    1.0                  │
│ Max Distance    30.0                 │
│ Max Instances   6                    │
│ Cooldown        0.05 s               │
│                                      │
│ Clips                                │
│  hit_01.wav       Weight 1.0         │
│  hit_02.wav       Weight 1.0         │
│  hit_03.wav       Weight 1.0         │
│                                      │
│ [▶ Preview Random] [■ Stop]          │
└──────────────────────────────────────┘
```

---

## 20.3 Audio Debugger

建议实现 ImGui 面板。

```text
Audio Device
    Device: Speakers
    Sample Rate: 48000
    Channels: 2
    Backend: WASAPI

Voices
    Active: 24 / 128
    Streaming: 2 / 8
    Rejected This Frame: 0
    Stolen This Frame: 1

Buses
    Master     1.00
    Music      0.70
    SFX        0.85
    UI         1.00
    Ambient    0.65
    Voice      1.00

Active Voices
    event_enemy_hit       0.2 s   SFX      Distance 3.2
    music_dive_day        82 s    Music    Streaming
    ambient_water_loop    14 s    Ambient  Distance 8.4
```

每个 Voice 至少显示：

- Handle
- Event
- Clip
- Bus
- 2D/3D
- Volume
- Pitch
- Distance
- Loop
- Streaming
- Owner Entity
- Playing Time
- Priority

---

## 21. 淡入淡出

淡入淡出建议由 AiluEngine 运行时层控制，而不是完全依赖后端。

```cpp
struct AudioFadeState
{
    f32 _start_volume = 1.0f;
    f32 _target_volume = 1.0f;
    f32 _duration = 0.0f;
    f32 _elapsed = 0.0f;
    bool _stop_when_finished = false;
    bool _active = false;
};
```

每帧更新：

```cpp
const f32 t = Math::Saturate(fade._elapsed / fade._duration);
const f32 volume = Math::Lerp(fade._start_volume, fade._target_volume, t);
```

FadeOut 完成后：

```text
stop_when_finished == true
    ↓
停止后端 Voice
    ↓
释放 Voice 槽位
    ↓
generation 增加
```

---

## 22. 音乐播放

第一版不需要专门的音乐系统，但建议增加简单封装。

```cpp
namespace Ailu::Audio
{
    AudioHandle PlayMusic(const Guid &clip, f32 fade_in_duration = 0.5f);
    void StopMusic(f32 fade_out_duration = 0.5f);
    void CrossFadeMusic(const Guid &clip, f32 duration = 1.0f);
}
```

规则：

- Music 默认走 `EAudioBus::kMusic`
- 默认 streaming
- 默认 loop
- 默认 persistent
- 新音乐播放时可淡出旧音乐
- 第一版同时最多保留两个 Music Voice，用于交叉淡化

---

## 23. 推荐实现阶段

## Phase 1：基础播放

实现：

- miniaudio 接入
- IAudioBackend
- MiniaudioBackend
- NullAudioBackend
- AudioDevice
- AudioHandle
- AudioVoicePool
- AudioClip
- Play
- Stop
- Pause
- Resume
- SetVolume
- SetPitch
- Music/Sfx/Master Bus
- Editor Preview

验收标准：

- 可以播放 WAV、MP3、FLAC
- 可以重复播放短音效
- BGM 可以循环
- 可以设置 Master/Music/Sfx 音量
- 退出时无崩溃
- 退出时无明显音频资源泄漏
- 无音频设备时自动使用 NullAudioBackend

---

## Phase 2：场景音频

实现：

- AudioSourceComponent
- AudioListenerComponent
- AudioSystem
- 3D Position
- Distance Attenuation
- `play_on_awake`
- Source 跟随 Transform
- Scene 卸载停止关联 Voice

验收标准：

- Source 随 Entity 移动
- Listener 随 Camera 移动
- Entity 销毁后 Voice 正确停止
- Scene 卸载后无残留场景音频
- 多 Listener 时编辑器产生警告
- 3D 音效远离 Listener 时音量正确衰减

---

## Phase 3：游戏可用性

实现：

- AudioEvent
- 随机 Clip
- 权重
- 随机 Volume
- 随机 Pitch
- Event Cooldown
- Event Max Instances
- Voice Priority
- Voice Stealing
- FadeIn
- FadeOut
- Bus Fade
- Audio Debugger

验收标准：

- 高频脚步不会无限创建 Voice
- 同类碰撞音不会瞬间淹没混音
- Voice 达到上限时系统稳定
- 旧 Handle 不会误控制新 Voice
- 调试面板能显示全部活动 Voice
- AudioEvent 随机结果符合权重配置

---

## Phase 4：按需求扩展

仅在项目确实需要时实现：

- Music CrossFade
- Snapshot
- 简单遮挡
- Low-pass Filter
- 对白字幕同步
- 音乐分层
- Voice 虚拟化
- 热重载
- 音频性能抓取
- 后端切换

---

## 24. 测试要求

至少实现以下测试。

### 24.1 AudioHandle 测试

- 分配 Voice 后 Handle 有效
- Voice 释放后旧 Handle 无效
- 槽位复用后 generation 改变
- 旧 Handle 不会控制新 Voice
- Invalid Handle 操作不崩溃

### 24.2 VoicePool 测试

- 正确分配空闲槽位
- 达到最大 Voice 后拒绝或抢占
- Persistent Voice 不被普通 Voice 抢占
- Stop 后槽位回收
- 自然播放完成后槽位回收

### 24.3 AudioEvent 测试

- 空 Clip 列表不会崩溃
- 单 Clip 正常播放
- 多 Clip 权重选择正常
- Volume 随机范围正确
- Pitch 随机范围正确
- Cooldown 生效
- Max Instances 生效
- PerEntity 并发生效

### 24.4 Scene 生命周期测试

- Source 创建后可以播放
- Entity 销毁时停止声音
- Scene 卸载时停止所有场景 Voice
- Persistent Music 不随普通 Scene 卸载
- AudioDevice 先于 Scene 销毁时应有保护

### 24.5 后端测试

- 音频设备初始化失败时进入 Null Backend
- 重复 Initialize 不崩溃
- 重复 Shutdown 不崩溃
- 加载不存在文件时返回失败
- Streaming Voice 正确释放文件句柄
- 应用退出时无后台线程残留

---

## 25. 日志与错误处理

建议增加音频日志分类：

```cpp
LOG_INFO("Audio", "Audio device initialized: {}", device_name);
LOG_WARNING("Audio", "Multiple active audio listeners found.");
LOG_WARNING("Audio", "Audio voice pool exhausted.");
LOG_ERROR("Audio", "Failed to load audio clip: {}", clip_path);
```

避免每帧重复刷日志。

以下警告应带限频：

- Voice 池满
- 找不到 Clip
- 找不到 Event
- 多 Listener
- Streaming Voice 超限
- 无音频设备

---

## 26. 性能预算

针对目标项目规模，建议预算：

```text
最大 Voice:
    128

典型活动 Voice:
    20～50

最大 Streaming Voice:
    8

典型 Streaming Voice:
    1～4

Listener:
    1

Bus:
    6

AudioSourceComponent:
    数百个可以存在
    仅更新活动中的 Source

高频位置更新:
    每帧仅更新正在播放的 3D Voice
```

不要每帧：

- 遍历全部 AudioClip
- 遍历全部 Asset
- 查询字符串路径
- 加载资源
- 进行音频文件 IO
- 重建所有后端声音对象

---

## 27. 最终最小类集合

```text
资产
    AudioClip
    AudioClipDocument
    AudioEvent
    AudioEventDocument

运行时
    AudioDevice
    AudioHandle
    AudioVoice
    AudioVoicePool
    AudioBus
    AudioPlayOptions
    AudioFadeState

ECS
    AudioSourceComponent
    AudioListenerComponent
    AudioSystem

后端
    IAudioBackend
    MiniaudioBackend
    NullAudioBackend

编辑器
    AudioClipEditor
    AudioEventEditor
    AudioDebugger
```

核心概念边界：

```text
AudioClip
    声音资源

AudioEvent
    如何选择和播放声音

AudioVoice
    一次正在进行的播放

AudioSourceComponent
    场景实体上的持续发声者

AudioListenerComponent
    场景中的听者

AudioDevice
    全局播放实例和设备管理

MiniaudioBackend
    底层实现细节
```

---

## 28. 实现原则总结

1. 使用 miniaudio，不自行编写底层音频设备和解码器。
2. miniaudio 只能存在于 Backend 模块。
3. AudioClip、AudioEvent 和 AudioVoice 必须分离。
4. Gameplay 只能持有 AudioHandle。
5. AudioHandle 必须使用 index + generation。
6. 一次性音效直接调用 Audio API，不动态创建组件。
7. 持续跟随实体的声音使用 AudioSourceComponent。
8. 第一版只允许一个 Listener。
9. 使用固定 Bus 层级，不实现任意混音图。
10. Voice 数量必须有限制。
11. AudioEvent 必须支持并发限制和冷却。
12. 长音频使用 Streaming。
13. 3D 音效推荐 Force Mono。
14. 音频回调中禁止执行文件、反射、ECS 和字符串逻辑。
15. 必须提供 NullAudioBackend。
16. ResourceManager 不负责具体播放逻辑。
17. Asset 依赖记录在 AssetDocumentHeader 中。
18. 第一版先完成稳定播放，再逐步增加高级功能。
