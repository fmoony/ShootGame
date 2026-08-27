# Rifle 左手 IK 与瞄准同步提速实施计划

> 状态：已完成并归档（2026-08-25）。L0～L4 的 Rifle 左手握持闭环已通过人工视觉验收；原 N1/N2 同步调参路线不再单独执行，剩余同步、贴墙稳定和手臂姿势问题统一转入《武器贴墙稳定与瞄准表现收尾实施计划》。Pistol 仍未接入左手 IK。

## 1. 当前结论

双客户端人工验收已经确认：

- 服务器表现目标、观察端平滑目标和 Rifle 第三人称枪口方向在稳定状态下基本闭合。
- 大部分上半身姿势在真实玩法距离下可以接受，暂不值得继续做全脊柱、锁骨和头部的大范围程序化分摊。
- `FAnimNode_ShooterAimIK` 当前只修改 `hand_r`；武器跟随右手完成最终枪口校正后，左手没有再次跟随最终武器姿势。
- 左手只有在水平姿势或右手校正较小时偶尔贴合，向上、向下和快速转向时容易离开护木。
- 当前表现目标服务器采样起点为 10 Hz，观察端指数平滑速率为 8/s；快速转向时可见延迟仍然过大。
- 第一人称左臂 IK 的观感可作为握把位置、肘部方向和手腕姿势的参考，但第一人称 Control Rig 还包含摄像机、脊柱和身体补偿，不得整体迁入第三人称。

因此下一步不继续校准 Camera/PawnEye，也不扩大右手 Aim IK，而是建立以下两个独立闭环：

```text
闭环 A：最终右手/武器姿势 → Rifle 左手握把目标 → 左臂 IK
闭环 B：服务器最新瞄准意图 → 观察端更快收敛 → 现有 AimOffset / ShooterAimIK
```

## 2. 目标与非目标

### 2.1 目标

- Rifle 左手在站立、移动、跳跃和俯仰瞄准时稳定贴合第三人称武器握把。
- 左手求解发生在 `Shooter Aim IK` 之后，跟随最终 `hand_r` 和武器姿势，不追逐上一帧世界坐标。
- Rifle 与 Pistol 的左手握把配置相互独立；没有握把配置时 IK 自动关闭。
- 将远端瞄准采样和平滑从当前调试起点调整到可接受响应区间，并通过双客户端和网络模拟验证。
- 保持服务器权威开火、散布、弹丸、伤害和 GAS Ability 行为不变。
- 保留 `ShootGame.Aim.DebugDraw 2` 的旧完整链路，Pistol 后续验收时可以重新启用。

### 2.2 非目标

本阶段不实现：

- Pistol 左手 IK；
- 全身或全上半身程序化姿势重构；
- 新的客户端开火预测、预测弹丸或服务器回滚；
- 每 Tick Reliable RPC；
- 动画或 AnimNotify 决定弹药、命中和武器切换；
- 复制完整骨骼姿势；
- 为了消除少量近距离视差而修改第一人称摄像机位置。

## 3. 依据与本地差异

Epic 的 Hand IK 示例使用 Hand IK Retargeting 与 Two Bone IK 维持双手持枪关系；UE5.6 本地 `FAnimNode_TwoBoneIK` 支持 Bone Space Effector、Joint Target、禁止拉伸和末端旋转选项：

