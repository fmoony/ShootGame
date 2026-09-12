# Rifle 第三人称换弹动画实施计划

## 1. 阶段定位

本阶段只为 Rifle 接入第三人称换弹表现，不修改已经完成的服务器权威换弹事务，不进入第一人称、Pistol、Equip 动画、客户端预测或弹匣道具表现。

权威链保持不变：

```text
IA_Reload
→ ASC Input.Reload
→ GA_Reload（ServerOnly）
→ State.Reloading
→ 等待 Weapon.ReloadDuration
→ Inventory.ReloadMagazine 原子提交
→ EndAbility
```

动画只消费 `UShooterAnimInstanceBase::bIsReloading`。动画完成、Sequence Time Remaining 与 AnimNotify 均不得提交弹药或结束 Ability。

## 2. 当前已确认基线

2026-08-30 使用项目只读 MCP 检查 `/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle`：

- Parent Class：`UShooterThirdPersonAnimInstance`；
- AnimGraph 当前共 22 个顶层节点；
- 当前主链为：

```text
LocomotioStateMachine
→ Layered Blend Per Bone.BasePose

MF_Rifle_Idle_ADS
→ AO_Rifle（Y = AimPitchN）
→ Slot "Arms"
→ Layered Blend Per Bone.BlendPose0

Layered Blend Per Bone
→ Local To Component
→ Shooter Aim IK
→ Shooter Left Hand IK
→ Component To Local
→ Output Pose
```

- `Shooter Aim IK.Alpha` 当前由 `bAimIKEnabled` 经 `Select Float` 转成 0/1；
- `Shooter Left Hand IK.Alpha` 当前由 `bLeftHandIKEnabled` 经 `Select Float` 转成 0/1；
- Reload 候选资源为 `/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload`，当前类型为 `AnimSequence`；Skeleton、序列长度、Root Motion 与预览姿势必须在编辑器中再次确认；
- UE 5.6 本地源码中 `ReplicateActivationOwnedTags` 默认开启，项目 Config 没有覆盖关闭；`GA_Reload` 的 `State.Reloading` 因此应复制为表现 Tag，但仍需 PIE 验证拥有者与观察者都能读取。

## 3. 目标结构

移动状态机不加入 Reload。新增独立的上半身 `WeaponAction` 状态机：

```text
Locomotion State Machine
        │
        └── 下半身与全身基础姿势

WeaponAction State Machine
        ├── ArmedAim
        │     MF_Rifle_Idle_ADS → AO_Rifle
        └── Reload
              MM_Rifle_Reload（不循环）
              → spine_01/02/03 分布式躯干俯仰
        │
        └── Slot "Arms"

Locomotion + WeaponAction
→ Layered Blend Per Bone
→ Shooter Aim IK（Reload 时平滑释放，结束后平滑恢复）
→ Shooter Left Hand IK（Reload 时平滑释放，结束后平滑恢复）
→ Output Pose
```

状态转换只读取：

```text
ArmedAim → Reload：bIsReloading == true
Reload → ArmedAim：bIsReloading == false
```

禁止使用 `Time Remaining` 决定退出 Reload。Sequence 提前结束时保持末帧；服务器完成或取消换弹、`State.Reloading` 消失后才退出。

## 4. AnimBP 实施步骤

### A1：建立 WeaponAction 状态机

1. 在 `ABP_TP_Rifle` AnimGraph 新建 State Machine，命名为 `WeaponAction`。
2. 新建 `ArmedAim` 与 `Reload` 两个 State，默认入口连接 `ArmedAim`。
3. 把当前 `MF_Rifle_Idle_ADS → AO_Rifle` 移入 `ArmedAim`；保持 `AO_Rifle.Y = AimPitchN`。
4. `Reload` 中放入 `MM_Rifle_Reload` Sequence Player，并关闭 Loop Animation。
5. 建立两条基于 `bIsReloading` / `!bIsReloading` 的转换规则。
6. 两条 Transition Blend Duration 初始设为 `0.10s`。
7. `WeaponAction` 输出继续进入当前 `Slot "Arms"`，再进入现有 `Layered Blend Per Bone.BlendPose0`。
8. 保持 Locomotion 仍连接 `Layered Blend Per Bone.BasePose`，不修改下半身状态机。

### A2：Reload 时保留俯仰并平滑释放左手 IK

第一轮视觉验收已证明同时硬关闭 AimIK / LeftHandIK 会产生三个问题：Reload 回到水平原始姿势、枪手贴合变差、结束时 Alpha 从 0 跳回 1。第二轮把现有 `AO_Rifle` 直接叠到 Reload 后，又证明该持枪 AimOffset 会与 Reload 自带的手臂动作冲突，俯仰越大越明显。因此改为：

```text
Reload State：MM_Rifle_Reload
→ Local To Component
→ spine_01 / spine_02 / spine_03 各追加 AimPitchDegrees / 3
→ Component To Local

AimIKAlpha = AimIKPresentationAlpha
LeftHandIKAlpha = LeftHandIKPresentationAlpha
```

