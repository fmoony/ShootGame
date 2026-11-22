# Stance-Aware Weapon Pose 实验

日期：2026-11-16；UE 5.8.4；分支 codex/stance-aware-weapon-pose。
工作树：ShootGame_UE58。
当前运行目录：
/Game/Experiments/Stance_20261022/StanceAware_20261116/Refined。

本轮建立了阶段 A、B 的实验实现，完成数值回归及代表画面检查。
**整体视觉验收未通过，尚未达到生产接入条件。**
第一人称肩部近裁切、左手握持脱离、插值握持残差和毫米级鞋面穿地仍保留。
阶段 C 未实施，生产角色、WeaponAction、GA、网络和 Gameplay 未修改。

## 1. 根因与审计结论

### 资源问题

原始来源只有四个移动方向和一个单膝跪 Idle。
旧 Idle 骨盆约 39 cm，移动骨盆约 64～75 cm，两者不属于一致的半蹲支撑姿态。
各方向的父层旋转、支撑时序和足部姿态也不一致。
即使每个主方向的 foot／ball 骨骼为正，旋转插值仍可能让鞋部进入地面。

本轮先从磁盘重载图、九个样本和资源，再重采 0.2 秒平滑版本。
其鞋部最低约 -18.03 cm，不能沿用此前零输入平滑报告宣称通过。
直接镜像保留了来源高度和节奏，解决了部分互穿，没有自动解决邻向接触。

初次仅校平各方向脚部后，主方向改善，但混合与起停仍失败。
随后统一半蹲支撑基准、方向时序和步幅，并制作站蹲中间接地姿态。
没有通过抬高整个 Actor 或附加 Root 高度来掩盖问题。

### 架构问题

旧图从 spine_01、Depth=1、权重 1 覆盖站立持枪上身。
其 Mesh Space Rotation Blend 使完整站立躯干姿态主导上半身，来源腰背前倾丢失。

普通绝对持枪姿态也不能直接当作 Additive。
站立 AO 的参考和肢体关系与新半蹲基准不同，直接复用会产生手部位置偏差。
早期静态还原候选在真实图中未通过，因此没有将导入成功当作握持通过。
最终改用真实 Stand 图的五个俯仰输出，制作专用下蹲增量参考。

[审计前资产及连线](AssetAuditBefore.json)与[最终实际连线](AssetAuditAfter.json)。

## 2. 新旧 AnimGraph

### 旧实验

~~~mermaid
flowchart LR
    LM["站立 / 跪 Idle / 八向 BS / 滑铲"] --> C["缓存 NativeLower"]
    C -->|BasePose| L["整段上身分层"]
    H["Stand ADS / Compact LowReady"] --> AO["AO_Rifle"]
    AO -->|spine_01，Depth 1| L
    L --> D["LowerOnly 选择"]
    C --> D
    D --> O["TP 输出"]
~~~

旧实验没有 WeaponAction、Arms Slot、AimIK、LeftHand IK 或 Control Rig。
原 NativeGraph 及其来源 BS、镜像资源完整保留，以 StanceBaseline 显式选择。

### 当前实验

~~~mermaid
flowchart LR
    A["CrouchAlpha"] --> SB["BS_StanceHeight：站蹲接地姿态"]
    DIR["Forward / Right"] --> MB["BS_StanceAware_Lower"]
    SB --> M["Mode 选择与缓存 NativeLower"]
    MB --> M
    W["Stand / Crouch ADS 或 LowReady"] --> WB["武器基准按 CrouchAlpha 混合"]
    M -->|身体基准| L["渐进胸肩分层"]
    WB -->|上身输入| L
    L --> C["缓存 StanceWeaponBase"]
    C --> AO["单次 AO_StanceWeapon"]
    A --> AO
    P["Sin Pitch"] --> AO
    C -->|原身体| S["限定上身 AO 范围"]
    AO --> S
    S --> D["LowerOnly 选择"]
    M --> D
    D --> O["TP 输出"]
~~~

Weapon Base Pose 和 Locomotion 先组合，AO 在这一实验组合之后执行一次。
随后用上身范围限制隔离 AO 对骨盆、双腿及脚部的影响。

