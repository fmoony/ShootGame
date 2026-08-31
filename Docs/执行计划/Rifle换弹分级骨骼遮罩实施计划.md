# Rifle 换弹分级骨骼遮罩实施计划

> 状态：阶段性完成，人工视觉验收通过（2026-08-31）
> 范围：仅调整 `ABP_TP_Rifle` 的第三人称 Reload 姿势分层
> 明确搁置：极端向下瞄准时的手臂异常旋转、Aim IK 数学升级、Pistol Reload

## 1. 背景与问题

当前 Rifle 第三人称换弹已经具备：

- `State.Reloading` 驱动 `WeaponAction` 的 `ArmedAim / Reload` 状态；
- `ReloadRecovery` 表现收尾入口；
- `ReloadIKAlpha`、`AimIKPresentationAlpha`、`LeftHandIKPresentationAlpha`；
- Reload 期间的程序化脊柱俯仰与结束前 IK 恢复。

人工视觉验收确认基本时序已经可用，但 `MM_Rifle_Reload` 本身包含头部、躯干和视线方向变化。第一人称玩家的摄像机没有发生相同动作，因此观察端会看到第三人称角色在换弹时改变头部观察方向，形成“角色表演与玩家真实视线不一致”。

这不是网络瞄准数据错误，也不应通过修改摄像机、复制额外头部状态或让 GAS 等待动画来解决。本轮只重新定义 Reload 资源对骨骼的控制范围。

## 2. 当前项目基线

当前主链为：

```text
Locomotion State Machine
        │
        └── Layered Blend Per Bone.Base Pose

WeaponAction
        ├── ArmedAim：MF_Rifle_Idle_ADS → AO_Rifle（AimPitchN）
        └── Reload：MM_Rifle_Reload → spine_01/02/03 俯仰修正
        │
        └── Slot "Arms"
                → Layered Blend Per Bone.Blend Pose 0

合成结果
→ Shooter Aim IK
→ Shooter Left Hand IK
→ Output Pose
```

当前问题在于 `Reload` 状态向外提供的是一套包含头、颈、胸椎和双臂的完整上身姿势；外层分层无法区分“希望保留的双臂换弹动作”和“不希望覆盖的头部表演”。

## 3. 目标结构

使用一份可复用的 `ArmedAimPose` 作为实时瞄准基础姿势，在 Reload 状态内部通过分级骨骼遮罩叠加换弹资源：

```text
MF_Rifle_Idle_ADS
→ AO_Rifle（AimPitchN）
→ Save Cached Pose：ArmedAimPose

WeaponAction.ArmedAim
→ Use Cached Pose：ArmedAimPose

WeaponAction.Reload
    Base Pose  = Use Cached Pose：ArmedAimPose
    Blend Pose = MM_Rifle_Reload（不循环）
                 → spine_01/02/03 分布式 AimPitchDegrees 修正
    Blend Mode = Blend Mask
    Blend Mask = RifleReloadWeightedUpperBody
    → Reload State Output

WeaponAction Output
→ 现有外层 Layered Blend Per Bone
→ Shooter Aim IK
→ Shooter Left Hand IK
```

目标职责为：

- 腿和骨盆：继续由 Locomotion 控制；
- 头和颈：继续由实时 `ArmedAimPose / AimPitchN` 控制；
- 胸椎：只吸收少量 Reload 资源动作，保留自然的躯干配合；
- 锁骨、双臂、手和手指：完整使用 Reload 资源；
- 武器：继续通过 `hand_r` 附着跟随右手动作；
- Aim / LeftHand IK：继续由现有 Reload 表现时序释放和恢复。

## 4. 初始遮罩权重

在 `ABP_TP_Rifle` 实际 Target Skeleton 上新增 Blend Mask，建议命名：

```text
RifleReloadWeightedUpperBody
```

该 Mask 存储在 Skeleton 资产中，不是独立 Content Browser 资产。执行前必须通过只读 MCP 确认 `ABP_TP_Rifle` 的真实 Target Skeleton，禁止凭文件名猜测。

第一轮权重只用于方向验证：

| 骨骼区域 | 初始权重 | 目的 |
| --- | ---: | --- |
| root / pelvis / 双腿 | `0.0` | 不让 Reload 改变移动和下半身 |
| spine_01 | `0.15` | 保留很少量躯干惯性 |
| spine_02 | `0.25` | 允许有限胸椎配合 |
| spine_03 | `0.35` | 让肩部与手臂过渡更自然 |
| neck / head | `0.0` | 不消费 Reload 资源的视线表演 |
| clavicle_l / clavicle_r | `1.0` | 完整承接双肩换弹动作 |
| upperarm / lowerarm / hand / fingers | `1.0` | 完整播放双臂与手指动作 |
| 对应 arm twist / corrective 骨骼 | `1.0` | 避免主骨骼与修正骨骼权重不一致 |

实际 Skeleton 可能包含额外 Manny twist / corrective 骨骼。人工设置时应按骨骼层级核对继承结果，不能只设置上表中的文字名称后默认完成。

