# 第三人称 C++ 程序化瞄准 IK 实施计划

> 状态：Rifle 既定闭环已完成并归档（2026-08-24）。原 C3 全上半身多骨骼分摊分支未继续执行，因为人工验收认为大部分上半身姿势已可接受；后续改为只处理左手握持约束与同步响应速度。

## 0. 2026-08-23 执行顺序修订（优先于后文旧顺序）

本节依据当前已提交实现、未提交资产状态和双客户端 Debug 实测追加。若本节与后文 C3 / C4 顺序、第一轮执行指令或提交拆分冲突，以本节为准。

### 0.1 当前证据与修订结论

- d21d2de 已建立 Runtime / Editor 自定义 AnimNode；1660edb 已实现 Rifle 本地 hand_r 单骨骼数学闭环。
- 观察端青线已经从角色 Mesh 的 UShooterThirdPersonAnimInstance 读取 AimDirectionWorld。
- 当前 C++ 对非本地角色仍写入 Character->GetActorForwardVector()。
- 实测 AimVsActorForward 几乎为 0°；青线和黄线终点 EndDistance 为几十厘米，在线长 10000 cm 下视觉近似共点。
- 蓝线即使收敛青线，也只证明 IK 正确追踪了错误的远端输入，不能证明枪口对应服务器认可的瞄准点。

原顺序“C2 → 可选 C3 → C4”停止执行。后续强制顺序改为：

~~~text
C2 已有实现只读复核
→ C2.5 表现目标有效性与消费角色修复
→ C4 Rifle 远端 AimDirection 闭环
→ Rifle 几何 / 网络 / 视觉重新验收
→ 只有姿势仍不自然时才进入 C3
~~~

在 ActorForward 输入上调整 clavicle_r / upperarm_r / hand_r，会把临时回退固化成姿势补偿，因此 C3 当前冻结。

### 0.2 C2.5：当前实现的针对性修复

#### 稳态目标有效性

UpdatePresentationAimSmoothing 当前以 LastPresentationAimUpdateTime 超过 PresentationAimFallbackDelay 作为回退条件，但该时间只在 OnRep_PresentationAimTarget 更新，服务器又只在属性发生足够变化时修改复制值。“没有收到重复属性更新”表示值可能没有变化，不能证明目标失效。

首选规则：

~~~text
已收到的 PresentationAimTarget 持续有效
→ 仅在死亡、复活、切枪、传送、Pawn 重建等明确生命周期事件重置
→ 使用明确的本地有效标记
→ 不用 ZeroVector 或无变化超时长期充当失效判据
~~~

禁止用每 Tick Reliable RPC、重复强推相同世界点或无条件 ForceNetUpdate 掩盖问题。

#### 观察角色覆盖

“观察者”不只等于 ROLE_SimulatedProxy：

~~~text
拥有者本地 Pawn → 即时 GetBaseAimRotation
普通客户端远端 Pawn（SimulatedProxy）→ 复制目标 + 本地平滑
Listen Server 的远端客户端 Pawn（Authority 且非 LocallyControlled）
→ 服务器表现目标 + 本地表现平滑或等价稳定消费
Dedicated Server → 不增加不可见动画的高成本表现工作
~~~

GetAimPresentationAngles 与 IK 必须消费同一份有效目标语义，不能一个使用平滑目标、另一个回退 ActorForward。

#### IK 开关

FTransform::Identity 仍是 Valid Transform。无武器时只检查 Mesh 和 HandToMuzzle.IsValid()，可能错误得到 bAimIKEnabled=true。启用条件至少证明 Character、CurrentWeapon、ThirdPersonMesh / Muzzle、HandToMuzzle 和非零有限 AimDirection 全部有效。

ABP_TP_Rifle 必须让 bAimIKEnabled 真正控制 Shooter Aim IK 的 Alpha 或等价开关，不得保留永久 Alpha=1 的伪开关。

C2.5 自动门槛：

- 稳定目标超过旧 PresentationAimFallbackDelay 后仍有效。
- 死亡、复活、切枪和传送正确重置。
- SimulatedProxy 使用平滑目标。
- Listen Server 观察远端客户端不走仅 ActorForward 的旧路径。
- 无 CurrentWeapon 时 bAimIKEnabled=false。

