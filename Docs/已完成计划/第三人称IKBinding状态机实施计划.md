# 第三人称 IK Binding 状态机实施计划

> 状态：已完成（2026-08-27）。阶段 0～3 与阶段 5 已实施、提交并回归通过；阶段 4 人工 PIE 视觉回归待人工验收（见 `Docs/开发记录/2026-08-27-1537-第三人称IKBinding状态机迁移.md` 遗留项）。本计划只重构第三人称表现层的 Aim IK / Left Hand IK 开关判定，不修改 `.uasset`、不改变 AnimGraph 节点结构、不触碰网络、权威开火与武器生命周期。AnimInstance 对外暴露的 `bAimIKEnabled`、`bLeftHandIKEnabled`、`HandToMuzzle`、`LeftHandGripInRightHandSpace`、`HandGripInLeftHandSpace` 保持兼容。

## 1. 背景与已确认事实

- `UShooterThirdPersonAnimInstance::UpdateShooterAnimationData` 目前用一组散落的缓存 bool 组合 `bAimIKEnabled` / `bLeftHandIKEnabled`：
  - `CachedWeapon`、`bCachedWeaponThirdPersonMeshAttached`；
  - `CachedLeftHandGripWeapon`、`bCachedLeftHandGripThirdPersonMeshAttached`；
  - `bLeftHandGripCacheDirty`；
  - `bCachedThirdPersonHandSocketExists`、`bCachedLeftHandBoneExists`、`bCachedHandGripSocketExists`。
- 当前逻辑存在三个明确问题：
  1. `bCachedHandToMuzzleInvalid` 会在 Weapon 存在且 `HandToMuzzle == Identity` 时每帧尝试重建，但 `bHasThirdPersonMuzzle` 又在每帧查询 Socket，缺失 Socket 的稳定状态没有缓存保护。
  2. 左手握把缓存用一组 bool 区分“尚未建立”“已确认缺失”“需要重建”，语义分散，测试只能覆盖局部组合。
  3. `FTransform::Identity` 被同时当作“默认值”“未计算”“缺失 Socket”和“无效数据”使用；`FShooterLeftHandIKMath::IsUsableFrame`、`IsAimIKEnabledForState` 和 `IsLeftHandIKEnabledForState` 都把 Identity 当作失败。
- `CurrentWeaponActor` 复制、`Weapon->Owner` 复制和 `AttachWeaponMeshes` 存在先后时序：`ShooterEquipmentComponent::ApplyCurrentWeapon` 在 `CurrentWeaponActor->GetOwner() != Character` 时暂不附着，等 `HandleWeaponActorReady` 补做。因此 WeaponOwner、AttachParent、AttachSocketName 必须纳入依赖签名。
- `ShooterAnimInstanceBase` 已用 `IsValid(CurrentWeaponActor) && CurrentWeaponActor->GetOwner() == Character` 定义 `bHasEquippedWeapon`；Binding 的 Weapon 有效性判定应与其保持一致。
- 当前 AnimGraph 只消费 AnimInstance 的公开只读数据；`FAnimNode_ShooterAimIK` 只按数学有效性检查 `HandToMuzzle`，`FAnimNode_ShooterLeftHandIK` 消费两个握把参考帧。节点顺序不改变。

## 2. 目标与非目标

### 2.1 目标

- 将 Aim IK 与 LeftHand IK 的依赖判定迁移到统一五状态模型：
  - `Unbound`
  - `WaitingForAttach`
  - `Pending`
  - `Unsupported`
  - `Ready`
- 用 `TWeakObjectPtr` 组成依赖签名，Binding 不再强持有 Character / Weapon / Mesh。
- `Identity` 允许作为合法 Transform；Transform 是否可消费只由 `Binding.State == Ready` 决定。
- `Pending` 1.0 秒仍未 `Ready` 时 `LogShootGame Error` 一次，之后继续每帧重试，不降频。
- `WaitingForAttach` 超过配置时长（默认 3.0 秒，Clamp 3.0～5.0）时开发期 `Warning` 一次，之后继续等待。
- `Unsupported` 只在依赖签名变化后重新检查，不再每帧查询 Socket。
- `Ready` 签名不变时零 Socket 查询、零 Transform 重算。
- 删除所有 `FTransform::Identity == invalid` 的旧哨兵语义，包括 AnimInstance 判定和 `FShooterLeftHandIKMath::IsUsableFrame`。

