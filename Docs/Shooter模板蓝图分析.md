# Shooter 模板蓝图分析

## 文档范围

本文记录 `/Game/Shooter` 中与持枪表现直接相关的动画蓝图、Control Rig 和武器蓝图。分析依据是当前项目资产中的实际节点与引用关系，而不仅是资产名称。

## 总体结构

Shooter 模板将同一名角色拆成两套显示路径：

```text
本地玩家
└─ 第一人称手臂 Mesh
   └─ ABP_FP_Weapon / ABP_FP_Pistol
      └─ Control Rig 校正 + Arms Montage

服务器和其他客户端看到的角色
└─ 第三人称全身 Mesh
   └─ ABP_TP_Rifle / ABP_TP_Pistol
      └─ 移动状态机 + AimOffset
```

第一人称蓝图主要服务本地玩家的摄像机和手臂表现；第三人称蓝图负责角色在世界中的完整姿势，也是远程客户端观察到的主要动画。

## 资产对应关系

| 资产 | 类型 | 主要职责 | 当前引用方 |
| --- | --- | --- | --- |
| `ABP_FP_Weapon` | Anim Blueprint | 通用武器的第一人称手臂姿势 | 步枪、榴弹发射器 |
| `ABP_FP_Pistol` | Anim Blueprint | 手枪的第一人称手臂姿势 | 手枪 |
| `ABP_TP_Rifle` | Anim Blueprint | 步枪类第三人称全身移动与瞄准 | 步枪、榴弹发射器、NPC |
| `ABP_TP_Pistol` | Anim Blueprint | 手枪第三人称全身移动与瞄准 | 手枪 |
| `Ctrl_HandAdjusment` | Control Rig | 通用武器双手、肩膀和脊柱校正 | `ABP_FP_Weapon` |
| `Ctrl_HandAdjusment_Pistol` | Control Rig | 手枪专用持枪校正 | `ABP_FP_Pistol` |

武器蓝图的实际映射如下：

| 武器蓝图 | 第一人称 AnimBP | 第三人称 AnimBP |
| --- | --- | --- |
| `BP_ShooterWeapon_Rifle` | `ABP_FP_Weapon` | `ABP_TP_Rifle` |
| `BP_ShooterWeapon_GrenadeLauncher` | `ABP_FP_Weapon` | `ABP_TP_Rifle` |
| `BP_ShooterWeapon_Pistol` | `ABP_FP_Pistol` | `ABP_TP_Pistol` |

## 第一人称动画蓝图

### `ABP_FP_Weapon`

它由步枪和榴弹发射器共用。AnimGraph 的主要链路是：

```text
Copy Pose From Mesh
        ↓
Ctrl_HandAdjusment
        ↓
Slot "Arms"
        ↓
Output Pose
```

- `Copy Pose From Mesh`：从角色基础 Mesh 复制姿势，让第一人称手臂继承角色的基础运动。
- `Ctrl_HandAdjusment`：根据 `Aim Target` 修正双手、肩膀、脊柱和头部。
- `Slot "Arms"`：叠加开火、换弹等 Montage。
- `Output Pose`：输出最终第一人称手臂姿势。

EventGraph 主要负责：

- 在初始化时获取 Pawn、`ShooterCharacter`、第一人称 Mesh 和第一人称摄像机。
- 根据速度计算 `IsMoving`、`bIsInAir` 和前后/左右移动分量。
- 根据 Controller Rotation 计算 `PitchN`。
- 从第一人称摄像机向前进行约 10000 单位的射线检测，并将命中点或射线终点写入 `Aim Target`。
- 通过 `Is Locally Controlled` 分支限制本地摄像机相关逻辑。

最后一项是多人化的关键：远程代理和专用服务器不能假定自己拥有本地 Controller 或第一人称摄像机。

### `ABP_FP_Pistol`

它的 EventGraph 和 AnimGraph 与 `ABP_FP_Weapon` 基本相同，主要差别是使用 `Ctrl_HandAdjusment_Pistol`，从而采用手枪专用的握持位置和 IK 对齐。

因此这两个第一人称 AnimBP 是同一套逻辑的两种武器规格，而不是两套完全独立的动画系统。

## 第三人称动画蓝图

