# 下蹲实验现状总结

日期：2026-11-16；UE 5.8.4；工作树 ShootGame_UE58。
实验根目录：/Game/Experiments/Stance_20261022。
地图：/Game/Experiments/Stance_20261022/L_StancePreview。

当前完成的是原生 AnimBP 下的八向下蹲与持枪分层实验。
生产角色、完整武器动作链路及接触质量尚未完成接入和验收。
上一轮提出的“保留腰背俯身、逐渐叠加持枪上身”目前仅为计划，尚未实施。

## 1. 当前 AnimGraph 实际拓扑

### 1.1 当前实验第三人称

资产：NativeGraph/ABP_StanceNative_TP；父类为 Engine.AnimInstance。

~~~mermaid
flowchart LR
    S["站立 / 蹲 idle / 八向 BS / 滑铲"] --> M["Blend Poses by Int"]
    M --> C["缓存 NativeLower"]
    C -->|BasePose| L["Layered Blend Per Bone"]
    H["Aim / Compact LowReady"] --> B["Blend Poses by Bool"]
    B --> AO["AO_Rifle"]
    AO -->|BlendPoses 0| L
    C -->|仅来源姿态| D["LowerOnly 调试选择"]
    L --> D
    D --> O["Output Pose"]
~~~

- 基础动作不是生产 Locomotion 状态机，由 Mode 选择四类动作。
- 移动由 BS_SourceLayer_Lower 原生播放器求值，Forward 接 X，Right 接 Y。
- 上身从 MF_Rifle_Idle_ADS、MM_Rifle_LowReady_Compact 中选择，再进入 AO_Rifle。
- Pitch 经 Sin(Degrees) 接 AO.Y；Yaw 接 AO.X。
- 分层的 BasePose 是 NativeLower，上身输入是经过 AO 的持枪姿态。
- 分层过滤从 spine_01 开始，Depth=1，上身权重为 1。
- Mesh Space Rotation Blend 开启，Mesh Space Scale Blend 关闭。
- 最后通过 LowerOnly 开关选择来源完整姿态或分层结果。

**因此，实验的顺序是“持枪姿态 → AimOffset → 上下身分层 → 输出”。**
Locomotion 来源与持枪来源是两条并行分支。
实验没有 WeaponAction 状态机、Arms Slot、AimIK、左手 IK、Control Rig 或后处理。

“仅下半身”调试开关实际输出来源完整姿态，包含来源上身。
它并不是把腰部以上的骨骼隐藏，因此适合作为不分层姿态的对照。

### 1.2 当前生产第三人称，供对照

资产：/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player。

~~~mermaid
flowchart LR
    H["ThirdPersonHoldSequence"] --> AO["ThirdPersonAimOffset"]
    AO --> C["缓存 ArmedAimPose"]
    C --> W["WeaponAction：ArmedAim / Reload"]
    W --> S["Slot Arms"]
    LM["LocomotioStateMachine"] -->|BasePose| L["Layered Blend Per Bone"]
    S -->|上身输入| L
    L --> LC["Local To Component"]
    LC --> AI["ShooterAimIK"]
    AI --> LI["ShooterLeftHandIK"]
    LI --> CL["Component To Local"]
    CL --> O["Output Pose"]
~~~

生产有效持枪链路是：
**Hold → AO → ArmedAimPose → WeaponAction → Arms Slot → 分层 → AimIK → 左手 IK。**

WeaponAction 会选择和混合上身动作，不是简单把它插在 Locomotion 后面。
其 ArmedAim 内还有 RifleReady，包含 ArmedAim、AimTransition、LowReady。
LowReady／过渡还使用局部分层、Sprint Additive 和 RifleHeadPose。
Reload 也有独立的脊柱修正与分层，不能等同于实验的 Aim／LowReady 布尔切换。
生产这些自定义处理仍保留；取消 C++ 姿态处理的范围仅为 Stance 实验。

### 1.3 第一人称

- 实验：附着父 Body 的 Copy Pose From Mesh → Output。
- 生产 ABP_FP_Player：Copy Pose From Mesh → Slot Arms → Output。
- 实验 FP 复制 TP 最终姿态，没有额外独立的瞄准、手臂 IK 或 Montage 层。

## 2. 实验措施、结果与现有不满点

### 2.1 已采取的措施