- [Hand IK Retargeting](https://dev.epicgames.com/documentation/en-us/unreal-engine/hand-ik-retargeting?application_version=4.27)
- [IK Rig](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-ik-rig?lang=en-US)

当前项目与通用示例的差异是：右手还会被项目自定义 `Shooter Aim IK` 在 AnimGraph 后段继续校正。因此左手求解必须排在它之后，并使用相对 `hand_r` 的固定握把目标；直接复用第一人称世界空间 `Aim Point` 或在 Control Rig 前段提前锁定左手，会重新产生一帧滞后或双手分离。

属性复制保证客户端最终收到服务器最新状态，但不保证观察到每个中间值。表现目标应使用有限频率、量化数据和观察端平滑，不能用每 Tick 可靠 RPC 强行追帧：

- [Replicate Actor Properties](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine?application_version=5.6)
- [Performance and Bandwidth Tips](https://dev.epicgames.com/documentation/en-us/unreal-engine/performance-and-bandwidth-tips-for-unreal-engine?application_version=5.6)

## 4. 推荐运行时结构

### 4.1 武器握把契约

每种武器在自己的第三人称 Mesh 上配置左手握把 Socket。Rifle 推荐名称：

```text
LeftHandGrip
```

优先使用 Mesh Socket，避免共享 Skeleton 上的 Rifle/Pistol 握把互相污染。

武器侧提供只读能力：

```text
ThirdPersonLeftHandGripSocketName
HasThirdPersonLeftHandGripSocket
GetThirdPersonLeftHandGripWorldTransform
```

Socket 名默认允许为空。Rifle 蓝图明确配置；Pistol 当前保持为空，因此不会错误开启 Rifle 左手 IK。

### 4.2 AnimInstance 数据契约

`UShooterThirdPersonAnimInstance` 增加：

```text
LeftHandGripInRightHandSpace
bLeftHandIKEnabled
```

握把目标应缓存为：

```text
LeftHandGrip 相对角色 hand_r 的刚性 Transform
```

它只在武器变化、第三人称 Mesh 附着状态变化或缓存无效时重建。AnimGraph 不直接持有 Weapon Actor，也不在动画线程中任意访问世界对象。

### 4.3 AnimGraph 顺序

Rifle 推荐顺序：

```text
Locomotion / Jump
→ 已完成的上下半身分层
→ AimOffset / 当前必要 Control Rig
→ Shooter Aim IK（hand_r 与枪口最终校正）
→ Left Hand Two Bone IK（hand_l 跟随最终 hand_r/武器）
→ Output Pose
```

Two Bone IK 第一轮只解决位置：

```text
IK Bone                 = hand_l
Effector Location Space = Bone Space
Effector Target Bone    = hand_r
Effector Location       = LeftHandGripInRightHandSpace.Location
Allow Stretching        = false
Alpha                    = bLeftHandIKEnabled ? 1 : 0
```

Joint Target 参考第一人称左臂 Basic IK 的肘部弯曲方向，通过第三人称预览确定，不在 C++ 中写死未经验证的轴和数值。第一轮保留输入动画的手腕旋转；如果手掌位置正确但旋转明显不自然，再单独增加有限手腕旋转校正。

同一条左臂不得同时由旧 Control Rig 和新增 Two Bone IK 以全权重求解。接入前必须确认旧 `Ctrl_Hand_L` 路径是否仍生效；若生效，先关闭旧左臂权重，再启用最终 Two Bone IK。

## 5. 分阶段执行与阶段门

### L0：只读资产和节点审计

Agent：

- 使用 CodeGraph 核对 C++ 数据流。
- 使用只读 MCP 核对 `ABP_TP_Rifle` 中 `Shooter Aim IK`、Control Rig 和 Output Pose 的顺序。
- 核对 Rifle 第一/第三人称 Weapon Mesh、现有 Socket 与第一人称 `Ctrl_Hand_L` 参考。
- 记录当前 Blueprint CDO 中实际采样和平滑参数，不能只相信 C++ 头文件默认值。

完成条件：明确最终节点插入位置、旧左臂求解是否需要关闭、Socket 应创建在哪个 Mesh 上。

### L1：C++ 左手握把数据契约

Agent：

- 实现武器握把只读接口和无效状态回退。
- 在 `UShooterThirdPersonAnimInstance` 建立 `LeftHandGripInRightHandSpace` 与 `bLeftHandIKEnabled`。
- 补纯计算和状态矩阵 Automation Tests。
- Editor Build 与 Focused Tests 通过后停止。

本阶段禁止修改 `.uasset`，也不得声称左手已经在 AnimBP 中生效。

### L2：Rifle Socket 人工阶段门

人工：

- 打开 Rifle 实际使用的第三人称 Skeletal Mesh。
- 创建 Mesh Socket `LeftHandGrip`。
- 参考第一人称握持姿势，把 Socket 放到护木上左手掌应接触的位置，并确定合理旋转。
- 在 `BP_ShooterWeapon_Rifle` 配置第三人称左手 Socket 名。
- Compile、Save，并回复已完成。

Agent 随后只读复核 Socket、蓝图配置和资产保存状态。未通过前不得进入 L3。

### L3：ABP_TP_Rifle 人工接线阶段门

Agent 先给出基于 L0 实际节点名称的精确连线说明。

人工：

- 在 `Shooter Aim IK` 后接入 `Two Bone IK`。
- 设置 `hand_l`、Bone Space `hand_r`、Effector Location、Joint Target、Stretch 和 Alpha。
- 关闭旧的重复左臂求解路径。
- Compile、Save，并在预览与双客户端中检查。

Agent 随后使用只读 MCP 核对节点、Pin、变量来源和编译状态；不得仅凭用户口述宣称资产验收通过。

### L4：Rifle 左手视觉验收

人工矩阵：

| 场景 | 验收重点 |
| --- | --- |
| 水平、向上、向下瞄准 | 左手掌持续贴合护木，不漂浮 |
| 快速左右、上下转向 | 左手不落后一帧追逐，不突然翻肘 |
| 前后左右移动 | 手掌不因 Locomotion 混合脱离 |
| Jump / Fall / Land | 下半身分层不回归，左臂不过伸 |
| Fire Montage | 开火表现不覆盖或破坏最终左手约束 |
| 切枪、死亡、复活 | IK Alpha 正确关闭和重新建立 |

`ShootGame.Aim.DebugDraw 1` 只用于确认最终枪口没有回归；完成后恢复为 0。旧完整链路继续保留在模式 2。

### N1：同步参数实测调优

在左手闭环独立验收后，Agent 才调整同步参数。首轮候选值：

```text
PresentationAimSampleInterval   0.10 → 0.05 秒
PresentationAimSmoothingRate    8 → 18 /s
PresentationAimMinChangeAngle   3° → 1°
PresentationAimMinChangeDistance 50 → 20 cm
```

执行前必须核对 Blueprint CDO 是否覆盖这些默认值，并确认角色实际 `NetUpdateFrequency` 足以承载 20 Hz 目标更新。首轮不新增 RPC、不逐次 `ForceNetUpdate`。

验收时区分：

- `FinalMuzzle Angle` 接近 0，但表现方向落后：网络目标或观察端平滑过慢。
- 快速转向时 `FinalMuzzle Angle` 明显升高：检查 `MaxCorrectionAngle` 饱和、节点顺序或 HandToMuzzle，不继续盲目提高网络频率。

### N2：网络验证与升级门槛

依次验证：

```text
Listen Server
Dedicated Server
Emulated Network
Disconnect Cleanup
```

首轮响应预算：

- 本机双客户端快速转向后约 0.2 秒内达到稳定观感。
- 100 ms RTT 模拟下约 0.3 秒内收敛，不反向、不锁世界方向、不长期停留旧目标。
- 稳态 Rifle `FinalMuzzle Angle` 维持既有可接受范围。

只有仍不达标且已获得带宽、更新间隔和误差证据时，才另建计划评估“量化方向 + 距离”或有限角速度外推；本计划不提前实现远端预测。

## 6. Agent 与人工职责边界

### 6.1 Agent 自主完成

- CodeGraph、官方资料、本地 UE5.6 源码和只读 MCP 审计。
- Runtime C++、AnimInstance 数据契约、测试和调试代码。
- Build、Automation、网络脚本、日志与结果判定。
- 每次提交前的中文开发记录、差异审计和提交。
- 在人工资产操作后只读复查，不替用户猜测视觉结果。

### 6.2 必须由人工完成

- 在 Skeletal Mesh 中创建和摆放 `LeftHandGrip` Socket。
- 修改、Compile、Save `BP_ShooterWeapon_Rifle` 和 `ABP_TP_Rifle`。
- 调整 Joint Target、握把旋转和最终手腕观感。
- 双客户端肉眼确认手掌接触、肘部弯曲和真实游戏距离观感。
- 决定 Rifle 验收是否通过以及是否推广到 Pistol。

### 6.3 强制暂停条件

出现以下任一情况时，Agent 必须停止并请求人工操作：

- 下一步需要保存 `.uasset`。
- Socket 或 Joint Target 存在多个视觉上不同但代码上都合法的选择。
- 蓝图有未保存修改，自动操作可能覆盖用户工作。
- Rifle 人工视觉验收尚未确认。
- 需要决定是否接受姿势取舍或进入 Pistol。

## 7. 自动验证

至少覆盖：

- 无武器、无第三人称 Mesh、无握把 Socket 时左手 IK 必须关闭。
- 武器切换或重新附着后握把缓存正确重建。
- `LeftHandGripInRightHandSpace` 计算有限、非 Identity 且不依赖每帧世界点追逐。
- Rifle 左手功能不能改变 `AimDirectionWorld`、`PresentationAimTarget` 或服务器开火目标。
- Aim 现有 Automation 全部通过。
- Editor Build、项目全量 Automation 和网络脚本按风险完成。

## 8. 推荐提交拆分

### 提交 1：左手握把数据契约

```text
Weapon C++ 接口
AnimInstance 数据
Automation Tests
中文开发记录
```

不包含 `.uasset`，完成后进入人工 L2。

### 提交 2：Rifle 左手 IK 资产闭环

```text
Rifle Mesh Socket
BP_ShooterWeapon_Rifle 配置
ABP_TP_Rifle Two Bone IK
只读资产复核
人工视觉结果
中文开发记录
```

### 提交 3：瞄准同步提速

```text
采样 / 门槛 / 平滑参数
必要测试补充
双客户端和网络模拟结果
中文开发记录
```

每个提交只解决一个可回退闭环。L2、L3、L4 的人工阶段门不能因 Agent 能编译 C++ 而跳过。

## 9. 完成定义

本计划只有同时满足以下条件才可归档：

- Rifle 左手在验收矩阵中稳定贴合，不出现持续漂浮、过伸和翻肘。
- Rifle 最终枪口方向没有因左手 IK 回归。
- 快速转向响应达到 N2 预算，弱网下可以短暂滞后但不出现反向或长期错误。
- Pistol 未实施状态被明确记录，不被误报为完成。
- Build、Automation、网络验证和中文开发记录完整。