### `ABP_TP_Rifle`

它负责步枪类角色的世界空间全身姿势。AnimGraph 的核心链路是：

```text
LocomotioStateMachine
          ↓
MF_Rifle_Idle_ADS + AO_Rifle
          ↓
Layered Blend Per Bone
          ↓
Output Pose
```

- `LocomotioStateMachine`：处理待机、行走、跑动、跳跃等基础移动。
- `MF_Rifle_Idle_ADS`：提供步枪瞄准状态的基础姿势。
- `AO_Rifle`：根据瞄准角度调整上半身。
- `Layered Blend Per Bone`：让下半身继续移动，同时在上半身叠加持枪瞄准。

EventGraph 从 Character 和 CharacterMovement 计算：

- `Velocity`
- `GroundSpeed`
- `ShouldMove`
- `IsFalling`
- `Direction`
- `PitchN`

其中 `Direction` 会结合 `bOrientRotationToMovement` 选择计算方式，并限制在模板需要的角度范围内。

### `ABP_TP_Pistol`

结构与 `ABP_TP_Rifle` 基本一致，但使用手枪资源：

- `MF_Pistol_Idle_ADS`
- `AO_Pistol`
- 手枪对应的移动和持枪动画

两者的主要区别是动画资产和持枪姿势，而不是状态计算方法。

## Control Rig

`Ctrl_HandAdjusment` 和 `Ctrl_HandAdjusment_Pistol` 的求解结构基本一致，包含：

- 根据 `Aim` 控制器进行空间转换和 Aim Math。
- 右臂 Basic IK：`upperarm_r`、`lowerarm_r`、`hand_r`，目标为 `Ctrl_Hand_R`。
- 左臂 Basic IK：`upperarm_l`、`lowerarm_l`、`hand_l`，目标为 `Ctrl_Hand_L`。
- 左右肩膀控制：`Ctrl_Shoulder_L`、`Ctrl_Shoulder_R`。
- 脊柱 FABRIK：从 `spine_01` 到 `spine_04`。
- 头部控制 `Ctrl_Head` 和整体 `BodyOffset`。
- 通过四元数弹簧插值平滑瞄准旋转，减少突然跳变。

两者节点算法非常接近。手枪版本的主要差别预计位于 Rig 控制器初始变换、层级默认值和武器握持偏移，而不是求解流程本身。

## 武器蓝图与角色的连接方式

每种 `BP_ShooterWeapon_*` 都配置了两种 AnimInstance Class：

- `FirstPersonAnimInstanceClass`
- `ThirdPersonAnimInstanceClass`

角色激活武器时，`AShooterCharacter::OnWeaponActivated` 会：

1. 更新弹药 UI。
2. 给第一人称 Mesh 设置武器对应的第一人称 AnimBP。
3. 给第三人称 Mesh 设置武器对应的第三人称 AnimBP。

武器 Actor 自身也包含两套 Mesh：

- First Person Mesh：`OnlyOwnerSee`。
- Third Person Mesh：`OwnerNoSee`。

这样本地玩家看到第一人称武器，其他玩家看到第三人称武器。

## 多人化时的职责边界

不应把动画蓝图本身当成需要复制的状态。优先复制真正的游戏状态，然后让每台机器上的 AnimBP 自行计算姿势。

需要同步的典型状态：

- 当前装备的武器 Actor 或武器类型。
- 服务器确认的开火事件。
- 换弹和武器切换事件。
- 弹药、生命和死亡状态。
- 必要的远程瞄准方向。

通常不需要单独复制：

- `GroundSpeed`、`Velocity`、`IsFalling` 等可从复制后的移动状态重新计算的变量。
- 本地第一人称摄像机射线结果。
- 纯本地后坐力和 HUD 动画。

## 已知风险点

- 第一人称 AnimBP 访问 Controller 或 Camera 前必须经过本地控制和有效性判断。
- 专用服务器没有本地玩家、Viewport 或 HUD，不能执行 `AddToViewport`。
- 只复制 Weapon Actor 还不够；当前武器引用、激活状态、弹药和开火权威仍需逐步补齐。
- 第三人称开火 Montage 必须让其他客户端收到表现通知，不能只在开火客户端播放。