### 2.2 非目标

- 不新增 `TryCompute` 或任何 bool 返回的计算层；保留原 `Compute*` 函数签名。
- 不增加 `IsRegistered()`、首帧标记或其他“组件未就绪”的额外判据。
- 不修改 `.uasset`、AnimGraph 连线、Socket 配置和武器蓝图。
- 不改变服务器权威开火、GAS、复制和武器切换事务。
- 不把死亡、Montage、AimDirection 无效、快速转身等帧级运行条件塞进 Binding 状态。
- 不接入 Pistol 左手 IK；Pistol 仍自然表示为 LeftHand `Unsupported(WeaponLeftHandGripNotConfigured)`。

## 3. 依据与本地差异

- 官方依据：
  - [Weak Pointers in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/weak-pointers-in-unreal-engine?application_version=5.6)：Binding 依赖观察使用 `TWeakObjectPtr`，不额外阻止对象回收。
  - [FWeakObjectPtr API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/FWeakObjectPtr?application_version=5.6)：两个都解析为 null 的弱指针视为相等，适合“对象销毁”与“从未出现”统一为依赖缺失。
  - [UAnimInstance::NativeUpdateAnimation](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Animation/UAnimInstance/NativeUpdateAnimation?application_version=5.1)：Pending / WaitingForAttach 按 `DeltaSeconds` 累计游戏时间，不按墙钟计时。
  - [USkinnedMeshComponent::DoesSocketExist](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Components/USkinnedMeshComponent/DoesSocketExist?application_version=5.1)：Socket 不存在时返回 `NAME_None`；Rebuild 前置检查后才允许读取 Socket Transform。
- 本机 UE 5.6 源码已核对：
  - `FWeakObjectPtr::Get()` 对 Garbage / PendingKill 对象返回 null；`operator==` 规定两个 null 解析结果相等。
  - `FTransform::IsValid()` 只检查 NaN/Inf 和 Rotation 归一化，不检查 Scale 是否为零或负；因此本计划额外定义 Scale 检查。
  - `USkinnedMeshComponent::GetSocketTransform` 对缺失 Socket 或骨骼会退回组件 Transform；Rebuild 必须先用 `DoesSocketExist` 把关，不能依赖返回值猜测。
- 项目本地差异：
  - 第三人称武器 Mesh 附着在 `Character->GetMesh()` 的 `ThirdPersonWeaponSocket`（当前为 `HandGrip_R`）上；AttachParent 和 AttachSocketName 都可能独立变化。
  - 原 `Compute*` 函数把输入 Identity 当失败；本计划删除这些 Identity 特判，但保留函数签名与几何计算职责。

## 4. 最终状态模型

### 4.1 状态枚举

```cpp
enum class EIKBindingState : uint8
{
    Unbound,
    WaitingForAttach,
    Pending,
    Unsupported,
    Ready
};
```

| 状态 | 含义 | 自动重试 |
| --- | --- | --- |
| `Unbound` | Character / CharacterMesh / CurrentWeapon / Owner 等依赖尚未建立 | 否，等待签名变化 |
| `WaitingForAttach` | Weapon 与 WeaponMesh 存在，但 Mesh 尚未 Attach 到当前 CharacterMesh | 否，等待签名中 Attach 信息变化；只走超时诊断 |
| `Pending` | 前置结构齐全，但本次 Transform 计算数学非法 | 是，签名不变时每帧 Rebuild |
| `Unsupported` | 当前武器 / 骨架稳定地不提供该 IK 所需能力 | 否，仅签名变化后重新检查 |
| `Ready` | 所有依赖与缓存 Transform 已建立，可被本帧 Enabled 消费 | 否，直接使用缓存 |

### 4.2 失败原因

