# Rifle 收枪动画表现

适用 UE 5.6.1；2026-10-11 接入临时蓝图按键。这里只改变动画表现。

## 操作与选片

在正式 Shooter 关卡中 Play，装备 Rifle 后按 K：ArmedAim ↔ LowReady。
过渡中再次按 K，从当前时间反向；按住不重复触发。
输入节点位于 BP_ShooterCharacter 的 EventGraph，可直接更换 Key。
该入口只接受本地控制且装备有效 Rifle 的角色，不发送 RPC、不复制姿态目标。

| 新过渡入场时的动画俯仰 P | 素材后缀 |
|---|---|
| P < -50° | Down60 |
| -50° ≤ P ≤ -30° | Down30 |
| P > -30° | CorrectedGrip |

选片采用现有 AimPitchDegrees，处理角度转换时仅容忍 0.0001° 浮点误差。
活动过渡锁定素材；实时改变俯仰不会重置时间，也不会中途换片。
从 LowReady 新入场抬枪时，重新按当前俯仰选片，从末端反向播放。
三段素材均约 0.833333 秒，播放倍率仍为 ±1。

资源目录：

```text
/Game/Characters/Mannequins/Anims/Rifle
    MM_Rifle_AimToLowReady_CorrectedGrip
    MM_Rifle_AimToLowReady_Down30
    MM_Rifle_AimToLowReady_Down60
```

原始 MM_Rifle_AimToLowReady 和 MM_Rifle_LowReady 保留在 Shooter 动画目录。
原始 Mixamo 素材、MF_Rifle_Idle_ADS、AO_Rifle、Skeleton 和武器 Socket 均未改动。

## 图与表现边界

ABP_TP_Player 继续继承运行时 ShooterThirdPersonAnimInstance。
WeaponAction 仍是 ArmedAim ↔ Reload，ArmedAim 内新增 RifleReady 子状态机：
ArmedAim ↔ AimTransition ↔ LowReady。
原 Locomotion、Arms Slot、Reload 与第一人称 CopyPose 的接线保留。
生产图不依赖实验 AnimInstance、测试 Skeleton 或 Natural Pitch Blend Mask。

收枪过程按播放时间逐渐释放原 Aim IK 表现权重，LowReady 时不纠正回瞄准方向。
原 LeftHand IK 保留；没有新增 IK 求解算法，也没有改 Blend Mask 权重。
Reload 的曲线和 IK 恢复继续由原管线处理。
开火还检查正在播放的 Montage，避免瞬时 Fire Tag 在同帧结束后遗漏动画中断。
换弹、装备、开火、死亡、失去本地控制或换武器时清除调试目标。
清除只影响动画；不会禁止开火、换弹或其它 Gameplay 行为。
这是临时视觉入口，尚未定义 LowReady 的 Gameplay 状态或网络复制规则。

## 调整与检查

Blender 输入和重放入口见[工具链文档](Blender动画编辑工具链.md)。
三个资源字段可在 ABP_TP_Player Class Defaults 中查看。
生产图只读测量入口：Run.ps1 -Mode Measure -Production。
阈值与锁定逻辑位于 ShooterThirdPersonAnimInstance::UpdateRifleReadyAnimation。
按键只在蓝图，未来接入正式状态来源时可移除 K 节点并替换该表现目标入口。

保留的定向 Editor 工具：

```text
-run=ShooterRifleSegmented -ProductionApply
-run=ShooterRifleSegmented -ProductionMeasure
Automation RunTests ShootGame.Animation.RifleReady.PIE
```

ProductionApply 只处理 ABP_TP_Player 和 BP_ShooterCharacter，执行前先保护基线。
ProductionMeasure 在内存中复制正式图、注入固定输入，不保存新的测试资产。
输出目录为 Saved/RifleTools/ProductionMeasure，可用 analyze_segmented.py 分析。
PIE 用真实 K 输入检查正式图，固定角度且移除临时世界的 NPC，避免外部输入和死亡干扰。
这些自动检查覆盖接入与时间行为，不替代肩臂、枪械轨迹的人工视觉验收。

正式关卡视觉检查：装备 Rifle，分别看 0°、-30°、-50°、-55°、-65°，反复按 K。
在过渡中反向，检查双手握持、肩部连续性和枪械降低方向；再试移动、开火、换弹和换枪。
原隔离地图仍保留，适合对照关闭 IK 的素材和 Natural Pitch Mask 效果。

## 依据与现状

[Epic UE 5.6 状态机文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/state-machines-in-unreal-engine?application_version=5.6)
支持状态内部动画图及入场重置。安装版本的 AnimNode_SequencePlayer.cpp 核对了
StartPosition、负 PlayRate 与累计时间行为；本项目沿用已有 Native 更新快照。
实验父类属于 Editor 夹具，生产图保留运行时父类，是接入与隔离方案的主要区别。

本轮证据见[迁移与生产接入记录](验证/RifleProduction_20261011/迁移与生产接入记录.md)。