- 武器分层：spine_03，Depth=4，Local Rotation 混合。
- 对应 spine_03／04／05 权重为 0.25／0.5／0.75，更深子骨骼为 1。
- pelvis、双腿、spine_01／02 由身体基准保留。
- AO 范围：spine_01、Depth=1，选取已经求值的上身增量结果。
- CrouchAlpha 为 0～1，当前夹具用约 0.25 秒完成站蹲输入变化。
- AO.X 是 CrouchAlpha，AO.Y 是 Sin(Degrees)；实验偏航继续由 Actor 转向承担。
- 实验 FP：Copy Pose From Mesh → Output，没有第二次 AO。

这是本轮实验拓扑，不是要求把生产图机械改成上述顺序。
生产仍为 Hold → AO → WeaponAction → Arms Slot → 分层 → AimIK → LeftHand IK。

## 3. 实际修改范围与资源

### 当前资产

Refined 目录共 25 段序列、三个 BlendSpace／AimOffset 和两个 AnimBP，共 30 项。
其父目录另保留首轮 14 项候选，当前默认图不引用这些候选。
来源 Baked 也保留早期制作输入；这些材料不作为最终通过结果。

- 八段蹲走：前、右前、右、右后、后、左后、左、左前，各一个标准样本。
- CrouchIdle：统一支撑的半蹲静态基准，替代当前播放中的单膝跪姿。
- Stance_0～8：九个站蹲接地姿态，供 BS_StanceHeight 取样。
- CrouchADS、CrouchLowReady：兼容半蹲身体的绝对持枪基准。
- CrouchAO_m90／m45／zero／p45／p90：五个下蹲俯仰样本。
- BS_StanceAware_Lower：中心 Idle 加八方向，保留两轴 0.2 秒平滑。
- BS_StanceHeight：以 CrouchAlpha 取样站蹲基础姿态。
- AO_StanceWeapon：站立三样本、下蹲五样本，共八点，原生三角化插值。
- ABP_StanceAware_TP／FP：标准 Engine.AnimInstance 父类。

站立 ADS 继续使用 MF_Rifle_Idle_ADS，站立 LowReady 使用 Compact 资源。
新 Crouch AO 使用 Mesh Space Additive，参考为 CrouchADS 第 0 帧。
FBX 中保存完整姿态；UE 中明确设置增量类型与参考，并验证还原和运行结果。
没有直接把原绝对姿态修改类型后充当正确增量。

### 制作方式

本轮八向是统一基准与支撑时序的新制作版本，不是未修改的 Source。
脚间距约 36 cm，左右相差半周期，支撑段占 65%，支撑速度为 45 cm/s。
当前实验 Actor 行进速度匹配为 45 cm/s；原基线仍保留 150 cm/s。
这些固定值仅属于隔离夹具，不能直接代表正式 CharacterMovement 参数。

Blender 原生 IK 只用于离线制作真实关节控制链，并烘焙到原骨架。
父层尺度沿用已验证约定，FBX 为 60 FPS、61 点、1 秒，绑定生产 SK_Mannequin。
Root 没有附加高度偏移，当前运行没有脚部 IK 或接地补偿。

Crouch ADS 保留下蹲腰背，以离线手臂求解校准头部与双手的关系。
Crouch AO 最终输入来自真实运行的五个 Stand 姿态。
采集场景、时刻和源 CSV 哈希保存在 RuntimeAimReference.json。
早期未通过的静态 AO 候选及步态候选保存在 Saved，没有作为最终通过证据。

[可编辑输入与制作说明](../../../SourceAssets/StanceCrouch/StanceAware_20261116/README.md)。

### 代码与保护

只修改既有实验夹具的 .cpp／.h，并新增 StanceAware 制作、导入和验证脚本。
C++ 改动为原生节点建图、参数输入、Actor 移动与只读骨骼／枪口／相机采集。
没有自定义动画求值、手写运行时 FK、IK、骨骼混合或回写。

619 个数据／生产代码文件与本轮起点一致。
来源根 README 是明确更新的当前说明，单独记录前后哈希。
原 SourceLayer、EightWay、Mirrored、生产 Skeleton、正式 AnimBP 及生产代码均保留。

旧镜像完整验证入口因夹具 .cpp／.h 已按本轮任务修改而拒绝通过。
新资源回归明确排除这两个已授权源码变更，306 项旧保护数据一致。
两段原镜像的导入、循环和镜像几何验证通过，旧脚本和历史报告没有改写。

## 4. 数值验证

运行使用固定 60 Hz，每三帧读取实际 SkeletalMesh 结果，约 20 Hz 采样。
鞋部由实际骨骼、生产参考网格及权重重建线性蒙皮，再检查高度与双脚三角形相交。
它不等同于所有鞋边、布料、GPU 后处理或实际碰撞体测试。