```cpp
enum class EIKBindingFailureReason : uint8
{
    None,

    MissingCharacterMeshAsset,
    MissingWeaponMeshComponent,
    MissingWeaponMeshAsset,
    MissingWeaponAttachSocket,

    MissingRightHand,
    MissingLeftHand,
    MissingMuzzle,
    MissingCharacterHandGrip,

    WeaponLeftHandGripNotConfigured,
    WeaponLeftHandGripSocketMissing,

    InvalidHandToMuzzle,
    InvalidWeaponGripInRightHandSpace,
    InvalidHandGripInLeftHandSpace
};
```

不变式：

| State | FailureReason |
| --- | --- |
| `Unbound` | `None` |
| `WaitingForAttach` | `None` |
| `Pending` | 对应的 `Invalid*` |
| `Unsupported` | 对应的 `Missing*` / `NotConfigured` |
| `Ready` | `None` |

### 4.3 依赖签名

签名只用于观察依赖变化，不持有生命周期。所有 UObject 字段使用 `TWeakObjectPtr<T>`。

公共签名：

```cpp
struct FShooterIKBindingCommonSignature
{
    TWeakObjectPtr<AShooterCharacter> Character;
    TWeakObjectPtr<USkeletalMeshComponent> CharacterMesh;
    TWeakObjectPtr<USkeletalMesh> CharacterMeshAsset;       // CharacterMesh->GetSkeletalMeshAsset()

    TWeakObjectPtr<AShooterWeapon> Weapon;
    TWeakObjectPtr<AActor> WeaponOwner;                     // Weapon->GetOwner()
    TWeakObjectPtr<USkeletalMeshComponent> WeaponMesh;
    TWeakObjectPtr<USkeletalMesh> WeaponMeshAsset;          // WeaponMesh->GetSkeletalMeshAsset()

    TWeakObjectPtr<USceneComponent> WeaponMeshAttachParent; // WeaponMesh->GetAttachParent()
    FName WeaponMeshAttachSocketName;                       // WeaponMesh->GetAttachSocketName()
};
```

Aim 专属签名：

```cpp
struct FAimIKBindingSignature : FShooterIKBindingCommonSignature
{
    FName HandSocketName;
    FName WeaponMuzzleSocketName;
};
```

LeftHand 专属签名：

```cpp
struct FLeftHandIKBindingSignature : FShooterIKBindingCommonSignature
{
    FName HandSocketName;              // 握把以 hand_r 为参考，变化也影响左手
    FName LeftHandBoneName;
    FName HandGripSocketName;
    FName WeaponLeftHandGripSocketName;
};
```

签名实现显式 `operator==`；`TWeakObjectPtr` 的 null 归一化语义负责把“已销毁”和“从未出现”视为相同依赖缺失。

### 4.4 Binding 结构体

```cpp
struct FAimIKBinding
{
    EIKBindingState State = EIKBindingState::Unbound;
    EIKBindingFailureReason FailureReason = EIKBindingFailureReason::None;

    FAimIKBindingSignature StoredSignature;
    FTransform HandToMuzzle = FTransform::Identity;

    float WaitingForAttachElapsedSeconds = 0.0f;
    bool bWaitingForAttachWarningReported = false;

    float PendingElapsedSeconds = 0.0f;
    bool bPendingTimeoutReported = false;
};

struct FLeftHandIKBinding
{
    EIKBindingState State = EIKBindingState::Unbound;
    EIKBindingFailureReason FailureReason = EIKBindingFailureReason::None;

    FLeftHandIKBindingSignature StoredSignature;
    FTransform WeaponGripInRightHandSpace = FTransform::Identity;
    FTransform HandGripInLeftHandSpace = FTransform::Identity;

    float WaitingForAttachElapsedSeconds = 0.0f;
    bool bWaitingForAttachWarningReported = false;

    float PendingElapsedSeconds = 0.0f;
    bool bPendingTimeoutReported = false;
};
```

Binding 只保存 State、FailureReason、StoredSignature、缓存 Transform 和计时/诊断字段；**不保存任何强引用**。两个 Binding 作为 AnimInstance 的普通私有成员存在，不需要 UPROPERTY 参与 GC；如需在 AnimBP 调试面板显示，额外暴露 State / Reason 的 `BlueprintReadOnly` 副本。