### 0.3 C4：Rifle 远端 AimDirection 针对性方案

复用已有服务器表现目标，不新增第二套客户端 Aim RPC：

~~~text
服务器 ControlRotation
→ ComputePreSpreadAimTarget
→ PresentationAimTarget（COND_SkipOwner）
→ 观察实例稳定 / 平滑目标
→ (Target - ThirdPersonMuzzleLocation).GetSafeNormal()
→ UShooterThirdPersonAnimInstance::AimDirectionWorld
→ Shooter Aim IK
~~~

AimOffset 可以从 View / Aim Pivot 指向目标计算局部角度；枪口 IK 必须从 ThirdPerson Muzzle 指向同一目标。本地拥有者继续使用即时 GetBaseAimRotation().Vector()；远端目标、武器或方向无效时优先关闭 IK，只有记录过的临时路径才允许回退 ActorForward。

C4 同时复核 HandToMuzzle 的 Attach 后建立时机、挂接重建刷新、MaxCorrectionAngle 的真实语义，以及 AimOffset / Arms Slot 与 IK 是否消费同向目标。

### 0.4 同一远端 Pawn 的验收

青线使用 AnimInstance.AimDirectionWorld，蓝线使用 ThirdPerson Muzzle Forward，目标点使用当前有效的 StablePresentationAimTarget。输出 ActorName、Role、IsLocallyControlled、PresentationTargetValidity、AimVsActorForward、MuzzleVsAimDirection 和 MuzzleToPresentationTargetAngle。

- 上下瞄准时 AimVsActorForward 不得长期为 0°。
- 稳定瞄准超过旧 0.5 秒窗口后，青线不得跳回 ActorForward。
- Rifle 静止中距离稳定后，蓝线与青线目标约 <=3°～5°。
- 快速转向允许网络 / 平滑滞后，但不得世界锁死、左右反转或永久侧翻。
- 近点和远点使用同一个服务器表现目标分别记录视差。

### 0.5 C3 重新开放条件

~~~text
蓝线与青线夹角仍大 → 返回 C4、空间、HandToMuzzle 与缓存排查
夹角已小但手腕 / 前臂 / 肩部明显难看 → 才进入 C3
几何和姿势均可接受 → 跳过 C3，直接收尾 Rifle
~~~

### 0.6 下一位 Agent 的首轮边界

首轮只执行 C2 当前实现与资产只读复核、C2.5 C++ / 测试修复、Editor Build 和 Focused Tests。完成后停止汇报，不得自动进入 C4 / C3，也不得修改、清理或提交当前用户的 AimOffset、AnimBP、Control Rig 和 Debug 蓝图实验资产。

## 1. 计划定位

本计划承接 `第三人称瞄准表现同步实施计划` 中 B5“按误差门槛决定是否增加程序化校正”。

当前已经通过本地调试确认：

- 第一人称黄色射线代表本地真实瞄准方向；
- 第三人称蓝色射线代表 Rifle `Muzzle` Socket Forward；
- 两条方向直接做 `Dot → ACOSd` 后，第三人称枪口与真实瞄准方向在正常 Pitch 范围内存在明显动态误差，最大约 32°；
- 误差不是固定 Rotation Offset；
- 现有三样本 AimOffset（Down / Horizontal / Up）无法稳定保证中间 Pitch 的真实 Muzzle 几何朝向；
- 手工增加 AimOffset Sample 会影响误差，但人工逐骨骼校准精度和迭代效率较低；
- 之前复制第一人称 Control Rig 做运行时残差校正时，因 World AimPoint 滞后、空间混用、第一人称躯干逻辑残留和右手单独承担大角度修正，出现快速转向翻侧等问题。

因此本计划不继续扩大原 Control Rig 实验，而是建立一个 **C++ 自定义 Skeletal Control AnimNode**：

```text
第三人称基础动画 / AimOffset
        ↓
Shooter Aim IK（C++）
        ↓
真实 Muzzle Forward
        ≈
Aim Direction
```

本阶段只改善第三人称动画表现，不修改服务器权威射击、Projectile、Spread、Damage 或 GAS Ability 结果。

---

## 2. 本阶段最终目标与非目标

### 2.1 最终目标