### 覆盖

- Current32：两种持枪各一次 Idle、32 个方向，共 66 场景、5,680 帧；Actor 固定。
- FinalPresentation：站蹲往返、八向启停、邻向切换和 11 个俯仰，
  共 58 场景、3,480 帧；Actor 按 45 cm/s 实际行进。
- 俯仰包含 -90、-45、-10、-1、-0.5、0、0.5、1、10、45、90°。
- FinalRaw32：同样输入下的 LowerOnly 对照。
- 25 段资源从磁盘重载，检查 RAW／COMPRESSED 姿态、循环和 Additive 参考。

### 接地与互穿

| 项目 | 当前结果 |
| --- | ---: |
| 32 方向最低 foot 骨骼 | +8.171 cm |
| 32 方向最低 ball 骨骼 | +0.683 cm |
| 32 方向鞋部最低点 | -0.122 cm |
| 站蹲往返鞋部最低点 | -0.231 cm |
| Idle／移动过渡鞋部最低点 | -0.218 cm |
| 邻向切换鞋部最低点 | -0.122 cm |
| 全部姿态切换最低鞋部点 | -0.238 cm |
| 两轮采集的双脚表面相交 | 0 帧 |

本轮保留非零地面误差，没有将毫米级穿地写成完全接触通过。
骨骼为正、蒙皮为负是不同判定，结果分开报告。

稳定主方向支撑内段的世界水平滑动平均约 0.02～0.06 cm/s，最大约 0.10 cm/s。
统计排除前 0.6 秒，结合声明的支持相位及鞋部近地高度。
按更宽的近地窗口统计，最大约 23 cm/s，该窗口包含离地／落地。
因此不能把稳定支撑内段结果解释为全程没有脚滑。
夹具没有正式加减速、地形、碰撞或 CharacterMovement，切向过渡仍需后续匹配。

### 姿态与复制

- FP／TP 世界位置误差：0 cm，旋转约 0.0000034°。
- 上身完整图与 LowerOnly：136,320 条下肢记录。
- 最大下肢位置差约 0.00000173 cm，旋转差在记录精度内为 0。
- AO.Y 与 Sin(Pitch) 最大差约 0.0000000112。
- 平视上胸相对骨盆的前倾投影：Stand 约 1.19°，Crouch 约 23.95°。
- ADS 俯仰站蹲对照：头到右手关系最大差约 0.525 cm。
- 左手相对右手的位置差最大约 1.43 cm，仍有中间俯仰插值残差。
- +90° 对照中的左右手关系差约 0.035 cm。

手部数字是站蹲姿态关系差，不是实际握把 Socket 对齐误差。
没有接入最终 AimIK／LeftHand IK，精确枪口瞄准与实际握持没有通过验收。
Muzzle Socket 的 X 轴与输入视线仍有站立基线本身的方向差，未独立旋转枪械掩盖它。

资源原始／压缩姿态最大位置误差约 0.000614 cm，旋转约 0.00236°。
这些数值证明导入与声明参考有效，不替代运行或视觉判断。

## 5. 视觉观察与未完成项

可视化 UE 5.8 已实际打开实验地图，并进入可操作 PIE。
侧面能看到半蹲、腰背前倾及双脚支撑；八向均保存了画面采样。
静态图片不足以证明整个步态周期的主观自然度，仍需用户操作验收。

- 新 Idle 是稳定静态基准，尚未补呼吸、细节重心和原始腰背动态。
- 新步态的节奏、步幅和整体自然度仍需用户确认。
- 第一人称在 Stand、Crouch 均有明显肩部近裁切，视觉未通过。
- LowReady 及交付时约 -23.84° 的 ADS 均可见左手离枪，握持视觉未通过。
- 中间俯仰存在约 1.43 cm 的双手关系残差。
- 毫米级鞋面穿地与近地过渡的滑动仍未消除。
- Airborne、完整 Sliding、坡面、台阶、正式移动、多人、Cook 均未验收。

没有以相机偏移、WeaponActor 独立旋转、Root 抬高或扩大运行时 IK 范围补偿上述问题。

[侧面半蹲](Images/CrouchSide.png)、[站立对照](Images/StandSide.png)。
[第一人称问题](Images/CrouchFP.png)、[站立第一人称](Images/StandFP.png)。
[LowReady 问题](Images/LowReadySide.png)与[八向采样记录](VisualWalkSamples.json)。