### 4.5 数学合法性

```cpp
bool IsMathematicallyValidBindingFrame(const FTransform& T)
{
    // 只查真正的非法数值 / 旋转 / Scale，不比较 Identity。
    return T.IsValid()
        && T.GetScale3D().X > 0.0f
        && T.GetScale3D().Y > 0.0f
        && T.GetScale3D().Z > 0.0f;
}
```

`T.IsValid()` 在本机 UE 5.6 中已覆盖 NaN/Inf 与 Rotation 归一化；Scale 由本函数补查。

### 4.6 Identity 最终语义

```text
FTransform::Identity 是合法 Transform，不承载“无效 / 未计算 / 未就绪”含义。
Transform 是否可消费只由 Binding.State == Ready 决定。
非 Ready 状态把缓存重置为 Identity 只是清空旧数据。
```

原 `Compute*` 函数保留，只删除输入 `Equals(FTransform::Identity)` 特判；保留原 `IsValid()` 防御。Rebuild 在调用 `Compute*` 前后使用 `IsMathematicallyValidBindingFrame` 预检和后检，因此不需要 `TryCompute` 层。

## 5. 运行时规则

### 5.1 每帧主循环

```text
UpdateShooterAnimationData(DeltaSeconds):
    Character = Cast<AShooterCharacter>(GetOwningActor())
    if (!Character):
        ResetBindingsAndOutputs()
        return

    AimSignature       = GatherAimSignature(Character)
    LeftHandSignature  = GatherLeftHandSignature(Character)

    UpdateAimBinding(Character, AimSignature, DeltaSeconds)
    UpdateLeftHandBinding(Character, LeftHandSignature, DeltaSeconds)

    // 之后才计算 AimDirectionWorld / AimTargetWorld / Enabled
```

单个 Binding 的更新：

```text
UpdateBinding(B, S, DeltaSeconds):
    PreviousState   = B.State
    bSignatureChanged = (S != B.StoredSignature)

    if (bSignatureChanged):
        B.StoredSignature = S
        RebuildBinding(B)

    else if (B.State == Pending):
        RebuildBinding(B)                 // 唯一签名不变仍每帧重建的状态

    if (bSignatureChanged || B.State != PreviousState):
        ResetBindingTimers(B)

    TickBindingTimers(B, DeltaSeconds)
```

- `Unbound` / `Unsupported` / `Ready`：签名不变时什么都不做。
- `WaitingForAttach`：不进入 Rebuild，只累加等待计时。
- `Pending`：每帧 Rebuild；1.0 秒超时后仍每帧 Rebuild，不降频。

### 5.2 Aim Binding Rebuild 判定链

每层只决定一次，命中即退出：

```text
!IsValid(Character) || !IsValid(CharacterMesh)
    → Unbound

CharacterMesh->GetSkeletalMeshAsset() == nullptr
    → Unsupported(MissingCharacterMeshAsset)

!IsValid(Weapon) || Weapon->GetOwner() != Character
    → Unbound

WeaponMesh == nullptr
    → Unsupported(MissingWeaponMeshComponent)

WeaponMesh->GetSkeletalMeshAsset() == nullptr
    → Unsupported(MissingWeaponMeshAsset)

WeaponMesh->GetAttachParent() != CharacterMesh
    → WaitingForAttach

WeaponMesh->GetAttachSocketName() == NAME_None
|| !CharacterMesh->DoesSocketExist(WeaponMesh->GetAttachSocketName())
    → Unsupported(MissingWeaponAttachSocket)

!CharacterMesh->DoesSocketExist(HandSocketName)
    → Unsupported(MissingRightHand)

!Weapon->HasThirdPersonMuzzleSocket()
    → Unsupported(MissingMuzzle)

读取 HandWorld、MuzzleWorld
任一输入不满足 IsMathematicallyValidBindingFrame
    → Pending(InvalidHandToMuzzle)

HandToMuzzle = ComputeHandToMuzzleTransform(HandWorld, MuzzleWorld)
!IsMathematicallyValidBindingFrame(HandToMuzzle)
    → Pending(InvalidHandToMuzzle)

    → Ready      // 即使 HandToMuzzle == Identity
```