- 新增 Runtime `FAnimNode_ShooterAimIK`，负责第三人称 Rifle 程序化枪口校正。
- 新增 Editor-only `UAnimGraphNode_ShooterAimIK`，使节点可以在 AnimBP 中作为普通 Skeletal Control 节点使用。
- AimOffset 继续负责基础自然姿势；C++ Aim IK 负责真实 `Muzzle` 几何朝向。
- 第一版只验证 Rifle、本地即时 AimDirection、右手单骨骼闭环。
- 正常 Pitch 范围内，黄色真实瞄准方向与蓝色 Muzzle Forward 的三维夹角目标压到约 `3°～5°` 内。
- 快速左右 / 上下转向时不得再出现锁世界方向、锁角色固定方向或突然侧翻。
- 蓝图只承担必要的资产接线，不承载 IK 数学。

### 2.2 明确非目标

本计划首轮不实现：

- Pistol；
- LeftHandGrip；
- Spine / Clavicle / UpperArm 多骨骼权重分摊；
- Spring；
- Recoil；
- Spread / Bloom；
- Reload / Equip 动画；
- Local Prediction；
- 预测弹丸；
- Server Rewind；
- 用 C++ Aim IK 替换第一人称 Control Rig；
- 大量新建 AimOffset Sample；
- 删除当前 `PresentationAimTarget`；
- 一次性完成远端网络 AimDirection 重构。

这些内容只能在 Rifle 本地几何闭环成立后继续评估。

---

## 3. 当前项目与工具边界

### 3.1 当前项目基线

当前 Runtime 主模块为：

```text
ShootGame
```

项目已有：

```text
ShootGameEditor.Target.cs
```

但当前没有长期 `ShootGameEditor` Editor Module。

本阶段预计新增：

```text
ShootGame
→ Runtime AnimNode / 数学 / AnimInstance 数据

ShootGameEditor
→ AnimGraphNode 编辑器包装
```

Runtime 节点依赖 UE `AnimGraphRuntime`；Editor 包装依赖 `AnimGraph`。

### 3.2 Agent 当前可以自主完成

Agent 可以自主：

- 使用 CodeGraph 定位 C++；
- 读取当前源码 / 配置 / Build.cs / Target.cs / `.uproject`；
- 使用项目只读 Unreal MCP 审计 AnimBP / Control Rig / 资产引用；
- 新建和修改 C++；
- 新建 / 修改 Runtime 与 Editor Module 文本文件；
- 修改 Build.cs、Target.cs、`.uproject`；
- 运行 `RefreshVisualStudioFiles.ps1`；
- 命令行 Build；
- 编写并运行 Automation / Feature Tests；
- 分析编译日志和测试结果；
- 新建开发记录；
- 提交自己产生的文本 / C++ 改动；
- 用户完成蓝图修改后，再通过只读 MCP 复查节点、Pin、资产引用和编译状态。

### 3.3 Agent 当前不能自主完成

**这是本计划的硬边界。**

当前项目对 Blueprint / AnimBP / Control Rig 使用的是 **只读 MCP 检查能力**。Agent 不能可靠地直接修改、连线、保存 `.uasset`。

因此 Agent 不得把以下事项写成“自行完成”：

- 在 `ABP_TP_Rifle` 中放置 `Shooter Aim IK` 节点；
- 将 Pose 链接到自定义节点；
- 将 AnimBP 变量链接到自定义节点 Pin；
- 修改 AnimBP Parent Class；
- 修改 Control Rig 图；
- 修改 AimOffset Sample；
- Compile / Save 这些 `.uasset` 并把结果当作自己已经完成；
- 通过直接写二进制 `.uasset` 绕过 Unreal Editor。

这些步骤必须明确停下来交给用户。

### 3.4 人工蓝图修改必须成为“阶段门”

凡是下一步依赖 `.uasset` 接线：

```text
Agent 完成 C++ / Build
        ↓
Agent 输出明确的人工操作清单
        ↓
停止
        ↓
用户在 Unreal Editor 中修改、Compile、Save
        ↓
用户回复“已完成”
        ↓
Agent 使用只读 MCP 复查
        ↓
才能继续后续实现 / 验证
```

Agent 不允许：