## 5. AnimBP 实施步骤

### P0：保存可回退基线

1. 保存当前 `ABP_TP_Rifle`、`MM_Rifle_Reload` 和其 Target Skeleton。
2. Agent 使用只读 MCP 记录：
   - AnimGraph 主链；
   - `WeaponAction` 的状态与 Transition；
   - `ReloadRecovery` 事件入口；
   - Aim / LeftHand IK 的 Alpha 连线。
3. 记录修改前 Git 状态，不覆盖其他未提交资产。

### P1：建立并复用 ArmedAimPose

1. 在顶层 AnimGraph 建立：

   ```text
   MF_Rifle_Idle_ADS
   → AO_Rifle（Y = AimPitchN）
   → Save Cached Pose "ArmedAimPose"
   ```

2. `WeaponAction.ArmedAim` 改为 `Use Cached Pose "ArmedAimPose"`。
3. 编译确认没有 Cached Pose 循环依赖、重复求值或断链。

### P2：建立 Skeleton Blend Mask

1. 打开真实 Target Skeleton。
2. 新增 `RifleReloadWeightedUpperBody`。
3. 按第 4 节设置第一轮权重。
4. 特别检查：
   - neck/head 最终为 `0`；
   - 双侧 clavicle 及完整手臂链最终为 `1`；
   - twist/corrective 骨骼没有保留错误的继承权重。
5. 保存 Skeleton。

### P3：重建 Reload State 内部姿势

1. `Base Pose` 使用 `ArmedAimPose`。
2. `Blend Pose 0` 使用非循环 `MM_Rifle_Reload`。
3. 添加 `Layered Blend Per Bone`：
   - `Blend Mode = Blend Mask`；
   - 选择 `RifleReloadWeightedUpperBody`；
   - `Blend Pose 0 Alpha = 1.0`；
   - `Mesh Space Rotation Blend = true`；
   - `Mesh Space Scale Blend = false`；
   - `Curve Blend Option = Override`。
4. 在 `MM_Rifle_Reload` 分支进入 `Layered Blend Per Bone.Blend Pose 0` 前保留 `spine_01/02/03` 的分布式 `AimPitchDegrees` 修正。实际视觉验收证明：双臂区域的 Mask 权重为 `1.0` 时，Reload 分支会完整覆盖 `ArmedAimPose` 的手臂俯仰；仅依赖 Base Pose 会让换弹双臂停留在水平姿势。俯仰必须先施加到 Reload 源姿势，再由 Mask 过滤头颈并合成。
5. 保持现有状态时序：

   ```text
   ArmedAim → Reload：bIsReloading && !bReloadPresentationRecovering
   Reload → ArmedAim：!bIsReloading || bReloadPresentationRecovering
   ```

6. 保持 `ReloadRecovery` Notify、现有 IK Alpha 连线和 GAS Reload 逻辑不变。

### P4：曲线与事件复核

1. 确认 `ReloadIKAlpha` 在新的内部 `Layered Blend Per Bone` 后仍可被 AnimInstance 读取。
2. UE 5.6 本地源码确认 `FAnimNode_LayeredBoneBlend` 默认 `CurveBlendOption = Override`；本计划仍要求在节点 Details 中显式核对，避免资产历史配置覆盖默认值。
3. 确认 `ReloadRecovery` 每次换弹只触发一次，Recover 后不会重新进入 Reload。
4. 如果 `ReloadIKAlpha` 在新结构下丢失，先停止并审计 Curve Blend 配置；不得用新的复制变量或 GAS Tag 绕过。

## 6. 验收顺序

### 6.1 编译与静态检查

- `ABP_TP_Rifle` Compile：0 Error；
- Skeleton、Reload Sequence、AnimBP 全部成功保存；
- 只读 MCP 确认 Cached Pose、Reload 内部分层和现有 IK 节点仍在预期链路；
- Agent 执行 `BuildEditor.ps1` 与聚焦 `ShootGame` Automation。

### 6.2 单客户端视觉验收

依次测试水平、向上约 45°、向下约 45°：

- Reload 期间头部没有跟随资源自行改变观察方向；
- 头部仍响应当前 `AimPitchN`；
- 双臂能完成弹匣操作，左手不会明显够不到弹匣；
- 锁骨处没有明显断层或单帧跳变；
- 躯干保留少量动作，换弹双臂能够随实时俯仰整体转动；
- Recover 阶段头、武器、双臂和 IK 收尾连续。

### 6.3 两客户端观察验收

- 拥有者与观察者看到的 Reload 状态一致；
- 观察者看到的头部方向持续对应玩家瞄准，而不是 Reload 资源视线；
- 移动、跳跃和转身中下半身仍由 Locomotion 正常驱动；
- `State.Reloading` 消失前仍不能开火，动画分层不改变服务器权威时序。

### 6.4 完整回归

人工视觉门通过后再运行项目标准完整自动化。视觉门未通过时不反复执行全套网络回归。