### 5.3 LeftHand Binding Rebuild 判定链

前七层与 Aim 完全相同；从角色 / 武器能力检查开始：

```text
!CharacterMesh->DoesSocketExist(HandSocketName)
    → Unsupported(MissingRightHand)

!CharacterMesh->DoesSocketExist(LeftHandBoneName)
    → Unsupported(MissingLeftHand)

!CharacterMesh->DoesSocketExist(HandGripSocketName)
    → Unsupported(MissingCharacterHandGrip)

Weapon->GetThirdPersonLeftHandGripSocketName() == NAME_None
    → Unsupported(WeaponLeftHandGripNotConfigured)

!Weapon->HasThirdPersonLeftHandGripSocket()
    → Unsupported(WeaponLeftHandGripSocketMissing)

读取 RightHandWorld、GripWorld
任一输入不满足 IsMathematicallyValidBindingFrame
    → Pending(InvalidWeaponGripInRightHandSpace)

WeaponGripInRightHandSpace =
    ComputeLeftHandGripInRightHandSpace(RightHandWorld, GripWorld)
!IsMathematicallyValidBindingFrame(WeaponGripInRightHandSpace)
    → Pending(InvalidWeaponGripInRightHandSpace)

读取 LeftHandWorld、HandGripWorld
任一输入不满足 IsMathematicallyValidBindingFrame
    → Pending(InvalidHandGripInLeftHandSpace)

HandGripInLeftHandSpace =
    ComputeHandGripInLeftHandSpace(LeftHandWorld, HandGripWorld)
!IsMathematicallyValidBindingFrame(HandGripInLeftHandSpace)
    → Pending(InvalidHandGripInLeftHandSpace)

    → Ready      // 即使任一结果为 Identity
```

每个 `SetState` 同时把缓存 Transform 重置为 Identity；只有 `Ready` 保留计算结果。

### 5.4 计时与日志

| 状态 | 阈值 | 行为 | 日志 |
| --- | --- | --- | --- |
| `Pending` | 1.0 秒 | 继续每帧 Rebuild，不降频 | `LogShootGame Error` 一次，全部构建 |
| `WaitingForAttach` | 3.0 秒（`WaitingForAttachWarningDelaySeconds`，Clamp 3.0～5.0） | 继续等待 | `LogShootGame Warning` 一次，`#if !UE_BUILD_SHIPPING` |

- 计时只累加 `Max(DeltaSeconds, 0.0f)`。
- 每个 Binding 独立计时；两个 Binding 同时等待时允许各出一条 Warning。
- 以下情况清零计时与报告标记：
  - 签名变化触发 Rebuild；
  - Binding 状态发生变化；
  - 离开 Pending / WaitingForAttach 后再次进入。
- 同签名、同状态持续 Pending 时，计时不清零，继续累加。
- 异常类 `Unsupported`（例如 `WeaponLeftHandGripSocketMissing`、`MissingMuzzle`）在进入该状态时 Warning 一次；`WeaponLeftHandGripNotConfigured` 属于预期能力缺失，不 Warning。

日志样例：

```text
LogShootGame: Warning: [IKBinding][Aim] WaitingForAttach exceeded 3.00s
    Character=... CharacterMesh=... Weapon=... WeaponMesh=...
    AttachParent=None AttachSocketName=None

LogShootGame: Error: [IKBinding][Aim] Pending exceeded 1.00s
    Reason=InvalidHandToMuzzle Elapsed=1.016s
    Weapon=... HandSocket=hand_r MuzzleSocket=Muzzle
```

### 5.5 Enabled 与帧级输入分离

```text
bAimIKEnabled =
    AimBinding.State == Ready
    && IsValidAimDirection(AimDirectionWorld)
    && (Character->IsLocallyControlled() || bAimTargetWorldValid)

bLeftHandIKEnabled =
    LeftHandBinding.State == Ready
```