```text
假设用户已经接线
→ 继续写后续代码
→ 最后才发现 AnimBP 根本没接入
```

---

## 4. 外部依据与 UE5.6 实现方向

Epic 的 Skeletal Control 体系将运行时节点和编辑器图节点分开：

```text
FAnimNode_SkeletalControlBase
→ Runtime / AnimGraphRuntime
→ EvaluateSkeletalControl_AnyThread
→ 输出受影响骨骼的 Component Space Transform

UAnimGraphNode_SkeletalControlBase
→ Editor / AnimGraph
→ 作为 AnimBlueprint 中可编辑的节点外壳
```

因此本计划采用：

```text
Runtime 算法
+
Editor 节点包装
```

而不是继续在 Control Rig Blueprint 中搭大量数学节点。

正式施工前，Agent 必须按 `AGENTS.md` 要求：

```text
项目基线（CodeGraph / 只读资产）
→ Epic 官方资料
→ 本地 UE5.6 Engine 源码核对
→ 再实现
```

至少核对：

- `FAnimNode_SkeletalControlBase` 在 UE5.6 的头文件、模块依赖和 override；
- `UAnimGraphNode_SkeletalControlBase` 的 Editor Module 依赖；
- `EvaluateSkeletalControl_AnyThread` 的 Component Space 约束；
- `FBoneReference` 初始化和 RequiredBones 使用方式；
- Editor Module 不进入 Game / Shipping Target。

---

## 5. 目标职责边界

```text
AShooterCharacter / GameThread
├─ 提供当前 Aim Intent
├─ 提供当前武器绑定关系
└─ 不直接修改骨骼

UShooterThirdPersonAnimInstance
├─ 缓存 AimDirectionWorld
├─ 缓存 HandToMuzzle
└─ 向 AnimGraph 提供稳定数据

FAnimNode_ShooterAimIK
├─ 读取当前 Component Space Pose
├─ 计算真实 Muzzle 当前方向
├─ 计算 Muzzle → AimDirection 的旋转差
└─ 输出受控骨骼 Transform

UAnimGraphNode_ShooterAimIK
└─ 只负责 AnimBP 编辑器节点外壳

ABP_TP_Rifle
└─ 用户人工完成最小节点接线
```

Gameplay Truth 继续保持：

```text
GA_Fire / Server
→ 当前 ControlRotation
→ 权威 Target
→ Spread
→ Projectile
→ Damage
```

第三人称 Aim IK 只影响 Presentation。

---

## 6. 数据设计

### 6.1 AimDirection

第一轮本地验证只使用即时方向：

```text
Locally Controlled Character
→ GetBaseAimRotation().Vector()
→ AimDirectionWorld
```

第一轮 **不使用**：

```text
SmoothedPresentationAimTarget
```

作为 IK 输入。

目的：

- 先排除网络采样与 World Point 平滑；
- 只验证 C++ 几何求解是否正确；
- 快速转向时 AimDirection 立即变化，不追逐旧世界目标。

远端网络 Direction 在本地闭环成功后另开阶段。

### 6.2 HandToMuzzle

定义：

```text
HandToMuzzle
=
ThirdPerson Muzzle 相对于 hand_r 的刚性 Transform
```

要求：

- Weapon Attach / Equip / 切换时刷新；
- 同一把已附着武器不每帧重算；
- 不使用上一帧 IK 后的 `MuzzleWorld` 反向更新自身输入。

如果首轮实现发现当前 Weapon 生命周期很难安全缓存，可先做 Rifle 专用最小缓存，不提前重构整个 WeaponDefinition。

---

## 7. 分阶段实施路径

## C0：冻结当前实验基线

### Agent 自主操作

1. 审计当前未提交的 Control Rig / AimOffset 实验状态。
2. 不自动修改 `.uasset`。
3. 记录已知基线：
   - 黄色：真实瞄准方向；
   - 蓝色：TP Muzzle Forward；
   - `Dot → ACOSd`：真实三维夹角；
   - Rifle 最大误差约 32°；
   - 之前 `RightHandWeight=0` 时快速转向翻侧消失。
4. 审计当前 `ABP_TP_Rifle` 最终 Pose 链中未来插入 Skeletal Control 的位置。

### 必须交给用户