## 6. 下一阶段生产兼容风险

生产拓扑和状态职责均保留，本轮没有直接替换 ABP_TP_Player。

1. 先接 ArmedAim 的 Stance 基准，逐项确认 AO 的参考和执行位置。
   本实验 AO 在身体组合后执行，生产 AO 在 WeaponAction 前，不能直接照搬。
2. RifleReady／LowReady 必须保留局部混合、Sprint Additive、头颈修正和 IK 释放。
   当前静态 LowReady 对照不代表这些状态和过渡已经成立。
3. Fire、Reload、Equip 逐层接入，每层检查是否重新覆盖腰背或重复施加俯仰。
   Reload 已有分布式脊柱修正，不应在 Arms Slot 后再无条件增加第二次 AO。
4. 绝对动作需要基准适配或经过制作的增量，禁止只切换 Additive 标志。
5. AimIK 应使用真实 HandToMuzzle 和目标数据，随后执行 LeftHand IK。
   不扩大修正角度来掩盖基础持姿错误；需复测实际枪口与握把 Socket。
6. FP 保持最终 TP CopyPose，再处理 FP Arms Slot。
   先解决可见网格和肩部裁切，避免用独立眼点或双肩迁移掩盖问题。
7. 正式移动需重新匹配步幅、加减速和真实下蹲状态。
   网络与 Gameplay 接入另按仓库契约验证，不能沿用本轮单机数据宣布通过。

## 7. 复测与依据

~~~powershell
& './Scripts/Animation/StanceAware/Run.ps1' -Mode Preview
& './Scripts/Animation/StanceAware/Run.ps1' -Mode Verify
& './Scripts/Animation/StanceAware/Run.ps1' -Mode Verify -Baseline
& './Scripts/Animation/StanceAware/Run.ps1' -Mode Capture -Stationary -PitchSweep -Output 'New/Full32.csv'
& './Scripts/Animation/StanceAware/Run.ps1' -Mode Capture -Transitions -Output 'New/Transitions.csv'
~~~

Output 必须是 Saved 内的新文件。
Baseline Verify 验证保留的资源契约，不将旧夹具源码不变作为本轮门禁。
Preview 用 1／2 站蹲、WASD 移动、R 持姿、V 视角、L 来源对照、O 方向轮换、T 暂停。

Epic 文档说明 Blend Mask 存在 Skeleton 中，本轮选择 Branch Filter 保持共享骨架不变。
[Blend Mask 官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/blend-masks-and-blend-profiles-in-unreal-engine?application_version=5.8)

AO 的 Mesh Space Additive 与参考姿势原则用于本轮设计。
[AimOffset 官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/aim-offset-in-unreal-engine?application_version=5.8)

统一支持时序参考同步动画应保持相同落脚关系的要求。
本轮序列统一为 1 秒，没有将未设置的 Sync Marker 宣称为已使用。
[同步官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sync-groups-in-unreal-engine?application_version=5.8)

网页默认版本可能更新，实际调用已按本机 UE 5.8.4 核对。
检查了 CreateMaskWeights、LayeredBoneBlend、RotationOffsetBlendSpace 与 AnimPose 的实际实现。

## 证据索引

- VerifyAssets.json：25 段、原生图、参考、循环与 619 项保护。
- LegacyMirrorResources.json：原两段镜像资源和 306 项旧数据回归。
- CurrentSkin.json、CurrentTransitionSkin.json：蒙皮、互穿和滑动。
- Current32.eightway.json：骨骼接触、AO 输入、FP 复制。
- CurrentLowerComparison.json：完整图和 LowerOnly 实际对照。
- FinalPresentation.json：站蹲头手、双手、枪口关系。
- BeforeSkin.json、SourceSurvey.json：当前平滑设置基线与来源差异。
- Current32.csv.gz、FinalPresentation.csv.gz：最终原始采集。
- Before32.csv.gz、RuntimeReference90.csv.gz：基线与制作输入来源。
- FinalRaw32.csv.gz：LowerOnly 原始对照；view.csv.gz：相机和枪口原始采集。
- VisibleSession.json、Images/Handoff.png：实际交付地图、原生类及输入状态。
- PostBuildFormatting.json：编译后仅空白调整的校验说明。
- Manifest.json：本轮代码、资产和证据校验值。