- `AimDirectionWorld == Zero`、AimTarget 本帧失效、死亡、Montage、快速转身都不得改变 Binding 状态。
- 远端 AimDirection / AimTarget 计算只有在 `AimBinding.State == Ready` 时才允许使用 `MuzzleWorld`；其余状态按“无 Muzzle”处理，避免未 Attach 武器参与目标计算。
- 以后死亡 / Montage / 收枪等条件作为额外 `GameplayAllowsIK` 加在 Enabled 表达式尾部，不进入状态机。

### 5.6 调试与手动重建

- AnimInstance 可暴露 `BlueprintReadOnly` 的 `AimBindingState`、`AimBindingFailureReason`、`LeftHandBindingState`、`LeftHandBindingFailureReason` 调试副本。
- 提供 `ForceRebuildIKBindings()`：清空两个 StoredSignature 并立即 Rebuild，用于编辑器资产重导入、测试或 Debug 时唤醒 `Unsupported`；运行时正式逻辑不依赖它。

## 6. 代码改动清单

### 6.1 `ShooterThirdPersonAnimInstance.h`

- 新增 `EIKBindingState`、`EIKBindingFailureReason`。
- 新增公共签名、Aim / LeftHand 签名和两个 Binding 结构体。
- 新增 `IsMathematicallyValidBindingFrame` 静态纯函数。
- 新增 `WaitingForAttachWarningDelaySeconds`（`EditAnywhere`，Clamp 3.0～5.0，默认 3.0）。
- 新增 `ForceRebuildIKBindings()`。
- 删除旧缓存字段：
  - `CachedWeapon`、`bCachedWeaponThirdPersonMeshAttached`；
  - `CachedLeftHandGripWeapon`、`bCachedLeftHandGripThirdPersonMeshAttached`、`bLeftHandGripCacheDirty`；
  - `bCachedThirdPersonHandSocketExists`、`bCachedLeftHandBoneExists`、`bCachedHandGripSocketExists`。
- 保留对 AnimGraph 公开的属性：`HandToMuzzle`、`LeftHandGripInRightHandSpace`、`HandGripInLeftHandSpace`、`bAimIKEnabled`、`bLeftHandIKEnabled`、`AimDirectionWorld`、`AimTargetWorld`、`bAimTargetWorldValid`。
- 保留 `ComputeHandToMuzzleTransform`、`ComputeLeftHandGripInRightHandSpace`、`ComputeHandGripInLeftHandSpace`、`GetHandWorldTransform`；只删除 Identity 特判并同步更新中文注释。

### 6.2 `ShooterThirdPersonAnimInstance.cpp`

- 重写 `UpdateShooterAnimationData`：先 Binding，后 Aim 输入。
- 实现 `GatherAimSignature`、`GatherLeftHandSignature`、`UpdateAimBinding`、`UpdateLeftHandBinding`、`RebuildAimBinding`、`RebuildLeftHandBinding`、`ResetBindingTimers`、`TickBindingTimers`、`ResetBindingsAndOutputs`。
- 远端 Muzzle 使用改为仅 `AimBinding.State == Ready`。
- `Compute*` 函数删除输入 `Equals(FTransform::Identity)` 条件，保留 `IsValid()` 防御；函数注释说明“调用方负责前置能力和数学合法性判定”。
- `IsAimIKEnabledForState` / `IsLeftHandIKEnabledForState` / `ShouldRefreshLeftHandGripCache` 在阶段 0 确认无 AnimBP 引用后退役；如有蓝图引用，保留为无 Identity 判定的兼容版本并记录。

### 6.3 `ShooterWeapon.h/.cpp`

- 新增 `FName GetMuzzleSocketName() const`。
- `GetThirdPersonMuzzleWorldTransform()` 缺失 Mesh / Socket 时改回 `FTransform::Identity`，与 `GetThirdPersonLeftHandGripWorldTransform()` 一致；Rebuild 只在 `HasThirdPersonMuzzleSocket()` 为真后调用。

### 6.4 `ShooterLeftHandIKMath.h/.cpp`

- `IsUsableFrame` 改为只做数学合法性：`IsValid()` 且 Scale 各分量大于 0，删除 `!Equals(FTransform::Identity)`。
- 相关中文注释同步更新。