如果当前 AnimBP / Control Rig 仍包含实验节点，需要用户决定：

- 保留但 Alpha=0；
- 或人工断开实验 Control Rig。

Agent只负责告诉用户具体资产和节点位置，不自行保存 `.uasset`。

### C0 完成条件

用户确认：

```text
当前 Rifle 已回到稳定基线
黄色 / 蓝色 Debug 仍可观察
实验 Control Rig 不再影响最终 Pose
```

---

## C1：建立 Runtime AnimNode + Editor GraphNode 空壳

本阶段 **不实现 IK**，只验证项目基础设施。

### Agent 自主操作

新增：

```text
Source/ShootGame/Animation/AnimNodes/
├─ AnimNode_ShooterAimIK.h
└─ AnimNode_ShooterAimIK.cpp

Source/ShootGameEditor/
├─ ShootGameEditor.Build.cs
├─ ShootGameEditor.h
├─ ShootGameEditor.cpp
└─ Animation/
   ├─ AnimGraphNode_ShooterAimIK.h
   └─ AnimGraphNode_ShooterAimIK.cpp
```

并修改：

```text
Source/ShootGame/ShootGame.Build.cs
Source/ShootGameEditor.Target.cs
ShootGame.uproject
```

第一版：

```text
Shooter Aim IK
→ 继承 SkeletalControlBase
→ Alpha 可用
→ 不输出骨骼修改
→ Pose 完全透传
```

Agent随后：

1. 运行一次 VS Project Refresh；
2. BuildEditor；
3. 按实际可用脚本验证 Game Target；
4. 运行 Focused Automation；
5. 确认 Runtime 模块没有 Editor-only 依赖泄漏；
6. 写开发记录。

### C1 人工阶段门：用户必须链接 AnimBP

Agent Build 成功后 **必须停止**，输出以下人工操作，不得自行假设已经完成：

```text
打开 ABP_TP_Rifle
→ 找到当前 AimOffset / Layered Blend 后的最终 Pose
→ 搜索 Shooter Aim IK
→ 将其插入最终 Pose 链
→ 当前 Alpha 保持 0 或保持空实现
→ Compile
→ Save
```

如果节点无法搜索：

```text
停止
→ 用户反馈截图 / 错误
→ Agent 排查 Editor Module / GraphNode 注册
```

### 用户完成后 Agent复查

用户回复“已链接并保存”后，Agent：

- 使用只读 Unreal MCP 打开 / 审计 `ABP_TP_Rifle`；
- 确认存在 `Shooter Aim IK`；
- 确认 Source Pose / Result Pose 链正确；
- 确认资产 Compile 状态；
- 确认没有多余 Control Rig 实验链重新接入。

只有复查通过，C1 才能标记完成。

### C1 验收

```text
节点存在
+
ABP 编译
+
姿势与基线完全一致
+
BuildEditor 通过
+
Standalone 正常
```

---

## C2：建立 Rifle 本地单骨骼几何闭环

这是第一个真正修改姿势的阶段。

### C2.1 Agent：实现数据源

建立 / 完善：

```text
UShooterThirdPersonAnimInstance
```

向 AnimGraph 提供：

```text
AimDirectionWorld
HandToMuzzle
bAimIKEnabled
```

第一版 Aim：

```text
LocallyControlled
→ GetBaseAimRotation().Vector()
```

第一版 HandToMuzzle：

```text
Rifle Weapon Attach / Equip
→ 计算一次
→ 缓存
```

Agent同时为这些值增加：

- IsFinite / IsNearlyZero 保护；
- 日志或 Debug Getter；
- 能够在 Automation 中验证的纯数据路径。

### C2.2 Agent：实现单骨骼 AnimNode

`FAnimNode_ShooterAimIK` 第一版：

```text
HandBone = hand_r
AimDirectionWorld
HandToMuzzle
Alpha
MaxCorrectionAngle
```

求解步骤：

```text
1. 读取 hand_r 当前 Component Space Transform
2. World AimDirection → Component Space
3. HandToMuzzle.Rotation × Muzzle +X
   → MuzzleForwardInHand
4. hand_r 当前 Rotation × MuzzleForwardInHand
   → CurrentMuzzleForwardCS
5. Current → Desired
   → 最短 Rotation Delta
6. 按 Alpha / 安全角限制应用到 hand_r Rotation
7. Translation / Scale 保持输入 Pose
8. 输出 FBoneTransform
```