`bAimIKEnabled` / `bLeftHandIKEnabled` 继续只表达 Binding 与动态输入是否有效，不混入 Reload 状态。Reload 不再叠加会修改持枪手臂的 AimOffset，而是通过 Component Space 骨骼控制让整套换弹上身作为一个整体随俯仰转动。`UShooterThirdPersonAnimInstance` 每帧只计算最终表现权重：

```text
bAimIKEnabled && !bIsReloading
→ 进入 Reload 约 0.10s 线性趋近 0
→ 退出 Reload 约 0.15s 线性趋近 1
→ AimIKPresentationAlpha

bLeftHandIKEnabled && !bIsReloading
→ 进入 Reload 约 0.10s 线性趋近 0
→ 退出 Reload 约 0.15s 线性趋近 1
→ LeftHandIKPresentationAlpha
```

两个 Alpha 与 `AimPitchDegrees` 都只服务动画表现，不复制、不改变 GAS Tag、不成为新的换弹状态真相。三段脊柱先平均分配角度；若方向相反，只校正一次符号，不以反复调参掩盖空间错误。

### A3：时间协调

服务器时钟仍是 Rifle WeaponActor 的 `ReloadDuration`。读取 `MM_Rifle_Reload` Sequence Length 后，必要时使用：

```text
PlayRate = SequenceLength / ReloadDuration
```

本轮只调整表现播放速率，不让动画长度反向修改 `ReloadDuration`。

## 5. Agent 与人工边界

### Agent 自动完成

- 用 CodeGraph 核对 GA_Reload、ASC Tag 与 AnimInstance 数据链；
- 用只读 MCP 保存修改前、修改后的 AnimGraph 节点和连线证据；
- 维护本计划与提交前中文开发记录；
- 编译、运行 `ShootGame` Automation 与完整网络回归；
- 检查 Git 范围，只提交本阶段 Rifle AnimBP、计划、测试和开发记录。

### 人工必须完成

- 在 Unreal Editor 中修改 `ABP_TP_Rifle` 的 AnimGraph / State Machine；
- 检查 `MM_Rifle_Reload` 的 Skeleton、Sequence Length、Root Motion 和预览姿势；
- 编译并保存 `.uasset`；
- 在有渲染窗口的两客户端 PIE 中完成视觉验收。

只读 MCP 不允许创建节点或保存 `.uasset`，因此 Agent 到达 A1 时必须暂停并提供精确连线说明。

## 6. 验收矩阵

### 功能

- 弹匣未满且有 Reserve 时按 R，Rifle 第三人称进入 Reload；
- 弹匣满、无 Reserve、死亡状态时不进入 Reload；
- 连按 R 不重启或重复提交；
- Reload 完成后弹药只转移一次；
- Reload 中切枪、死亡、断线时动画退出且弹药不提交。

### 动画

- 原地、前进、后退、左右横移时下半身保持移动；
- Reload 动画只接管上半身；
- Reload 期间角色仍保持当前俯仰方向，枪、双臂和躯干作为整体运动；
- Reload 期间 LeftHand IK 平滑释放，不把左手拉回握把；
- Reload 结束后两套 IK 恢复，枪口与左手不长期失效；
- 抬头、低头、跳跃时不出现明显骨骼翻转或单帧爆跳。

### 网络

- Dedicated Server 下拥有者和观察者都能看到 `bIsReloading` 生效；
- Listen Server 与远程客户端结果一致；
- 100ms 延迟 / 2% 丢包下动画允许延迟开始，但 Ammo、Ability 与 Tag 最终一致；
- 不新增 Reload Multicast、复制 Bool 或第二份权威状态。

## 7. 提交边界

本阶段建议只创建一个提交：

```text
动画：接入Rifle第三人称换弹表现
```

提交前必须写开发记录，并至少完成：

```text
BuildEditor.ps1
RunAutomation.ps1 -TestFilter ShootGame
RunAll.ps1（视觉验收通过后）
```

第一人称和 Pistol 必须等本阶段视觉验收后另立提交，不能夹带进入本轮。

## 8. 阶段结果

2026-08-31，Rifle 第三人称换弹表现已形成阶段性可用基线并通过人工视觉验收：

- `WeaponAction` 使用 `State.Reloading` 进入 Reload，服务器权威事务保持不变；
- `ReloadIKAlpha` 平滑释放和恢复 Aim / LeftHand IK；
- `ReloadRecovery` 只负责表现提前收尾，且由 `bIsReloading` 防守迟到 Notify；
- Reload 内部通过 `RifleReloadWeightedUpperBody` 分级 Blend Mask 过滤资源头颈动作；
- Reload 源姿势在合成前应用 `spine_01/02/03` 分布式实时俯仰，使双臂换弹动作能随玩家瞄准方向运动；
- Rifle 的 `ReloadDuration` 当前为 `2.5s`，覆盖 `2.2s` 动画序列及表现收尾，并增加“权威时长不得短于动画”的自动化配置断言。

本阶段不包含 Pistol、第一人称换弹、极端低头手臂异常和网络预测。