1. 取消独立眼点前移、双肩前移等实验补偿，恢复按生产配置附着 head 的相机。
2. 从仅恢复手臂，改为完整持枪上身分层，改善下蹲时头肩与枪械的相对位置。
3. 移除实验手写 C++ 取样、FK、混合、脊柱瞄准、握持、IK、接地及骨骼回写。
4. TP／FP 改用标准 SkeletalMeshComponent 和原生 AnimBP，FP 使用原生 Copy Pose。
5. 修复 FBX 父对象运动未正确落到真实 root 的问题，验证绑定生产 Skeleton。
6. 从完整烘焙版本派生原地输入，补齐八个标准方向，退出重复右后补点方案。
7. 将问题较大的左前、右后替换为当前右前、左后的直接镜像。
8. 修复角度直接接 AO 导致约 1° 就到端点的问题，改用 Pitch → Sin(Degrees)。
9. 为手动体验开启 Forward／Right 各 0.2 秒原生方向平滑。

当前 C++ 只保留输入、固定速度 Actor 移动、相机、只读骨骼采集及 Editor 建图。
没有保留用于接地或姿态修正的运行时回退开关。

### 2.2 当前仍不满意或未完成的部分

- **上身偏直**：当前完整覆盖持枪上身，尚未实现用户要求的轻微前倾。
  下蹲来源中腰背姿态具体被覆盖多少，仍需按骨骼运行数据对照。
- **鞋面仍穿地**：当前左前约 -4.0 cm，右后约 -5.1 cm。
  主方向 foot／ball 骨骼点为正，不代表鞋部蒙皮一定在地面以上。
- **相邻方向混合穿地**：零输入平滑的既有采集中，
  右后与后退之间 157.5° 的前掌骨骼最低约 -13.7 cm。
  左前邻域也存在约 -3～-6.9 cm 的骨骼点穿地。
- **idle 不统一**：中心 idle 仍是原始单膝跪姿，与移动半蹲姿态不一致。
- **移动自然度尚未验收**：固定 150 cm/s 未做步幅匹配，脚滑及启停衔接仍需检查。
- **尚未完整复刻生产**：缺 WeaponAction、生产收枪分支、AimIK、左手 IK 和 FP Arms Slot。
- **第一人称与精确瞄准未全面通过**：已有构图改善不等于极端俯仰、遮挡和枪线全部正确。
- 坡面、台阶、网络、正式 CharacterMovement、Cook 及完整滑铲回归均未验收。

0.2 秒平滑仅改变 BlendSpace 输入过渡，没有修复落脚高度或支撑相位。
上述接触数值来自此前零输入平滑版本，不能当作当前平滑设置的重新验收结果。
“微俯身”方案尚未执行，也没有额外加入 C++ 前倾角度补偿。

## 3. 现有动画资源与来源

### 3.1 当前实验实际使用的方案

来源处理链为：
**原始 FBX → Blender 父层运动烘焙 → 原地版本 → 八向派生／直接镜像 → 标准 FBX 导入。**

当前下蹲序列直接绑定：
/Game/Characters/Mannequins/Meshes/SK_Mannequin。

BlendSpace 为 SourceLayer/BS_SourceLayer_Lower，X=Forward，Y=Right。
八个移动样本加中心 idle，共九点；播放倍率均为 1。
右后只保留一个标准 135° 样本，不再使用三个重复右后点。

下表路径均相对实验根目录，名称已省略目录对应的 AS 前缀。

| 方向 | 当前目录／动作 | 制作来源 |
| --- | --- | --- |
| idle | SourceLayer / CrouchIdle | 原始单膝跪 idle 的原地播放输入 |
| 前 | EightWay / CrouchForward | 上轮离线制作的正前派生 |
| 右前 | EightWay / CrouchForwardRight | 上轮处理过的斜向派生 |
| 右 | SourceLayer / CrouchRight | 原始右移的原地版本 |
| 右后 | Mirrored / CrouchBackRight | 当前 EightWay 左后直接镜像 |
| 后 | SourceLayer / CrouchBack | 原始后退的原地版本 |
| 左后 | EightWay / CrouchBackLeft | 上轮处理过的斜向派生 |
| 左 | SourceLayer / CrouchLeft | 原始左移的原地版本 |
| 左前 | Mirrored / CrouchForwardLeft | 当前 EightWay 右前直接镜像 |

EightWay 本身经过来源融合、离线 IK、约束和周期平滑，不能称为未修改的 Source。
本次 Mirrored 只反射与左右骨骼配对，不重新求解，也不增加移相。
镜像保留来源已有处理结果；它不会自动消除来源的接地缺陷。

上身使用生产 MF_Rifle_Idle_ADS、MM_Rifle_LowReady_Compact 和 AO_Rifle。
滑铲只播放保留的 AS_Slide_Aim，本轮没有重新制作。

### 3.2 排除派生制作后，原始下蹲来源有哪些

SourceAssets/StanceCrouch 根目录保存五份原始 FBX，Manifest 校验字节未改：