### 6.5 测试

- 新增 `Source/ShootGame/Tests/Animation/ShooterIKBindingAutomationTests.cpp`，测试名前缀 `ShootGame.Aim.Binding*`。
- 迁移 `ShooterAimPresentationAutomationTests.cpp` 与 `ShooterLeftHandIKAutomationTests.cpp` 中所有“Identity 等于失败”的旧断言。
- 新增文件后执行一次 `Scripts/Development/RefreshVisualStudioFiles.ps1`。

## 7. 分阶段执行

### 阶段 0：只读审计与基线

Agent：

- 使用 CodeGraph 核对 AnimInstance、Weapon、LeftHandIKMath 的当前调用链。
- 使用只读 MCP 核对 `ABP_TP_*` 是否只引用 `bAimIKEnabled`、`bLeftHandIKEnabled`、两个握把 Transform 和 Aim 输入属性；确认没有 AnimBP 节点引用 `IsAimIKEnabledForState`、`IsLeftHandIKEnabledForState`、`ShouldRefreshLeftHandGripCache`。
- 记录当前 `ShootGame.Aim*` 测试数量与通过基线。

阶段门：

- 确认无需修改任何 `.uasset`；
- 确认退役三个纯判定函数不会破坏 AnimBP。

### 阶段 1：状态机类型与纯逻辑

Agent：

- 落地第 4 节全部枚举、签名、Binding 结构体和数学合法性函数。
- 实现 `Rebuild` 判定链的纯逻辑入口，保证每个分支可用 Automation 直接验证。
- 新增首批 `ShootGame.Aim.Binding*` 纯状态测试。
- 编译并运行 `-TestFilter ShootGame.Aim.Binding`。

阶段门：

- Editor Build 通过；
- 新测试全部通过；
- 尚未改动 AnimInstance 更新路径。

### 阶段 2：AnimInstance / Weapon / LeftHandIKMath 迁移

Agent：

- 按第 6 节迁移 AnimInstance 更新路径。
- 完成 Weapon getter、Muzzle 回退修正和 `IsUsableFrame` Identity 清理。
- 迁移旧测试中的 Identity 断言。
- 运行 `BuildEditor.ps1` 和 `RunAutomation.ps1 -TestFilter ShootGame`。

阶段门：

- 全部 `ShootGame` Automation 通过；
- Rifle / Pistol 的 Binding 状态机逻辑测试覆盖第 8 节场景；
- 不产生 `.uasset` 改动。

### 阶段 3：日志与诊断验收

Agent：

- 用注入 DeltaSeconds 的测试验证：
  - Pending 0.5 秒无 Error，1.0 秒 Error 一次，1.5 秒无第二次，且每帧仍 Rebuild；
  - WaitingForAttach 2.9 秒无 Warning，3.0 秒 Warning 一次，之后继续等待。
- 用测试或日志断点确认 `Ready` / `Unsupported` 签名不变时没有 Socket / Transform 查询。
- 运行 `RunAutomation.ps1 -TestFilter ShootGame`、`RunStandaloneSmoke.ps1` 和 `RunNetworkSession.ps1`。

阶段门：

- 日志频率与级别符合第 5.4 节；
- 网络会话无回归。

### 阶段 4：PIE 视觉回归

人工：

- Rifle：装备、瞄准、切枪、快速转身、开火 Montage 中 Aim IK 与 LeftHand IK 正常开关。
- Pistol：Aim `Ready`、LeftHand `Unsupported(WeaponLeftHandGripNotConfigured)`，无 Warning / Error。
- 死亡 / 换枪 / 断线重连后 Binding 能按签名变化恢复。
- 通过日志或调试器确认 State / Reason 符合预期；不新增 AnimBP 调试连线。

阶段门：

- 人工视觉确认通过；
- 无新增 `.uasset` 保存操作。

### 阶段 5：清理与归档

- 删除临时日志与实验代码，保留第 5.6 节调试入口。
- 更新 `AGENTS.md` 文档入口、本计划状态和相关开发记录。
- 将计划移入已完成归档或按项目规范标记完成。