必须 fail-soft：

```text
无效方向
无效 Bone
非有限值
异常 Transform
→ 不修改 Pose
```

### C2.3 Agent：自动化

抽出 Aim IK Math Helper，至少覆盖：

```text
Identity
Pitch 30°
Yaw 30°
Combined Pitch/Yaw
HandToMuzzle Rotation Offset
Zero Direction
Invalid Numeric
MaxCorrection
```

要求数学测试不依赖人工打开 AnimBP。

---

## C2 人工阶段门：链接输入 Pin

Agent 完成 C++ 和自动化并 Build 成功后 **停止**。

由于 Agent 不能修改 `ABP_TP_Rifle.uasset`，用户人工完成最小接线。

Agent必须根据实际生成的节点 Pin 给出逐项操作，例如：

```text
ABP_TP_Rifle

已有 Pose
→ Shooter Aim IK.Source Pose

AimDirectionWorld
→ Shooter Aim IK.Aim Direction

HandToMuzzle
→ Shooter Aim IK.Hand To Muzzle

Alpha = 1

Shooter Aim IK.Result
→ 原后续 Pose
```

如果通过 AnimInstance Property Access / 节点 Property Binding 能减少 Pin，Agent可以在实现后推荐最少接线版本，但 **不得为了少两根蓝图线提前引入复杂 Proxy 架构**。

用户：

```text
Compile
Save
运行 Rifle
```

然后反馈：

- 黄色 / 蓝色线截图；
- AngularError；
- 水平 / 上 / 下；
- 快速左右；
- 快速上下；
- 是否有手腕明显变形。

### C2 Agent复查

用户保存后，Agent使用只读 MCP：

- 确认节点连接；
- 确认输入 Pin 来源；
- 确认 Alpha；
- 确认实验 Control Rig 没有同时作用；
- 必要时只读检查运行日志 / 自动化结果。

---

## C2 验收门槛

### 几何验收

Rifle、本地：

```text
水平
中间上瞄
最大上瞄
中间下瞄
最大下瞄
```

目标：

```text
黄色 Aim Direction
≈
蓝色 Muzzle Forward

AngularError
正常区间约 <= 3°～5°
```

### 稳定性验收

```text
慢速左右
快速左右
慢速上下
快速上下
移动中瞄准
```

不得：

- 朝固定世界方向锁死；
- 朝角色固定相对方向锁死；
- 快速转向突然指向侧面；
- NaN；
- Weapon Attach 抖动；
- 第一人称姿势受到影响。

### 功能回归

不得改变：

```text
GA_Fire
Ammo
Projectile
Damage
Inventory
```

### C2 失败时

如果几何仍不正确：

```text
停止
→ 不进入多骨骼
→ 不增加 Spring
→ 不调整复杂 Alpha 曲线
```

只检查：

```text
AimDirectionWorld
Component Space 转换
HandToMuzzle
MuzzleForwardInHand
CurrentMuzzleForward
Rotation Delta
```

---

## 8. C3：多骨骼自然姿势（仅冻结方向）

只有 C2 满足：

```text
枪口几何已经正确
但 hand_r 单独承担大角度修正，姿势明显不可接受
```

才进入。

候选链：

```text
clavicle_r
upperarm_r
hand_r
```

必要时少量 Spine。

目标：

```text
基础 AimOffset
→ 提供自然大方向

程序化骨骼链
→ 分摊 Correction

hand_r
→ 最终 Muzzle 收尾
```

C3 不提前冻结：

- 精确骨骼权重；
- Spring；
- DeadZone；
- 曲线；
- 最大角速度。

先根据 C2 实际姿势再设计。

C3 仍需要用户在 AnimBP 中进行必要资产侧参数调整；Agent不能自行保存 `.uasset`。

---

## 9. C4：远端 AimDirection（仅冻结方向）

C2 / C3 本地稳定后再处理网络。

目标是：

```text
Server 认可的 Aim Intent
→ 可复制 Aim Direction / Rotation
→ SimulatedProxy 本地方向平滑
→ AimOffset
→ Shooter Aim IK
```

