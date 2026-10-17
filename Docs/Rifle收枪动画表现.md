# Rifle 收枪动画表现

维护日期：2026-10-16；适用 UE 5.6.1。
Local 方案 2 保持；临时 K 网络预览已退出，正式 Gameplay 见[走跑接入](WalkSprint与自动收枪.md)。
本文说明动画资源与图结构，实际姿态目标由正式 Sprint 驱动。

## 当前正式方案

正式图为 `/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player`。
父类保持运行时 ShooterThirdPersonAnimInstance，不依赖实验 AnimInstance。
WeaponAction 的 ArmedAim 内沿用 RifleReady：ArmedAim ↔ AimTransition ↔ LowReady。

AimTransition 使用 MM_Rifle_AimToLowReady_Relaxed，LowReady 使用 MM_Rifle_LowReady_Relaxed。
动态 ArmedAimPose 是分层底姿态，肩臂按原 RifleReloadWeightedUpperBody Mask 做 Local 混合。
运行时 ShooterRifleReadyLayer 保持子播放器相关，姿态 Alpha 按真实 t/T 渐进。
该自定义层只用于 AimTransition；稳定 LowReady 使用标准 Layered Blend Per Bone，权重恒为 1。
LowReady 保持相同 BlendMask、Local Space 和头颈修正，未增加新的 C++ 节点。
正式三个选片槽均引用同一 Relaxed 过渡；旧 CorrectedGrip / Down30 / Down60 保留作回退。
不接入方案 3 的额外反向边界混合；素材原长约 0.833333 秒。
过渡序列原生 Rate Scale 为 2.6，双向全程约 0.320513 秒；运行时只选择 ±1 方向。
首发等待真实抬枪端点；缓冲、取消和输入优先级见走跑专题。
中途反向保持当前累计时间，实时改俯仰不会重置动画或切换素材。

当前采用的 Rifle 素材目录：

```text
/Game/Characters/Mannequins/Anims/Rifle
    MM_Rifle_AimToLowReady_Relaxed
    MM_Rifle_LowReady_Relaxed
```

六段 Rifle 序列统一位于上述目录；新资源采用验收的 35% 肩位与 SmoothSeam 端点。
Shooter/Animation 仅存放 AnimBP，FP Rifle Montage 也已迁至 Characters 的 Rifle 目录。
原 Ctrl_HandAdjusment 系列与 CtrlRig_FPWarp 位于 Characters/Mannequins/Rigs。
头颈 Runtime 节点位于两个子状态的 Local 肩臂分层之后，只改变 neck_01/02 与 head。
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

回归覆盖 Rifle Standalone 六个俯仰，以及三枪各自的 Listen、Dedicated 与延迟丢包。
网络夹具使用真实 NetDriver、拥有连接、W / Shift 输入与原 Fire / Reload 入口。
纯姿态切换在固定权威武器上检查 Shot / Ammo 增量为 0；开火继续验证一次首发。

## 手动检查

在正式 Lvl_Shooter 设置 2 Players / Listen Server，依次装备 Rifle、AWP 和 GrenadeLauncher。
一端用 W / Shift 走跑，从另一端观察收抬枪、中途反向及俯仰；再交换操作者。
本地检查第一人称肩臂和枪械可见性，再触发原 Fire / Reload 确认动作仍可执行。
Dedicated 对照选 Play As Client、2 Players；主观画面不由无头 Automation 判定。

一次性 Local / Mesh / Boundary 与五轮肩部头部原型已清理。
仅保留原 Segmented50 回退地图及必要依赖，当前验证直接使用正式角色与走跑输入。
该地图仍用于素材和 Natural Pitch Mask 对照，不代表正式 Local 方案或真实 FP 角色。
历史报告原文保留，已删除的截图、JSON、CSV 和脚本路径可能失效，见[验证索引][index]。
本次接入与清理的范围、验证和边界见[生产接入记录][posture]。

[net]: 验证/RifleReadyNetworkPreview_20261013/实施与验证.md
[index]: 验证/README.md

[posture]: 开发记录/2026-10-14-1224-Rifle姿态生产接入与清理.md

本次目录调整与提交说明见[动画目录迁移记录][layout]。

[layout]: 开发记录/2026-10-14-1622-动画资源目录统一与Rifle验收提交.md

稳定 LowReady 标准节点替换见[本轮记录][standard]。

[standard]: 开发记录/2026-10-14-1630-LowReady标准分层节点.md