- CrouchIdle：单膝跪姿 idle。
- CrouchLeft：半蹲左移。
- CrouchRight：半蹲右移。
- CrouchBack：半蹲后退。
- CrouchForwardLeft：半蹲左前。

**原始来源是四个移动方向加一个跪姿 idle，不是完整原生八向下蹲包。**
没有独立正前、右前、左后、右后，也没有与当前移动一致的半蹲 idle。

Source 资产用于原始导入对照；SourceBaked 用于完整父层烘焙对照。
LayerPlayback／SourceLayer 提供原地版本；EightWay 和 Mirrored 是后续派生。
旧 BlenderGrounded 的 92 段及旧 BackRight 资源保留，当前图没有引用那 92 段。

### 3.3 除去本实验，项目原有动画有哪些

本次盘点范围为 /Game/Characters/Mannequins/Anims。
这里的“原有”指实验之外已存在的项目资源，包含此前生产派生，不全是模板原件。

- Rifle：Walk 八向、Jog 八向、跳跃六段、受击八段。
- Rifle 持枪与瞄准：MF_Rifle_Idle_ADS、AO_Rifle 及上／中／下样本。
- Rifle 武器动作：Fire、DryFire、Reload、Equip、Draw、Holster 及第一人称 Montage。
- Rifle 生产派生：Compact／Relaxed LowReady、AimToLowReady 各版本、
  Sprint Compact Additive，以及已有外部 RifleIdle 等来源。
- Pistol：Walk 八向、Jog 八向、跳跃、持枪与 AO、开火、换弹及装备动画。
- Unarmed：Walk 八向、Jog 八向、idle、跳跃／下落／落地及攻击动画。
- 另有死亡动画。

在上述正式动画目录内，本次未发现独立 Crouch 或 Slide 动画序列。
原生 Rifle 八向 Walk 可作节奏和自然度参考，但本身不是下蹲动作。
完整资产名与类型保存在本次资产审计中，不根据文件名推断实际生产引用。

## 4. 当前实验已经解决的问题

- **生产 Skeleton 导入问题**：明确了父层烘焙和骨架尺度要求，
  新版本可通过标准 FBX 导入直接绑定生产 Skeleton。
- **实验 C++ 姿态处理混入问题**：已移除，当前运行求值交给原生 AnimBP。
- **FP 与 TP 最终姿态不一致的排查边界**：已用实际组件采集验证复制一致。
- **仅恢复手臂导致头肩关系异常的问题**：已改为完整上身分层；
  历史同版本对照中，蹲姿持枪相对位置恢复到站立参考。
  该历史结果不自动代表当前所有视角自然度通过。
- **俯仰过早饱和的问题**：已将角度输入转换为 AO 所需的归一化数值。
- **移动方向资源缺项和右后重复补点问题**：当前已为八向各一段独立引用。
- **左前／右后主方向双脚互穿问题**：镜像后本次主方向稳定采样均为 0/68 帧相交。
  这不等于鞋面接地或相邻方向混合通过。
- **脚部判定只看骨骼点的问题**：已增加实际运行骨骼驱动的鞋部蒙皮重建及表面相交检查，
  区分骨骼穿地、鞋面穿地和双脚互穿。

最近镜像运行覆盖 32 方向、两持枪与两 idle，共 66 场景、5,680 帧。
其结论是“镜像保真通过、完整接触质量未通过”。
本实验目前提供可复查的资源与原生链路基线，尚未成为正式生产下蹲功能。

## 证据与本次核对范围

本次从磁盘重新加载四个 AnimBP 和当前 BlendSpace，读取真实引脚连接与九个样本。
未保存或修改动画、蓝图及生产代码，也未执行上一轮微俯身计划。
MCP 服务未运行，因此使用只读 Python Commandlet；未启动可视化编辑器。
本次不重新采集游戏运行数据，脚部结论引用各版本对应的既有实测。

308 个保护文件与镜像报告 SHA256 一致，两段镜像动画也一致。
BlendSpace 与镜像报告不同，差异对应后来开启的 0.2 秒方向平滑。

- [本次磁盘资产与连线审计](AssetAudit.json)。
- [本次保护文件核对](HashCheck.json)。
- [当前运行说明](../滑铲与下蹲动画实验.md)。
- [原生链路移除与运行对照](../StanceNativeGraph_20261115/README.md)。
- [直接镜像及剩余接触问题](../StanceMirror_20261116/README.md)。
- [完整父层烘焙验证](../StanceSourceBaked_20261115/README.md)。
- [原地播放来源验证](../StanceSourceLayer_20261115/README.md)。
- [原始来源与派生目录](../../../SourceAssets/StanceCrouch/README.md)。