此阶段再审计：

```text
PresentationAimTarget
SmoothedPresentationAimTarget
RemoteViewPitch16
```

决定是否：

- 从 Point 派生 Direction；
- 直接复制量化 Rotation；
- 或复用已有 Point 但仅在 AnimInstance 中转成 Direction。

原则：

```text
IK 最终消费 Direction
```

不得再次：

```text
Lerp 一个旧 WorldPoint
→ 右手无限制追逐该点
```

网络阶段完成后才运行 Dedicated / Listen / Emulated 全矩阵。

---

## 10. Agent 与用户协作方式

### Agent 自主负责

- C++ / Build.cs / Target / `.uproject`；
- Runtime AnimNode；
- Editor GraphNode；
- AnimInstance 数据契约；
- Aim IK 数学；
- Feature Tests；
- Build；
- 自动化；
- 日志与编译错误修复；
- 开发记录；
- 只读资产复查；
- 每个阶段的人工操作清单。

### 必须交给用户

- `ABP_TP_Rifle` 中放置 `Shooter Aim IK`；
- Pose Pin 接线；
- AimDirection / HandToMuzzle 等输入 Pin 接线；
- AnimBP Compile / Save；
- Control Rig / AimOffset `.uasset` 的任何修改；
- 视觉姿势自然度验收；
- 黄色 / 蓝色调试线观感验收；
- 用户确认后是否继续多骨骼阶段。

### Agent 的停止规则

遇到以下情况 Agent 必须停止，不得假装自动完成：

```text
下一步必须改 AnimBP
下一步必须改 Control Rig
下一步必须改 AimOffset
下一步必须人工点击 Compile / Save 才能产生新 .uasset 状态
下一步需要视觉判断
```

Agent应输出：

```text
当前已自动完成什么
需要用户打开哪个资产
找到哪个节点
新增 / 断开哪根线
设置哪个 Pin
Compile / Save
预期看到什么
完成后回复什么
```

用户完成后，Agent再继续。

---

## 11. 验证策略

### Fast Loop

Agent 自动：

```text
Build
+
Animation.AimIK Feature Tests
```

### Asset Gate

用户修改 `.uasset` 后：

```text
用户 Compile / Save
↓
Agent 只读 MCP Audit
```

### Visual Gate

用户：

```text
黄色 Aim Direction
蓝色 Muzzle Forward
AngularError
```

进行 Rifle 人工验收。

### Network Gate

只在 C4 后进入：

```text
DedicatedNetwork
ListenNetwork
EmulatedNetwork
```

### Full Regression

阶段稳定后按现有七阶段：

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

继续读取 `Summary.json`、Marker 和进程泄漏结果，不只看退出码。

---

## 12. 推荐提交拆分

每次提交前按 `开发记录规范` 写中文记录。

### Commit 1：建立 Shooter Aim IK 节点基础设施

Agent：

```text
Runtime AnimNode 空壳
Editor GraphNode
Module / Target / Build
自动 Build
```

不包含 `.uasset`。

提交后进入人工 C1 Asset Gate。

### Commit 2：Rifle 本地 Aim IK 数学闭环

Agent：

```text
AnimInstance 输入
HandToMuzzle 缓存
单 hand_r 求解
Math Feature Tests
Build
```

仍不包含用户尚未完成的 AnimBP 改动。

用户完成 C2 Asset Gate 后，再决定 `.uasset` 是否作为单独人工资产提交或与阶段验收记录一起提交。

### Commit 3：C2 验收收尾

仅在人工视觉验收通过后：

```text
最终参数
开发记录
必要测试补充
ABP_TP_Rifle 资产
```

然后暂停。

C3 / C4 不自动开始。

---

## 13. 第一轮执行指令

Agent 第一次拿到本计划时只执行：

```text
C0 审计
+
C1 Runtime / Editor 节点基础设施
```

完成 C1 自动验证后：

> **停止。**

不得自行声称已经“接入 ABP_TP_Rifle”。

必须向用户输出明确人工接线步骤。

用户完成并保存 `ABP_TP_Rifle` 后，Agent只读复查通过，再开始 C2。

这条人工阶段门是本计划的一部分，不是执行异常。