## 8. 自动验证矩阵

至少覆盖：

```text
Aim / LeftHand：无 Character、无 Weapon → Unbound
Weapon 先到、Mesh 未 Attach → WaitingForAttach
Attach 完成且计算合法 → Ready
Attach 完成但输入或结果数学非法 → Pending
Pending 0.5s 无 Error；1.0s Error 一次；1.5s 无第二次；仍每帧 Rebuild
Pending 恢复合法 → Ready，计时清零
WaitingForAttach 2.9s 无 Warning；3.0s Warning 一次；3.5s 无第二次
Ready 时 Detach → WaitingForAttach，Transform 清空
重新 Attach → 完整 Rebuild
同组件换 SkeletalMesh 资产 → Rebuild
同 Parent 换 AttachSocketName → Rebuild
WeaponOwner 从 null 变为 Character → Rebuild / Unbound → 后续状态
Pistol：LeftHand = Unsupported(WeaponLeftHandGripNotConfigured)，无 Warning
Rifle：LeftHand = Ready（握把、骨骼、Socket 齐全）
缺 Muzzle：Aim = Unsupported(MissingMuzzle)，LeftHand 不受影响
Unsupported 签名不变连续 N 帧：零 DoesSocketExist / GetSocketTransform 调用
Compute 输入 / 输出恰好为 Identity：成功并允许 Ready
AimDirection == Zero / AimTarget 失效：Binding 状态不变，仅 bAimIKEnabled = false
```

## 9. Agent 与人工边界

### 9.1 Agent 自主完成

- CodeGraph、只读 MCP、官方资料和本地 UE 5.6 源码核对。
- 全部 C++、状态机、Weapon API、数学层清理、Automation 测试和日志实现。
- Build、Automation、Standalone 冒烟和网络会话验证。
- 每个提交前的中文开发记录与暂存审计。

### 9.2 人工完成

- PIE 中 Rifle / Pistol / 切枪 / 死亡 / 重连的第三人称视觉回归。
- 确认 Warning / Error 的观感与频率可接受。
- 决定是否接受极端场景下的日志输出。

### 9.3 强制暂停条件

- 阶段 0 发现 AnimBP 引用了将被退役的纯函数，且必须修改 `.uasset`。
- 人工视觉回归不通过，需要判断是否属于本状态机引入的回归。
- 蓝图存在未保存修改，自动操作可能覆盖人工工作。
- 状态 / Reason 存在多个语义解释，需要人工拍板。

## 10. 提交拆分

### 提交 1：第三人称 IK Binding 状态机迁移

```text
ShooterThirdPersonAnimInstance.h/.cpp
ShooterWeapon.h/.cpp
ShooterLeftHandIKMath.h/.cpp
新增 ShooterIKBindingAutomationTests.cpp
迁移后的 Aim / LeftHand Automation Tests
中文开发记录
```

不含 `.uasset`；编译、全量 Automation、Standalone 冒烟和网络会话通过后才提交。

### 提交 2：执行计划归档与文档导航

```text
本执行计划状态更新
AGENTS.md 文档入口更新
中文开发记录
```

## 11. 完成定义

只有同时满足以下条件，本计划才可归档：

- 两个 Binding 在所有依赖变化下按第 5 节状态机收敛，无旧 bool 缓存残留。
- `Identity` 不再被任何 IK 判定当作失败；Transform 消费只由 `State == Ready` 决定。
- `Ready` / `Unsupported` 签名不变时零 Socket 查询、零 Transform 重算。
- Pending 1.0 秒 Error 一次后继续每帧重试；WaitingForAttach 3.0 秒 Warning 一次后继续等待。
- Rifle Aim / LeftHand 为 `Ready`，Pistol Aim 为 `Ready` 且 LeftHand 为预期 `Unsupported`，无错误日志。
- 原有 AnimGraph 输出兼容，AnimBP 不需要保存任何 `.uasset`。
- Editor Build、`ShootGame` Automation、Standalone 冒烟、网络会话和人工 PIE 视觉回归全部通过。
- 开发记录、文档导航和计划状态完整。