## 7. 调整与回退规则

只允许按以下顺序调整：

1. 肩部断层：先检查 twist/corrective 权重和 `Mesh Space Rotation Blend`；
2. 双手够不到弹匣：逐步提高 spine_03，再考虑 spine_02；
3. 躯干动作过大：逐步降低 spine_01/02/03；
4. 头部仍受 Reload 影响：检查 neck/head 最终 Mask 权重；分布式脊柱俯仰属于 Reload 源姿势的实时方向载体，不应直接删除；
5. 曲线丢失：检查 `Curve Blend Option`，不新增网络状态。

以下情况直接回退本方案，而不是继续堆参数：

- 双臂动作必须依赖大幅躯干扭转才能完成；
- Mask 导致肩部在多个俯仰角持续撕裂；
- Reload 曲线或 Notify 在分层后无法稳定到达；
- 需要修改 GAS、Inventory、Equipment 或网络复制才能维持表现。

回退范围仅包括 `ABP_TP_Rifle` 和 Target Skeleton 中新增的 Blend Mask；不得回退同一资产内与本计划无关的用户修改。

## 8. Agent 与人工边界

### Agent 自动完成

- CodeGraph 核对 C++ 数据和状态来源；
- 只读 MCP 审计蓝图节点、资产路径和修改结果；
- 编译、Automation、网络回归与日志判定；
- 更新计划、开发记录和提交范围；
- 在人工视觉验收失败时根据证据提出单一调整项。

### 人工必须完成

- 在 Skeleton Editor 创建并编辑 Blend Mask；
- 在 `ABP_TP_Rifle` 中建立 Cached Pose 和 Reload 内部分层；
- 删除或旁路旧的 Reload 脊柱修改节点；
- 编译、保存 `.uasset`；
- 完成单客户端和两客户端视觉验收。

Agent 不得自行启用可写蓝图插件、恢复一次性资产修改工具或绕过人工资产门。

## 9. 非目标与搁置项

本计划不处理：

- 极端向下瞄准时 `hand_r` / 左臂的异常旋转；
- `Shooter Aim IK` 的最大校正角、权重曲线或多骨骼分配；
- Pistol 的 Reload 动画与分层；
- 第一人称 Reload；
- ReloadDuration、弹药提交和开火阻塞时序；
- 新的 GameplayTag、复制字段或网络预测。

极端低头手臂问题必须在本计划完成并形成稳定 Rifle Reload 基线后，作为独立计划重新测量和讨论。

## 10. 实现依据

### 官方外部依据

- Epic Games：[Using Layered Animations in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/using-layered-animations-in-unreal-engine?lang=en-US)：使用 Cached Pose 与 Layered Blend Per Bone 复用基础姿势并按骨骼范围覆盖动作。
- Epic Games：[Animation Curves in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-curves-in-unreal-engine)：动画曲线用于与 Sequence 时间同步的连续表现值。
- Epic Games：[Animation Notifies in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/animation-notifies-in-unreal-engine?lang=en-US)：Notify 用于与 Animation Sequence 同步的离散表现事件。

### 本地 UE 5.6 核对

- `FAnimNode_LayeredBoneBlend` 提供 `BlendMode`、`bMeshSpaceRotationBlend`、`bMeshSpaceScaleBlend` 和 `CurveBlendOption`；本地构造默认 `CurveBlendOption = ECurveBlendOption::Override`。
- `ECurveBlendOption::Override` 的本地注释语义为最后一个含有效曲线值的 Pose 覆盖之前的值。

### 与当前项目的差异

当前项目原先由 Reload 完整上身姿势覆盖头颈，并用三个脊柱节点补偿俯仰；最终方案以 `ArmedAimPose` 保持未覆盖区域的实时瞄准，对 Reload 源姿势施加分布式俯仰后，再按 Blend Mask 只吸收受控比例的 Reload 躯干和完整双臂动作。服务器换弹、网络复制和两套 IK 节点职责保持不变。

## 11. 阶段实施结果

2026-08-31 已完成并通过人工视觉验收：

- 在 Manny Skeleton 中建立 `RifleReloadWeightedUpperBody` 分级 Blend Mask；
- `ABP_TP_Rifle` 复用 `ArmedAimPose`，在 Reload 内部以 Blend Mask 合成换弹动作；
- Reload 资源中的头颈表演被过滤，观察方向继续来自实时瞄准姿势；
- 最初按计划旁路 `spine_01/02/03` 后，俯仰时双臂保持水平，验证了 Base Pose 不能穿透手臂权重为 `1.0` 的覆盖层；
- 恢复 Reload 分支的分布式脊柱俯仰后，向上、向下换弹姿势与枪支方向恢复一致；
- `ReloadIKAlpha` 与 `ReloadRecovery` 继续负责 IK 平滑释放和表现收尾，没有成为权威换弹状态。

本阶段明确不处理极端低头手臂旋转、Pistol Reload 和第一人称 Reload。
