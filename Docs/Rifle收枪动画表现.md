# Rifle 收枪动画表现

维护日期：2026-10-17；适用 UE 5.6.1。
Local 方案 2 保持；临时 K 网络预览已退出，正式 Gameplay 见[走跑接入](WalkSprint与自动收枪.md)。
本文说明动画资源与图结构，实际姿态目标由正式 Sprint 驱动。

## 当前正式方案

正式图为 `/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player`。
父类保持运行时 ShooterThirdPersonAnimInstance，不依赖实验 AnimInstance。
WeaponAction 的 ArmedAim 内沿用 RifleReady：ArmedAim ↔ AimTransition ↔ LowReady。

AimTransition 使用 MM_Rifle_AimToLowReady_Compact，LowReady 使用 MM_Rifle_LowReady_Compact。
动态 ArmedAimPose 是分层底姿态，肩臂按原 RifleReloadWeightedUpperBody Mask 做 Local 混合。
运行时 ShooterRifleReadyLayer 保持子播放器相关，姿态 Alpha 按真实 t/T 渐进。
该自定义层只用于 AimTransition；稳定 LowReady 使用标准 Layered Blend Per Bone，权重恒为 1。
LowReady 保持相同 BlendMask、Local Space 和头颈修正，未增加新的 C++ 节点。
正式三个选片槽均引用同一 Compact 过渡；旧 Relaxed / CorrectedGrip / Down30 / Down60 保留。
不接入方案 3 的额外反向边界混合；素材原长约 0.833333 秒。
过渡序列原生 Rate Scale 为 2.6，双向全程约 0.320513 秒；运行时只选择 ±1 方向。
首发等待真实抬枪端点；缓冲、取消和输入优先级见走跑专题。
中途反向保持当前累计时间，实时改俯仰不会重置动画或切换素材。

当前采用的 Rifle 素材目录：

```text
/Game/Characters/Mannequins/Anims/Rifle
    MM_Rifle_AimToLowReady_Compact
    MM_Rifle_LowReady_Compact
    MM_Rifle_Sprint_Compact_Additive
```

Compact 三段资源复制自已验收的贴身跑姿实验；实验已清理，原 Relaxed 资源保留。
LowReady Base 保留呼吸；过渡端点匹配新肩臂姿态，循环 Additive 驱动小幅胸肩摆动。
AimTransition 与 LowReady 均在原头颈修正前插入标准 Apply Additive。
过渡 Alpha 为真实播放器 t/T 经 Ease In Out（Exponent=2）；稳定 LowReady 的 Alpha=1。
FPP_Run 的 BlendSpace 为 SprintStride Leader，两处循环序列为 Follower，采用 Sync Group。
Additive 为 Local Space，以正式 Compact LowReady 第 0 帧为参考，不引用实验 Base。
Shooter/Animation 仅存放 AnimBP，FP Rifle Montage 也已迁至 Characters 的 Rifle 目录。
原 Ctrl_HandAdjusment 系列与 CtrlRig_FPWarp 位于 Characters/Mannequins/Rigs。
头颈 Runtime 节点位于两个子状态的肩臂分层及跑姿 Additive 之后。
该节点仍只改变 neck_01/02 与 head；本次没有修改其算法或参数。
按真实播放器进度平滑解除 ADS 偏头并端正 Roll，保留原面部仰角。
角色前向与竖直方向在游戏线程 PreUpdate 缓存；生产父类与原 AO、IK 顺序保持。
原始 Mixamo、ArmedAim、AO_Rifle、Skeleton、Socket 和 Blender 调整输入保留。
本轮未调整 Mask 权重、Aim IK、LeftHand IK、FP Proxy 或相机层级。
本地 FP 沿用 CopyPose 和现有净空机制，远端只消费自己的 TP 动画实例。

## 正式姿态与旧预览退出

正式姿态只消费 CMC 的实际 Sprint，K 蓝图节点、预览模式开关及复制组件均已删除。
AnimInstance 使用唯一目标和正式动作中断规则；没有调试模式射击放行分支。
Rifle、AWP、GrenadeLauncher 共用现有 Rifle 素材、Mask 和头颈修正；Pistol 保持原持姿。
正反方向由现有播放器选择，速度直接读取序列原生 Rate Scale，不写运行时加速倍率。
首发在真实抬枪端点后走原 GAS；窗口与取消规则见走跑专题。
Owner 自行预测 Sprint，Observer 消费服务器 Sprint；Dedicated 不依赖渲染动画推进射击事务。

旧网络预览的验收材料保留原貌，仅供历史查阅，见[网络预览报告][net]。

## 调整工具与回归

Blender 工具与必要输入见[工具链文档](Blender动画编辑工具链.md)。
正式图的三个素材字段可在 Class Defaults 中查看，Local 节点也保留编辑器外壳。
旧分段 ProductionApply 已移除，避免重新覆盖正式选片；不再提供一次性正式接入工具。
保留 `Run.ps1 -Mode Measure -Production` 作为内存只读测量入口，不保存生产资产。
analyze_segmented.py 用于保留的分段回退夹具，不把旧阈值检查作为 Local 方案验收。

```text
Automation RunTests ShootGame.Animation.RifleReady
```

当前正式网络回归为三枪各自的 Listen、Dedicated 与延迟丢包，共 9 项。
本轮另在真实生产角色采集 0°、±60° 的 FP / TP，不替换运行时动画类。
同一过滤器还执行 Standalone 六俯仰检查，共 10 项；角度为 0、-29.99、-30、-50、-50.01、-65°。
网络夹具使用真实 NetDriver、拥有连接、W / Shift 输入与原 Fire / Reload 入口。
纯姿态切换在固定权威武器上检查 Shot / Ammo 增量为 0；开火继续验证一次首发。

## 手动检查

在正式 Lvl_Shooter 设置 2 Players / Listen Server，依次装备 Rifle、AWP 和 GrenadeLauncher。
一端用 W / Shift 走跑，从另一端观察收抬枪、中途反向及俯仰；再交换操作者。
本地检查第一人称肩臂和枪械可见性，再触发原 Fire / Reload 确认动作仍可执行。
Dedicated 对照选 Play As Client、2 Players；主观画面不由无头 Automation 判定。

三套跑姿实验、F4–F8 切换角色与临时采集资料已清理。
原 Segmented50 地图已恢复实验前基线，用于旧分段素材对照。
正式接入验证使用 Lvl_Shooter 的生产角色及走跑输入，不把实验地图截图替代生产证据。
历史报告原文保留，已删除的截图、JSON、CSV 和脚本路径可能失效，见[验证索引][index]。
本次接入与清理的范围、验证和边界见[生产接入记录][posture]。

[net]: 验证/RifleReadyNetworkPreview_20261013/实施与验证.md
[index]: 验证/README.md

[posture]: 开发记录/2026-10-14-1224-Rifle姿态生产接入与清理.md

本次目录调整与提交说明见[动画目录迁移记录][layout]。

[layout]: 开发记录/2026-10-14-1622-动画资源目录统一与Rifle验收提交.md

稳定 LowReady 标准节点替换见[原记录][standard]。

贴身跑姿生产接入见[接入验证](验证/RifleRunProduction_20261017/生产接入与验证.md)。
后续清理与最终证据见[清理核对](验证/RifleRunCleanup_20261017/验收清理.md)。

[standard]: 开发记录/2026-10-14-1630-LowReady标准分层节点.md
