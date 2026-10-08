# TP 落脚 Notify

## 当前行为

声音由 `UShooterAnimNotify_Footstep` 发出；距离脚步组件及其 Character 引用已经删除。
四种武器的 TP 移动状态都使用 Unarmed 的 `BS_Idle_Walk_Run`。
该 BlendSpace 引用 8 个 Walk、8 个 Jog 和一个 Idle 序列，共有 27 个采样点。
只给 16 个移动序列添加 `ShooterFootsteps` 轨道，不修改 Idle、FP AnimBP 或同步标记。
Notify 时间沿用资源作者的 `L` / `R` 标记，分别填写 `foot_l` / `foot_r`。
用户已通过当前 Notify 表现；时刻沿用作者标记，没有按脚高最低点重新计算。

继续使用五个 Kenney 混凝土 SoundWave 和 `SA_Footsteps`，音量 0.65。
每次随机选择声音，音高范围 0.96～1.04，在对应脚骨骼的世界位置播放。
Notify 对象由资产共享，不保存某个角色的计时器或上次播放状态。
当前随机声音变体允许相邻重复。

只有玩家 Character 的 TP Mesh 可以发声，共享序列被 FP Mesh 使用时直接忽略。
Dedicated、死亡、离地、水平速度不足 10cm/s、脚骨骼或声音缺失时直接忽略。
事件权重阈值为 0.1，并禁用 Dedicated 触发。
BlendSpace 保持已有的 `HighestWeightedAnimation` 模式，以免多个混合样本同时发声。
本地隐藏 TP Mesh 已配置 `AlwaysTickPoseAndRefreshBones`，不需要额外修改可见性。

没有声音 RPC、复制字段、AI 听觉事件或 GAS 写入。
Owner 读取本地 TP 动画，Remote 读取复制移动驱动的 TP 动画。
Listen 主机只使用自己世界中的 TP Mesh；本轮不增加 NPC、多地表或落地专用声音。

## 外部依据与本地核对

- [Epic UE5.6 Animation Notifies](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-notifies-in-unreal-engine?application_version=5.6)：脚步事件、权重阈值与 Dedicated 过滤。
- [Epic UE5.6 Blend Spaces](https://dev.epicgames.com/documentation/en-us/unreal-engine/blend-spaces-in-unreal-engine?application_version=5.6)：最高权重样本的 Notify 模式。

本地 UE5.6.1 的 `BlendSpace.cpp` 实际按最高权重样本索引收集 Notify。
`AnimationBlueprintLibrary.cpp` 的事件创建函数不自动调用 Notify 的编辑器创建钩子。
因此配置脚本显式写入专用轨道、Dedicated 开关和权重，不依赖该钩子。
事件数组对 Python 受保护，原生 `ConfigureEditorEvent` 仅在编辑器目标中暴露。
它只配置当前 Notify 所属事件；脚本通过动画库创建事件，再调用此入口。
`UAnimNotify` 提供携带 `FAnimNotifyEventReference` 的三参数接口，本次使用此签名。
官方机制与本地版本一致；作者标记的具体落脚精度属于项目素材，文档不能替代视觉验收。

## 五项不变量的范围确认

- Owner Immediate Feedback：只调整本地移动音效的触发时机。
  Shot Intent、Owner Fire 表现和确认路径未改动；脚步请求和实际混音分别取证。
- Local Prediction Obeys Weapon Rules：新增 Notify 必须服从死亡、离地和停止限制。
  用真实 Notify 守卫测试和停步实跑取证；武器节拍、弹药预算及 Reject 不在本次范围。
- Server Is Final Authority：脚步没有玩法结果，不写服务器状态。
  Dedicated 静音及源码无 RPC、复制、AI 听觉写入用于证明边界，不宣称完成射击权威回归。
- Remote Is Confirmed Only：远端声音来自已有复制移动驱动的动画。
  用 Dedicated 和 Listen 的真实远端播放请求取证；远端 Fire 确认入口保持原状。
- Exactly One Authority Result：本轮新增的重复风险是 FP/TP 双 Mesh 与混合样本。
  用 FP 守卫、最高权重配置、同帧计数和旧组件不存在的配置检查取证。
  脚步不创建权威事务；完整射击结果计数仍以独立回归为准。

## 复现与验证边界

通过 UE Python 执行 `Scripts/Development/ConfigureTPFootstepNotifies.py`。
`-FootstepNotifyMode=preview` 只读取资源、事件、标记和左右脚姿态。
`apply` 备份目标资产到 `Saved/FootstepsNotify/Before_*`，再仅重建专用轨道。
`verify` 在新进程中检查保存后的事件数量、左右脚、时间、声音及 Dedicated 设置。

`ShootGame.Audio.Footsteps` 包含声音配置、旧组件缺席检查和 Notify 守卫两项测试。
`ShootGame.Footsteps.Probe` 用正常移动输入观察生产 Notify，不手动调用播放。
探针检查拥有者左右脚、远端请求、停步静音及同帧重复。
Dedicated 请求观测持续到整个测试会话结束，准备标记不提前终止声音错误检测。
`-ShootGameFootstepRecord` 记录真实 Master 混音；此时不要使用 `-NoSound`。
请求计数不能证明声音可听，混音录音也不能替代人工声画同步验收。
本机 `Lvl_Test` 的 NullRHI 运行曾得到空混音；实际试听使用 `-RenderOffscreen`。
射击地图的 NPC 枪声会污染录音，最终试听使用没有射击提交的 `Lvl_Test` 实跑。
执行结果和完整射击回归的限制见本轮开发记录。
