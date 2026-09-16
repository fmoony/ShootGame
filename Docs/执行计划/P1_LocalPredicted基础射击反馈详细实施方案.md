# P1 Local Predicted 基础射击反馈详细实施方案

- 日期：2026-09-14
- 上游计划：[P1 Local Predicted 基础射击反馈执行计划](P1_LocalPredicted基础射击反馈执行计划.md)
- 基线提交：`329444d`（玩法：收口武器开火与双向切枪）
- 核对引擎：`E:\Unreal_Engine\UE_5.6`，版本 `5.6.1-44394996`
- 适用阶段：P1-0 ~ P1-D

## 0. 文档定位

上游计划定义了 P1 的目标、非目标、职责边界与子阶段顺序。
本文在其基础上补齐实施所需的全部具体内容：现状证据、引擎行为核验、
冻结 API 签名、逐文件改动点、测试破坏面、可观测性、验证命令与回退判据。

本文不改变上游计划的范围与验收标准，只修正按上游表述会导致实现错误的关键细节。

```text
上游计划（范围与验收）
→ 本文（具体实施方案）
→ 每个子阶段一份开发记录 + 一个独立提交
```

---

## 1. 现状核对

### 1.1 唯一玩家开火入口链

```text
IA_Fire Started
→ AShooterCharacter::DoStartFiring()                    ShooterCharacter.cpp:534
→ UShooterAbilitySystemComponent::AbilityInputTagPressed(Input.Fire)
→ TryActivateAbility(Spec->Handle, /*bAllowRemoteActivation*/ true)
→ UShooterGameplayAbility_Fire（InstancedPerActor + ServerOnly）
→ AShooterWeapon::StartFiring() → Fire() → ExecuteFireAtTarget()
   ├─ FireProjectile()                  ShooterWeapon.cpp:663   仅服务器
   ├─ WeaponOwner->PlayFiringMontage()  → MulticastPlayFiringMontage
   ├─ MulticastPlayFiringFX()           不可靠 Multicast
   └─ WeaponOwner->AddWeaponRecoil()    直调，不经网络
```

- ASC 位于 `AShooterPlayerState`，使用 `Mixed` 复制模式（`ShooterPlayerState.cpp:18-25`）。
- Ability 在 `PostInitializeComponents` 一次性授予并复制给拥有者连接
  （`ShooterPlayerState.cpp:82-100`）；重生只更新 Avatar，不重复授予。
- 取消链统一走 `CancelAbilitiesByTag(Input.Fire)`：

```text
GA_Reload   ShooterGameplayAbility_Reload.cpp:157    （服务器执行）
GA_Equip    ShooterGameplayAbility_Equip.cpp:177     （服务器执行）
Character   ShooterCharacter.cpp:510 死亡 / EndPlay
NPC         ShooterNPC.cpp:372       死亡
```

- `AShooterCharacter::EndPlay` 只在 `HasAuthority()` 下取消开火 Ability
  （`ShooterCharacter.cpp:221-226`）。
- `AShooterNPC::OnSemiWeaponRefire()` 第 296 行直接调用 `Weapon->StartFiring()`，
  是 NPC 半自动续射的第二条权威开火入口，不经过 GA_Fire。

### 1.2 表现资产与射速来源

- `FiringMontage` / `MuzzleFlash` / `FireSound` / `FiringRecoil` / `bFullAuto` /
  `RefireRate` 均为 `EditAnywhere` 且**不复制**。
- 客户端通过 `OnRep_WeaponId() → ApplyWeaponRow()` 从
  `UShooterWeaponRuntimeSubsystem` 启动快照恢复这些值
  （`ShooterWeapon.cpp:153-180`、`230-276`）。
- 因此本地节拍可以安全读取 `bFullAuto` / `RefireRate` / 表现资产，不需要新增复制字段。
- `MagazineAmmo` / `ReserveAmmo` 为 `COND_OwnerOnly` 复制（`ShooterWeapon.cpp:454-455`）。
- `bIsFiring` / `TimeOfLastShot` / `RefireTimer` 不复制，只在权威端有意义。

### 1.3 现有测试与观测面

- `ShootGame.Ability.Fire.*` 共 16 项，全部是 CDO + 反射断言，不构造 World / ASC / Weapon。
- 其中 7 项依赖 `NetExecutionPolicy == ServerOnly`，破坏面见第 7 节。
- `ShootGame.Ability.Fire.Cancel.*` 7 项复用同一组 CDO / 接口契约辅助函数，且零
  `NetExecutionPolicy` 引用，与策略切换无关；其中 Reject.Dead 只调用公共契约，
  其余测试还调用 WeaponHolder 接口契约，不能描述为七个函数体完全相同。
- 真实开火行为只由 `AShooterNetworkTestCoordinator` 覆盖，该文件不是 Automation 测试，
  且**未被 `WITH_DEV_AUTOMATION_TESTS` 包裹**；只有生成点
  `ShooterGameMode.cpp:40-61` 有守卫。
- 协调器结果只有两个出口：`FailTest()` 输出 `AUTOMATION_TEST_FAILURE: <reason>`，
  以及 `ShooterNetworkTestCoordinator.cpp:2634-2659` 的约 50 项巨型合取输出一行
  `AUTOMATION_TEST_CLIENT_SUCCESS ...`。
- 远端角色的弹药观测目前只断言 `GetBulletCount() == 0`（`.cpp:3241-3252`），
  因为弹药是 `COND_OwnerOnly`。

---

## 2. 对原执行计划的关键修正

上游计划方向正确，以下九处须在下笔改代码前修正，否则会直接触发上游第 13 节的回退条件。

### 修正 1：监听主机走 Authority 分支，不是 Predicting

上游 6.1 节要求用 `IsPredictingClient / IsLocallyControlled / HasAuthority` 三分流。

引擎核验显示：`InternalTryActivateAbility` 的分支条件是
`policy == LocalOnly || NetMode == ROLE_Authority`（`AbilitySystemComponent_Abilities.cpp:1851`），
LocalPredicted 专属分支在 `:1904`，只有纯客户端可达。

因此监听主机与 Standalone 的 `ActivationMode == Authority`，
`IsPredictingClient()` 恒为 false（`GameplayAbility.cpp:1918-1934`）。

若按“预测客户端”分支实现，监听主机将完全没有第一人称反馈，
直接违反上游 14 节的“Listen Host 不双播但必须有本地反馈”要求。

### 修正 2：`IsLocallyControlled()` 不能识别拥有者本地视图

NPC 在 Dedicated Server 上 `IsLocallyControlled()` 返回 **true**：
`FGameplayAbilityActorInfo::IsLocallyControlled()` 落到
`APawn::IsLocallyControlled()`，AIController 在服务器上
`LocalRole == ROLE_Authority` 且 `RemoteRole != ROLE_AutonomousProxy`，
`AController::IsLocalController()` 返回 true（`Controller.cpp:86-110`）。

必须改用 `FGameplayAbilityActorInfo::IsLocallyControlledPlayer()`
（`GameplayAbilityTypes.h:183`）。该函数要求 `PlayerController` 有效且本地控制，
NPC 的 `PlayerController` 在 `InitFromActor` 中解析为 null，因此恒为 false。

### 修正 3：Host / Standalone 会 Recoil 双次施加

`ExecuteFireAtTarget` 无条件调用 `WeaponOwner->AddWeaponRecoil(FiringRecoil)`
（`ShooterWeapon.cpp:659`）。加入拥有者本地表现路径后，
监听主机与 Standalone 会在本地路径与权威路径各施加一次。

### 修正 4：Multicast 开火音效对拥有者重复播放

`MulticastPlayFiringFX` 的音效经 `SpawnSoundAttached(FireSound, RootComponent, ...)`
播放（`ShooterWeapon.cpp:739-740`），拥有者能听到。
本地路径补充音效后必须抑制这一路。
若初始预测因迟到的本地阻塞 Tag 没有播放，不能靠恢复 Owner Multicast 兜底；
应由 GA_Fire 的服务器激活确认回调补播一次，保持拥有者第一人称表现仍只有一个入口。

### 修正 5：第一人称枪口不是“跳过”，而是“改为第三人称”

上游 6.3 节要求 Multicast 跳过第一人称枪口。
由于 `FirstPersonMesh->bOnlyOwnerSee = true` 而
`ThirdPersonMesh->bOwnerNoSee = true`（`ShooterWeapon.cpp:57`、`:65`），
正确做法是 Multicast 的枪口恒挂第三人称网格，第一人称枪口只由本地路径提供。

### 修正 6：取消链不需要新增代码

`FGameplayAbilityActivationInfo::SetPredicting()` 已把
`bCanBeEndedByOtherInstance` 置真（`GameplayAbilityTypes.cpp:161-169`），
`SetActivationConfirmed()` 同样置真（`:176-181`）。

服务器的 `ReplicateEndOrCancelAbility` 对非本地控制者下发
`ClientCancelAbility`（`AbilitySystemComponent_Abilities.cpp:2104-2117`），
客户端实例在 `RemoteEndOrCancelAbility` 中进入 `EndAbility`（`:2162-2179`）。

因此切枪 / Reload / Equip / Death / Disconnect 的服务器取消会收敛到客户端预测实例。
P1 只需要保证 `EndAbility` 幂等清理。

本地 UE 5.6.1 的 `UGameplayAbility` 构造函数实际把
`bServerRespectsRemoteAbilityCancellation` 默认设为 true。为了继续由服务器决定权威 Ability
何时结束，GA_Fire 必须在构造函数中将它显式设为 false。客户端输入释放仍由现有可靠
`ServerSetInputReleased` 通知服务器，不新增 StopFire RPC。

### 修正 7：`State.Firing` 的观察端集合保持不变

ServerOnly 路径下 `ActivationOwnedTags` 走 `AddReplicatedLooseGameplayTags`
（`COND_None`，`AbilitySystemComponent.cpp:1642`）。

LocalPredicted 路径下改走本地 `AddLooseGameplayTags` 加
`AddMinimalReplicationGameplayTags`（`COND_SkipOwner`，
`AbilitySystemComponent.cpp:1657`；分支见 `GameplayAbility.cpp:963-975`）。

两者合起来覆盖同样的端：拥有端本地挂载，远端经最小复制接收。
因此 `ShooterAnimInstanceBase.cpp:64` 的 `bIsFiring` 在远端不会退化。

### 修正 8：测试破坏面精确为 2 处断言 / 7 个测试

详见第 7 节。其余 9 个 Fire 测试与策略无关，不得顺手动。

### 修正 9：P1-0 的门槛口径

上游 8 节 P1-0 要求证明“玩家没有旧 Fire RPC 或直接 Weapon 开火调用者”。
实际存在 `AShooterNPC::OnSemiWeaponRefire()` 的 `Weapon->StartFiring()`
（`ShooterNPC.cpp:296`），这是 NPC 专用权威路径。
P1-0 证据须按“玩家无第二入口、NPC 仅服务器路径”的口径记录，
并冻结该路径不获得任何本地预测表现。

### 修正 10：EndPlay 必须先取消预测实例，再清 ActorInfo

`AShooterCharacter::EndPlay` 当前先调用 `ClearActorInfo()`，再执行服务器取消。
UE 5.6.1 的 `ClearActorInfo()` 只清空 Owner / Avatar / PlayerController 等引用，不会取消 Ability。

P1 必须在 ASC 仍确认本 Character 是当前 Avatar 时先完成取消：

```text
服务器 Character：取消 Fire / Reload / Equip
拥有者本地 Character：取消预测 Fire
→ 清理 PredictedFeedbackTimer
→ 最后 ClearActorInfo
```

### 修正 11：半自动首次射击不能用零时间作冷却判据

Weapon 取用和归还时会把 `TimeOfLastShot` 重置为 `0.0f`。若直接用
`WorldTime - TimeOfLastShot >= RefireRate`，世界启动后的第一枪可能被误判为仍在冷却。

半自动资格查询应直接读取权威 `RefireTimer` 是否活动：首次取用时 Timer 未激活，可以开火；
开火后 Timer 在冷却期活动，应拒绝再次激活。全自动不走该查询。

### 修正 12：网络时序只能在同一时钟域内比较

Dedicated Client 与 Server 的 `UWorld::GetTimeSeconds()` 不共享起点，不能直接比较客户端
`FirstWorldTime` 与服务器 `WorldTime`。P1 的响应性证据改为在 Owner 客户端同一时钟上比较：

```text
OwnerPredictedLocalTime < OwnerAuthorityConfirmationReceivedLocalTime
```

服务器另行证明 Ammo / Projectile / AuthorityShotCount 只有一份。PredictionKey 只标识一次
Ability 激活或 Burst；权威每发射击使用独立 `AuthorityShotOrdinal`，不把 PredictionKey
持久写进池化 Weapon。

---

## 3. UE 5.6 依据核对

### 3.1 外部官方依据

- [EGameplayAbilityNetExecutionPolicy::Type（UE 5.6 API）](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/EGameplayAbilityNetExecutionPoli-?application_version=5.6)
- [Using Gameplay Abilities in Unreal Engine（5.6）](https://dev.epicgames.com/documentation/unreal-engine/using-gameplay-abilities-in-unreal-engine?application_version=5.6)
- [FPredictionKey（UE 5.6 API）](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/FPredictionKey?application_version=5.6)
- [Switch on EGameplayCueNotify_LocallyControlledPolicy（UE 5.6 API）](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Utilities/FlowControl/Switch/SwitchonEGamepla-_6?application_version=5.6)
- [Abilities in Lyra in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/abilities-in-lyra-in-unreal-engine)

### 3.2 GameplayCue 通道的官方原语（补做检索结论）

这是上游 15 节复盘“是否迁移 GameplayCue”的关键依据。

- 官方 5.6 API 页确认存在 GameplayCue Notify 的
  `EGameplayCueNotify_LocallyControlledPolicy` 分支节点。
- 本地 5.6.1 源码给出精确定义（`GameplayCueNotifyTypes.h:94-110`）：

```text
Always     无论是否本地控制都生成
LocalOnly  仅当来源 Actor 是本地控制时生成
NotLocal   仅当来源 Actor 不是本地控制时生成
```

- 配套还有 `EGameplayCueNotify_LocallyControlledSource`，
  可取 `TargetActor` 作为“是否本地控制”的判定来源
  （`GameplayCueNotifyTypes.h:89-91`、`:127-133`）。

结论：GAS 已经把“拥有者本地表现 vs 远端确认表现”的分工做成了**资产级配置原语**。
P1 用 `bOwnerLocalView` 加 Multicast 抑制手工实现的正是同一件事。
这既是 P1 结束后迁移 GameplayCue 的最强论据，
也是 P1 不需要自建表现框架、只需最小改动的理由。

### 3.3 Lyra 的差异（补做检索结论）

- 官方 Lyra 文档页确认 Lyra 的 Ability 以输入 Tag 驱动、支持本地预测。
- Lyra 的武器开火 Ability 为 `ULyraGameplayAbility_RangedWeapon`，
  使用 scoped prediction window 预测**瞄准目标数据**
  （来源为 Epic 官方论坛帖，属官方可及但非文档级证据）：

```text
https://forums.unrealengine.com/t/useless-scoped-prediction-window-in-ulyragameplayability-rangedweapon-startrangedweapontargeting/2709747
```

- Lyra 的开火表现（动画、枪口、音效）由 GameplayCue 与武器实例驱动，
  而非 NetMulticast RPC。此项**未能从官方文档逐字确认**，
  标记为 OFFICIAL-ADJACENT，置信度中等。

与项目的关键差异：

```text
Lyra：LocalPredicted 开火 Ability + 预测 TargetData + GameplayCue 表现
P1  ：LocalPredicted 开火 Ability + 只预测表现 + 保留 Multicast
```

P1 的预测深度**有意窄于** Lyra：项目当前没有 TargetData 与命中预测管线，
也没有 GameplayCue 资产管线，因此只预测表现是范围收敛，不是实现落后。

### 3.4 本地 5.6.1 源码核验结论

核验位置：

```text
.../GameplayAbilities/Private/AbilitySystemComponent_Abilities.cpp
.../GameplayAbilities/Private/Abilities/GameplayAbility.cpp
.../GameplayAbilities/Private/GameplayAbilityTypes.cpp
.../GameplayAbilities/Private/AbilitySystemComponent.cpp
.../GameplayAbilities/Public/Abilities/GameplayAbilityTypes.h
.../GameplayAbilities/Public/Abilities/GameplayAbilitySpec.h
.../GameplayAbilities/Public/GameplayCueNotifyTypes.h
```

激活与分流：

- 激活分支由 `NetMode` 决定，不由 policy 决定：
  `AbilitySystemComponent_Abilities.cpp:1851`。
- 监听主机 `ActivationMode == Authority`：
  `GameplayAbilityTypes.cpp:153-157` 加 `GameplayAbility.cpp:1964-1967`。
- `IsPredictingClient()` 对 Host 恒为 false：`GameplayAbility.cpp:1918-1934`。
- `IsLocallyControlled()` 对服务器 NPC 为真：
  `GameplayAbilityTypes.cpp:107-127` 加 `Controller.cpp:86-110`。
- `IsLocallyControlledPlayer()` 是正确判据：
  `GameplayAbilityTypes.cpp:129-135`、`GameplayAbilityTypes.h:183`。

结束与取消：

- 客户端预测实例可被服务器结束：`GameplayAbilityTypes.cpp:161-169`、
  `AbilitySystemComponent_Abilities.cpp:2162-2179`。
- 服务器实例不受客户端结束命令影响：
  `AbilitySystemComponent_Abilities.cpp:1886`。
- 服务器只对非本地控制者下发结束或取消：
  `AbilitySystemComponent_Abilities.cpp:2104-2117`。
- 预测被拒时引擎会结束本地实例：
  `ClientActivateAbilityFailed_Implementation` `:2251-2305`。

输入与表现：

- `ServerSetInputReleased` 是可靠 RPC：`AbilitySystemComponent.h:1650-1652`。
- 输入释放的本地实例回调链：`AbilitySystemComponent_Abilities.cpp:2832` 加
  `ShooterAbilitySystemComponent.cpp:42-47`。
- `State.Firing` 观察端集合不变：`GameplayAbility.cpp:963-975` 加
  `AbilitySystemComponent.cpp:1657`。
- 可用的 PredictionKey 访问器非弃用：`GameplayAbilitySpec.h:152`。
- GameplayCue 本地控制策略原语：`GameplayCueNotifyTypes.h:94-110`。

---

## 4. 冻结判定矩阵

用两个布尔量分流，不引入第三种模式：

```cpp
const bool bAuthoritySide  = HasAuthority(&ActivationInfo);
const bool bOwnerLocalView = ActorInfo && ActorInfo->IsLocallyControlledPlayer();
```

| 运行端 | `bAuthoritySide` | `bOwnerLocalView` | 本地表现 | 权威事务 |
|---|---|---|---|---|
| Dedicated Server（远端玩家） | true | false | 不执行 | 执行 |
| Dedicated Server（NPC） | true | false | 不执行 | 执行 |
| 预测客户端 | false | true | 执行 | 不执行 |
| Listen Host（本人） | true | true | 执行 | 执行 |
| Standalone | true | true | 执行 | 执行 |

每个场景的可见反馈恰好一份：

```text
Owner 第一人称 Montage / 枪口 Niagara / 本地音效 / Recoil
  ← 只由 bOwnerLocalView 的本地路径播一次

Remote 第三人称 Montage / 枪口 Niagara / 音效
  ← 只由服务器 Multicast 播一次
```

`ActivateAbility` 内部顺序固定为：

```text
1. 解析当前 Weapon；Authority 侧做 Activate 防御复核，失效时按 Cancelled 结束
2. bOwnerLocalView → 播一次本地表现；Weapon.IsFullAuto() 时启动 PredictedFeedbackTimer
3. bAuthoritySide  → CachedWeapon = Weapon；绑定 OnOutOfAmmo；Weapon->StartFiring()
4. 预测客户端收到服务器接受确认时，若本次激活尚未成功播放且当前武器仍有效，只补播一次
```

预测请求的正式 Reject 由服务器 `CanActivateAbility` 返回 false 后通过 GAS 下发；
不得在 Authority 的 `ActivateAbility` 内手工伪造 `ActivationMode::Rejected`。

Host 上三步同帧有序；Multicast 在第三步内同步执行，
因此“本机先本地、后 Multicast”是确定顺序，不依赖网络时序。

---

## 5. 冻结 API 契约

### 5.1 `UShooterGameplayAbility_Fire`

文件：`Source/ShootGame/AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h/.cpp`

新增私有状态：

```cpp
TWeakObjectPtr<AShooterWeapon> CachedWeapon;   // 双端缓存：权威端控武器，拥有端控表现
FTimerHandle PredictedFeedbackTimer;           // 归 Ability，不归 Weapon
int32 PredictedShotOrdinal = 0;                // 本次激活内的第几次本地反馈
bool bPredictedFeedbackActive = false;
```

新增私有函数：

```cpp
void StartOwnerPredictedFeedback(AShooterWeapon& Weapon);
bool TryPlayOwnerFeedback(AShooterWeapon& Weapon, const TCHAR* Marker,
    bool bConfirmedFallback = false);
void StopOwnerPredictedFeedback();
void HandlePredictedFeedbackTick();
bool IsCurrentWeaponStillValidForFeedback(AActor* AvatarActor) const;
void StopAuthorityWeapon();
void LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon,
    int32 ShotOrdinal) const;

virtual void ConfirmActivateSucceed() override;
```

GA_Fire 还保存两个仅限当前激活的布尔状态：是否已经成功播放拥有者反馈、
确认回退后的阻塞 Tag 宽限是否仍有效。宽限在迟到 Tag 首次清除时关闭，后续新阻塞态恢复正常门控。
`ActivateAbility` 复位，`EndAbility` 清理。Reject 不会进入确认回调，因此不会误补播。

构造函数唯一策略改动：

```cpp
NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
bServerRespectsRemoteAbilityCancellation = false;
```

**不修改**：

```text
InstancingPolicy                       保持 InstancedPerActor
bRetriggerInstancedAbility             保持 false
ActivationBlockedTags                  保持 State.Dead / State.Reloading / State.Equipping
ActivationOwnedTags                    保持 State.Firing
AssetTags                              保持 Input.Fire
```

### 5.2 `AShooterWeapon`

文件：`Source/ShootGame/Weapons/ShooterWeapon.h/.cpp`

新增五个公开表现或只读函数：

```cpp
/** 拥有者本地普通预测表现入口。
 *  按本 WeaponActor 的 RefireRate 限制纯表现；不写 MagazineAmmo / ReserveAmmo / TimeOfLastShot /
 *  bIsFiring / RefireTimer / Inventory / Projectile。Dedicated Server 直接返回 false。
 *  返回是否真实播放了至少一项表现。 */
bool PlayOwnerPredictedShotFeedback();

/** 服务器确认当前预测激活后的单次回退。
 *  旁路本地纯表现冷却并重新推进冷却；Reject 路径不得调用。 */
bool PlayOwnerConfirmedShotFeedback();

/** 本地预测节拍只读配置。 */
bool IsFullAuto() const { return bFullAuto; }
float GetRefireRate() const { return RefireRate; }

/** 服务器只读射速资格查询，无副作用。
 *  仅供半自动使用：RefireTimer 未激活时返回 true。
 *  全自动不调用本函数，真实补射时机仍由权威 RefireTimer 决定。 */
bool CanStartSemiAutoShotNow() const;
```

`CanStartSemiAutoShotNow()` 不读取 `TimeOfLastShot == 0.0f` 作为“从未开火”哨兵；
World 不可用或 Weapon 为全自动时返回 false。

WeaponActor 另持有一个不复制的 `OwnerFeedbackCooldownEndTime`。普通预测只有本地时间越过该值才播放；
成功播放后按 `Max(RefireRate, 0.01f)` 推进。确认回退可旁路判定，但同样推进下一次冷却。
该字段在池取用、归还和客户端 Owner 复制变化时复位，不建立 Timer，不修改权威
`TimeOfLastShot` / `RefireTimer`，也不在 GA / Character / ASC 间共享。

Weapon 新增一个私有表现判据，供 Multicast 与权威 Recoil 去重共用：

```cpp
bool HasOwnerLocalPlayerView() const;
```

其语义必须同时满足 `PawnOwner->IsPlayerControlled()` 与
`PawnOwner->IsLocallyControlled()`；不得只检查普通 `IsLocallyControlled()`，以免把服务器 NPC
误判为拥有者本地玩家。

`MulticastPlayFiringFX_Implementation` 语义变更，签名与 RPC 属性不变：

```cpp
void AShooterWeapon::MulticastPlayFiringFX_Implementation()
{
    if (IsRunningDedicatedServer())
    {
        return;
    }

    if (!MuzzleFlash && !FireSound)
    {
        return;
    }

    const bool bOwnerLocalPlayerView = HasOwnerLocalPlayerView();

    // Owner 已由本地预测承担第一人称枪口与音效；Multicast 到达时只登记确认，不再生成可见 FX。
    if (bOwnerLocalPlayerView)
    {
#if WITH_DEV_AUTOMATION_TESTS
        RecordOwnerAuthorityConfirmationForAutomationTest();
#endif
        return;
    }

    // Remote 与 NPC 使用第三人称世界网格。
    if (MuzzleFlash && ThirdPersonMesh)
    {
        UNiagaraFunctionLibrary::SpawnSystemAttached(MuzzleFlash, ThirdPersonMesh, MuzzleSocketName,
            FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
    }

    if (FireSound)
    {
        UGameplayStatics::SpawnSoundAttached(FireSound, RootComponent, NAME_None,
            FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
    }
}
```

`ExecuteFireAtTarget` 变更，让 Recoil 只有一个来源：

```cpp
FireProjectile(TargetLocation);
WeaponOwner->PlayFiringMontage(FiringMontage);
MulticastPlayFiringFX();

// 拥有者的 Recoil 由本地拥有者路径负责；权威端只对非本地控制者保留现状。
if (!HasOwnerLocalPlayerView())
{
    WeaponOwner->AddWeaponRecoil(FiringRecoil);
}
```

`PlayOwnerPredictedShotFeedback` 实现顺序：

```text
1. IsRunningDedicatedServer() 或非 HasOwnerLocalPlayerView() → false
2. 四项表现资产全空 → false
3. FiringMontage 存在时经持有者本地入口播第一人称 Montage
4. MuzzleFlash、FirstPersonMesh 与 Muzzle Socket 都有效时才生成第一人称枪口
5. FireSound 存在时挂 RootComponent，不依赖 Muzzle Socket
6. FiringRecoil 非零时经持有者本地入口施加，不依赖 Weapon Mesh
7. 返回是否真实播放了任意一项
```

各表现项独立校验；枪口配置缺失不得连带吞掉 Montage、Sound 或 Recoil。

### 5.3 `IShooterWeaponHolder`

文件：`Source/ShootGame/Weapons/Interfaces/ShooterWeaponHolder.h`

新增纯虚方法：

```cpp
/** 拥有者本地第一人称开火表现入口：只播第一人称 Montage 与本地 Recoil。
 *  与 PlayFiringMontage / AddWeaponRecoil 的区别是本入口绝不触发网络 RPC，
 *  且不修改任何 Gameplay 状态。返回是否真实播放。 */
virtual bool PlayOwnerLocalFiringFeedback(UAnimMontage* Montage, float Recoil) = 0;
```

现有实现者只有两个，必须同时实现：

- `AShooterCharacter`：仅在 `IsPlayerControlled() && IsLocallyControlled()` 时，按资产是否有效分别
  播放第一人称 Montage 与 `AddControllerPitchInput(Recoil)`；返回是否真实播放了任意一项。
- `AShooterNPC`：`return false;`，与既有 `PlayFiringMontage` 的 unused 语义一致。

### 5.4 `AShooterCharacter`

文件：`Source/ShootGame/Characters/ShooterCharacter.h/.cpp`

移除 Multicast 的第一人称分支：

```cpp
void AShooterCharacter::MulticastPlayFiringMontage_Implementation(UAnimMontage* Montage)
{
    if (!Montage)
    {
        return;
    }

    // 第三人称 mesh 在所有客户端播放（含拥有者），维持同场景第三人称一致性。
    // 拥有者的第一人称 Montage 只由 PlayOwnerLocalFiringFeedback 播放。
    if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
    {
        AnimInstance->Montage_Play(Montage);
    }
}
```

`EndPlay` 调整为先取消、后清 ActorInfo：

```cpp
// 必须在 ClearActorInfo 前判断并取消；清空 ActorInfo 本身不会结束 Ability。
if (HasAuthority())
{
    CancelFireAbility();
    CancelReloadAbility();
    CancelEquipAbility();
}
else if (IsPlayerControlled() && IsLocallyControlled())
{
    CancelFireAbility();
}

// 仅当 ASC 当前 Avatar 仍是本 Character 时再 ClearActorInfo。
```

`AddWeaponRecoil` 与 `PlayFiringMontage` 保持现状，继续服务权威路径。

### 5.5 `UShooterAbilitySystemComponent`

**生产逻辑零改动。**

可选的注释同步：`ShooterAbilitySystemComponent.cpp:40-41` 的
“ServerOnly Ability 在客户端没有活动实例”在 P1 后不再成立，
改为说明“本地预测实例由 `AbilitySpecInputReleased` 直接驱动，
同时以可靠 RPC 通知服务器”。

禁止新增第二条 StopFire RPC。

---

## 6. 子阶段实施方案

### 6.1 P1-0：ServerOnly 复盘与基线冻结

只出证据，不改策略。

- 前置证据：直接引用
  `Saved/Automation/Runs/20260914_151623/Summary.json` 与
  `ShootGame.Pickup.RespawnGate.CrossRespawnGrant`，不重跑七阶段。
- 唯一入口核验：
  `codegraph.cmd explore "GA_Fire activation path and weapon fire entry points"`。
- 权威调用者清单：见 1.1 节；`StartFiring` 只有 GA_Fire 与
  NPC 半自动续射两个调用点。
- 时序基线：现有 `GA_Fire activated:` / `GA_Fire ended:` 只证明服务器事务边界，不能记录
  客户端输入到 Owner 表现的延迟；响应性时序证据推迟到 P1-B 的同端观测点建立后采集。
- 权威计数：复用协调器 `ProjectileSpawnCount` 与
  `Weapon->GetBulletCount()`，半自动与全自动各记录一组。
- 定向测试：仅在证据不足时运行 `ShootGame.Ability.Fire.*`；
  不运行 `RunAll.ps1`。

通过条件：

```text
一次权威射击只扣 1 发并生成 1 个弹丸
玩家没有第二开火入口；NPC 只在服务器执行
当前完整回归全部 Passed
```

产出：证据写入 P1-A 的开发记录；没有文件变化时不创建空提交。

### 6.2 P1-A：抽取纯本地表现路径

目标：先建立可单测的表现 API，全量 Gameplay 行为零变化。

改动清单：

```text
1. ShooterWeaponHolder.h   新增纯虚 PlayOwnerLocalFiringFeedback
2. ShooterCharacter.h/.cpp 实现 PlayOwnerLocalFiringFeedback
3. ShooterNPC.h/.cpp       实现 PlayOwnerLocalFiringFeedback，返回 false
4. ShooterWeapon.h/.cpp    新增 PlayOwnerPredictedShotFeedback（本阶段无生产调用者）
5. ShooterWeapon.h/.cpp    挂载本地表现测试计数（见 8.1 节）
```

建议 Feature Tests：

```text
ShootGame.Ability.Fire.Prediction.LocalFeedbackCosmeticOnly
ShootGame.Ability.Fire.Prediction.LocalFeedbackStateless
```

测试不得继续使用纯 CDO 断言冒充运行时证据。P1-A 先建立最小 Editor Test World，生成 Weapon 与
本地玩家 Holder，分别配置 Montage / Muzzle / Sound / Recoil 缺失组合，并对比调用前后的
`bIsFiring / TimeOfLastShot / RefireTimer / Ammo`。Dedicated 不播放的结论移到 P1-B/D 网络场景。

通过条件：新增测试通过；`ShootGame.Ability.Fire.*` 16 项行为不变；不跑七阶段。

建议提交：`射击：分离拥有者本地开火表现路径`

### 6.3 P1-B：切换 LocalPredicted，完成半自动闭环

改动清单按依赖顺序：

```text
1. 构造函数设置 LocalPredicted 与 bServerRespectsRemoteAbilityCancellation=false
2. 新增 bAuthoritySide / bOwnerLocalView 双布尔分流
3. 删除 ActivateAbility 开头的 HasAuthority 硬拒绝（现 :109-113）
4. 新增 IsFullAuto / GetRefireRate / CanStartSemiAutoShotNow 与双分支 CanActivateAbility
5. ActivateAbility 按第 4 节固定顺序实现
6. InputReleased 改为停本地 Timer + 条件停武器 + 条件复制结束
7. EndAbility 幂等清理，权威门控 StopFiring
8. Multicast 与 ExecuteFireAtTarget 按 5.2 / 5.4 节落地
```

`CanActivateAbility` 双分支：

```text
预测客户端（!AvatarActor->HasAuthority()）
  保留现有宽松预检（ShooterGameplayAbility_Fire.cpp:80-84），
  弱网修复 310faf9 的语义必须保留；
  追加 IsLocallyControlledPlayer() 与“当前 Weapon 足以播放表现”软判据，
  软判据不参与拒绝。

服务器 / Host / Standalone
  执行 Super 与现有全部校验；
  半自动追加 Weapon->CanStartSemiAutoShotNow()；
  全自动不追加，冷却期允许激活。
```

`InputReleased`：

```cpp
StopOwnerPredictedFeedback();

if (HasAuthority(&ActivationInfo))
{
    StopAuthorityWeapon();
}

// 预测客户端的释放意图已由可靠 ServerSetInputReleased 承载；
// GA_Fire 显式不接受客户端直接结束服务器实例，不再复制第二条结束命令。
const bool bReplicateEnd = HasAuthority(&ActivationInfo);
EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEnd, false);
```

`EndAbility` 清理顺序：

```text
1. StopOwnerPredictedFeedback()   清 Timer、复位标志与 Ordinal
2. if (CachedWeapon.IsValid())
     OnOutOfAmmo.RemoveAll(this)
     if (HasAuthority(&ActivationInfo)) CachedWeapon->StopFiring();   // 新增权威门
     CachedWeapon.Reset()
3. Super::EndAbility(...)
```

建议 Feature Tests：

```text
ShootGame.Ability.Fire.Prediction.Policy
ShootGame.Ability.Fire.Prediction.OwnerImmediateSemiAuto
ShootGame.Ability.Fire.Prediction.ListenNoDuplicate
ShootGame.Ability.Fire.Prediction.RemoteConfirmedOnly
ShootGame.Ability.Fire.Prediction.DedicatedNoLocalFeedback
ShootGame.Ability.Fire.Prediction.ServerRejectCleanup
  （必须由客户端发起：客户端预测到服务器 Reject 到客户端实例结束与本地节拍停止；
    只查服务器 CanActivateAbility 不算覆盖）
ShootGame.Ability.Fire.Prediction.ReloadFireImmediate
  （客户端尚未收到 State.Reloading 时按开火：允许有限本地表现；服务器必须 Reject；
    0 Authority Commit / 0 Projectile / Ammo 未被扣减；Reject 后本地循环停止并收敛）
ShootGame.Ability.Fire.Prediction.RejectRefireCooldown
ShootGame.Ability.Fire.Prediction.FirstShotReadyAfterAcquire
ShootGame.Ability.Fire.Prediction.LocalFeedbackCadencePerWeapon
ShootGame.Ability.Fire.Prediction.ConfirmedFallbackBypassesCadence
ShootGame.Ability.Fire.Prediction.SingleAuthorityProjectile
```

通过条件：

```text
Owner 同一客户端时钟上 FIRE_PREDICTED_OWNER 早于 AUTHORITY_CONFIRMATION_RECEIVED
一次输入的 Owner 第一人称反馈计数为 1
  （仅限客户端未处于换弹 / 切枪 / 死亡，且武器可见且为当前装备时）
无丢包场景的 Authority 与 Remote 单发计数增量都为 1
Emulated 场景的 Remote 增量不超过 Authority，且多发窗口内至少收到一次
Reject 后 Projectile / Ammo / Damage 均不变
Reject 后没有活动 Ability、Local Timer 或残留 State.Firing
同一武器的普通预测表现间隔不小于 RefireRate，不同武器互不继承冷却
本地冷却误挡但服务器接受时确认回退恰好补播一次
```

建议提交：`GAS：实现半自动开火本地预测反馈`

### 6.4 P1-C：全自动本地表现节拍与取消收敛

`StartOwnerPredictedFeedback` 当帧先播放一次并记为 `PredictedShotOrdinal=1`；仅当
`Weapon.IsFullAuto()` 时启动 `PredictedFeedbackTimer`。循环 Timer 的首次回调发生在一个
`RefireRate` 之后，不配置零延迟 FirstDelay：

```cpp
// RefireRate 允许为 0；下限保护避免同帧死循环。
const float Interval = FMath::Max(Weapon.GetRefireRate(), 0.01f);
GetWorld()->GetTimerManager().SetTimer(PredictedFeedbackTimer, this,
    &UShooterGameplayAbility_Fire::HandlePredictedFeedbackTick, Interval, /*bLoop*/ true);
```

`HandlePredictedFeedbackTick` 逻辑：

```text
++PredictedShotOrdinal
复核：Avatar 有效 && Weapon 有效 && !Weapon->IsHidden()
     && GetCurrentWeaponForAvatar(Avatar) == Weapon
失效 → EndAbility(..., false, /*bWasCancelled*/ true) 并返回，不得串到新 Owner
否则 PlayOwnerPredictedShotFeedback() 加 FIRE_PREDICTED_OWNER 标记
```

服务器发起的取消继续沿用现有 GAS 复制链，依据修正 6：

```text
切枪 / Reload / Equip / Death / OutOfAmmo
→ 服务器 CancelAbilitiesByTag(Input.Fire) 或 EndAbility
→ ClientCancelAbility / ClientEndAbility
→ 拥有者预测实例 EndAbility
```

Inventory Clear 当前只在死亡与 EndPlay 生产路径发生，由它们先取消 Fire；不得把
`ClearInventory()` 本身记录成已有取消入口。客户端 Pawn 销毁或 Avatar 更换依赖
`AShooterCharacter::EndPlay` 在 `ClearActorInfo()` 前执行本地取消（见 5.4 节）。

权威隔离要求：本地路径不得调用
`StartFiring` / `Fire` / `StopFiring` / `ConsumeAmmo`，也不得写 `TimeOfLastShot`。
普通预测在进入表现入口后还要通过当前 WeaponActor 的纯表现冷却；不得把冷却放进全局、ASC 或
GA 实例形成跨武器共享。网络用例必须使用真实半自动武器配置连点，不得用反射在客户端篡改
`bFullAuto` 制造测试条件。

建议 Feature Tests：

```text
ShootGame.Ability.Fire.Prediction.FullAutoImmediate
ShootGame.Ability.Fire.Prediction.FullAutoRelease
ShootGame.Ability.Fire.Prediction.CancelReload
ShootGame.Ability.Fire.Prediction.CancelEquip
ShootGame.Ability.Fire.Prediction.CancelDeath
ShootGame.Ability.Fire.Prediction.CancelWeaponDestroyed
ShootGame.Ability.Fire.Prediction.CancelAvatarChanged
```

通过条件：按住立即进入本地节拍且松开当帧停止；本地节拍停止后 0.5 秒内
服务器弹丸与 Ammo 不再变化；上述取消路径结束后
`PredictedFeedbackTimer` 未激活、`PredictedShotOrdinal` 复位、`CachedWeapon` 为空。

建议提交：`射击：补齐全自动预测表现与取消收敛`

### 6.5 P1-D：网络证据、清理与阶段验收

扩展既有协调器，不另建第二套框架。

新增 2 个客户端到服务器的 `UFUNCTION(Server, Reliable)` 证据上报，沿用现有命名风格：

```cpp
ServerReportOwnerFireFeedbackEvidence(
    int32 PredictedCount,
    int32 AuthorityConfirmationCount,
    int32 PredictionKey,
    bool bPredictedBeforeAuthorityConfirmation)
ServerReportRemoteConfirmedFeedback(int32 Count)
```

约束：

- 观测源必须是生产计数器（见 8.1 节），不得在协调器里复制实现。
- Owner 的预测时间与 Authority Confirmation 接收时间必须来自同一 Owner 客户端时钟。
- AuthorityShotCount、Ammo 与 ProjectileCount 由服务器协调器直接读取，不经客户端 RPC 上报。
- 无丢包的半自动场景断言单发增量为 1；全自动按场景起止快照比较计数增量，
  不把累计值写死为 1。
- `MulticastPlayFiringFX` 是 Unreliable RPC。Emulated 丢包场景只要求 Remote 增量不大于
  Authority 增量、绝不重复，并在多发窗口内至少收到一次；不得强求逐发相等。
- 在 `ShooterNetworkTestCoordinator.cpp:2634-2659` 巨型合取中追加门闩，
  并在 `:2665-2742` 的 `AUTOMATION_TEST_CLIENT_SUCCESS` 行追加字段。
- 远端观测不得改为读取真实弹量；弹药是 `COND_OwnerOnly`，
  远端只以“第三人称确认表现计数”为证。

其余工作：

```text
Dedicated / Listen 无丢包场景检查 Remote 与 Authority 增量一致
Emulated 检查 Remote 无重复且不超过 Authority，多发窗口内至少收到一次
三态都检查无双第一人称反馈、无双弹丸
确认三份日志都出现 LogNet: PktLag set to 100 与 PktLoss set to 2
删除 P1 产生的临时日志、调试字段与无调用者旧表现入口
更新 Shooter完整Demo最终路线规划的当前阶段状态
运行七阶段全量回归并记录 Summary
```

建议提交：`测试：完成P1预测射击反馈网络回归`

---

## 7. 测试改造清单

### 7.1 必须改写的 2 处断言，覆盖 7 个测试

改动点一：`Tests/Ability/ShooterAbilityFireAutomationTests.cpp` 第 28-30 行

- 现断言：`GA_Fire uses ServerOnly net execution`。
- 改写为：`GA_Fire uses LocalPredicted net execution`。

改动点二：`Tests/Ability/ShooterAbilityFireBehaviorAutomationTests.cpp` 第 25-27 行

- 现断言：`GA_Fire executes only on server`。
- 改写为：同一断言的 LocalPredicted 版本，并重命名 helper
  `TestServerOnlyContract`。

受影响测试：

```text
ShootGame.Ability.Fire.Grant.Player         经 FireAutomationTests.cpp:78
ShootGame.Ability.Fire.Grant.NPC            经 FireAutomationTests.cpp:98
ShootGame.Ability.Fire.ServerOnly           :105
ShootGame.Ability.Fire.SingleActivation     :117
ShootGame.Ability.Fire.SingleProjectile     :129
ShootGame.Ability.Fire.AmmoConsume          :138
ShootGame.Ability.Fire.FullAutoRelease      :150
```

### 7.2 命名与语义同步

- `ShootGame.Ability.Fire.ServerOnly` 拆为
  `ShootGame.Ability.Fire.Prediction.Policy` 与
  `ShootGame.Ability.Fire.AuthorityBoundary`。
- 上游 10.2 节禁止“删除断言让策略切换通过”。
  `TestWeaponExecutionBoundary`（`:36-57`）与
  `TestAmmoAuthorityContract`（`:59-81`）必须原文保留，
  它们表达的权威边界在 P1 后仍然成立且更重要。
- `ShooterGameplayAbility_Fire.h:13` 与 `:38` 的注释必须改写。
- `TestServerOnlyContract` 在 Fire / Equip / Reload 三个文件重名，
  只重命名 Fire 版本。

### 7.3 无需改动

```text
ShooterAbilityFireCancellationAutomationTests.cpp   全部 7 项
ShooterAbilityReloadEquipAutomationTests.cpp:90-92  只断言阻塞 Tag
ShooterAbilityEquipBehaviorAutomationTests.cpp      断言各自 Ability
ShooterAbilityReloadBehaviorAutomationTests.cpp     断言各自 Ability
ShootGame.Architecture.Baseline.AnimBPSurface       只断言 AnimBP 父类
ShootGame.Ability.Fire.ActorInfo / Grant.RespawnNoDuplicate / Reject.* / Cancel.* / NPC
```

### 7.4 新增测试落位

建议新增 `Source/ShootGame/Tests/Ability/ShooterAbilityFirePredictionAutomationTests.cpp`。

该文件不在上游 11 节的主要文件清单内，属新增文件；
结构变化完成后只运行一次
`Scripts/Development/RefreshVisualStudioFiles.ps1`，并提示用户 VS 存在待重载状态。

若需规避新增文件，可把 P1-A/B/C 的 Feature Tests 追加进现有三个 Fire 测试文件，
但必须与 7.1 节的重命名解耦。

---

## 8. 可观测性设计

### 8.1 生产计数器

挂在 `AShooterWeapon`，与既有 `SetAmmoForAutomationTest`（`ShooterWeapon.h:417-424`）同风格：

```cpp
#if WITH_DEV_AUTOMATION_TESTS
public:
	void ResetFireFeedbackCountersForAutomationTest();
	void RecordOwnerAuthorityConfirmationForAutomationTest();
	int32 GetPredictedOwnerFeedbackCountForAutomationTest() const;
	int32 GetOwnerAuthorityConfirmationCountForAutomationTest() const;
	int32 GetAuthorityShotCountForAutomationTest() const;
	int32 GetRemoteConfirmedFeedbackCountForAutomationTest() const;
	bool IsFiringForAutomationTest() const;
	float GetTimeOfLastShotForAutomationTest() const;
	bool IsRefireTimerActiveForAutomationTest() const;

private:
	int32 PredictedOwnerFeedbackCount = 0;
	int32 OwnerAuthorityConfirmationCount = 0;
	int32 AuthorityShotCount = 0;
	int32 RemoteConfirmedFeedbackCount = 0;
#endif
```

递增位置必须是生产表现入口：

```text
PredictedOwnerFeedbackCount  PlayOwnerPredictedShotFeedback 播放成功后
OwnerAuthorityConfirmationCount Multicast 到达 Owner、跳过可见 FX 前
AuthorityShotCount           Fire() 成功 ConsumeAmmo 并执行开火行为后
RemoteConfirmedFeedbackCount MulticastPlayFiringFX 在非拥有端执行后
```

计数器属于各网络端自己的 Weapon 副本。协调器在每个测试场景开始时对稳定的目标 Weapon 记录
起点快照，验收只比较场景增量；不得假设池化 Actor 的历史累计值为零，也不得在开火过程中重置。

Shipping 构建零增量；测试只读计数，不复制实现。

### 8.2 权威字段写入探针

P1-A 需要证明本地路径不写权威状态。做法：

```text
调用前：通过 WITH_DEV_AUTOMATION_TESTS 只读接口读取
        bIsFiring / TimeOfLastShot 与 RefireTimer 活动状态
调用 PlayOwnerPredictedShotFeedback() 三次
调用后：逐项比对；断言 RefireTimer 仍未激活
```

这三个字段都不是反射属性，禁止使用 `FindFProperty` 假装读取；测试 Getter 只读现有状态，
不得为测试复制生产判定。

### 8.3 统一日志标记

| 标记 | 输出位置 |
|---|---|
| `FIRE_PREDICTED_OWNER` | `PlayOwnerPredictedShotFeedback` 播放成功后（拥有端） |
| `FIRE_OWNER_CONFIRMED_FALLBACK` | 初始门控跳过、服务器随后接受时补播成功后（拥有端） |
| `FIRE_AUTHORITY_CONFIRMATION_RECEIVED` | Multicast 到达 Owner 并跳过可见 FX 前 |
| `FIRE_AUTHORITY_COMMIT` | `Fire()` 成功扣弹并执行开火行为后（权威端） |
| `FIRE_REMOTE_CONFIRMED` | `MulticastPlayFiringFX` 在非拥有端执行后 |
| `FIRE_PREDICTION_REJECTED` | 协调器根据 Reject 场景最终结果输出，不依赖 EndAbility 必然再次进入 |
| `FIRE_LOCAL_FEEDBACK_STOPPED` | `StopOwnerPredictedFeedback` 真实停掉活动 Timer 时 |

字段按所在时钟域固定：

```text
Owner 预测/确认：PlayerId / Weapon / PredictionKey / PredictedShotOrdinal
                  / OwnerLocalTime / Count / NetMode
Authority Commit：PlayerId / Weapon / AuthorityShotOrdinal / ServerWorldTime
                  / Count / NetMode
Remote Confirmed：PlayerId / Weapon / RemoteShotOrdinal / ObserverLocalTime
                  / Count / NetMode
```

- `PredictionKey` 取自 `GetCurrentActivationInfo().GetActivationPredictionKey().Current`
  （`GameplayAbilitySpec.h:152`，非弃用），标识一次激活或一次 Burst。
- `ShotOrdinal` 标识该 Burst 中第几次反馈；不得假设全自动每发都有新 PredictionKey。
- Weapon 权威 Timer 后续射击拿不到 GA 的 PredictionKey；不得为日志把 PredictionKey 持久写入 Weapon。
- 跨进程的 `OwnerLocalTime / ServerWorldTime / ObserverLocalTime` 不直接比较大小。
- `PlayerId` 取 `GetPlayerState()->GetPlayerId()`，NPC 用 `INDEX_NONE`。
- 日志等级统一 `LogShootGame, Display`，禁止 `LogTemp`。

---

## 9. 验证流程

每个产生代码改动的子阶段：

```powershell
# 1. Preflight
codegraph.cmd explore "<本阶段改动符号>"

# 2. 编译
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\BuildEditor.ps1

# 3. 定向测试
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAutomation.ps1 `
  -TestFilter "ShootGame.Ability.Fire"

# 4. 定向网络场景（不跑 RunAll）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunNetworkSession.ps1 `
  -ServerMode Dedicated -ClientCount 2 -Port 17790 `
  -ServerExtraArgs @("-ShootGameNetworkTest")

# 5. 版式与 include 门禁
git add <本阶段文件>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Development\CheckTextLayout.ps1 -Scope Staged
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Development\CheckSourceIncludePaths.ps1
git diff --cached --check
```

P1-A / P1-B / P1-C 不得提前运行 `RunAll.ps1`。仅在 P1-D 收口时运行一次：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <unused>
```

必须逐项读取的产物：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
Saved/Automation/Reports/<ts>_ShootGame/index.json
Saved/Automation/Logs/<ts>_ShootGame.log
Saved/Automation/Standalone/<ts>.log
Saved/Automation/Network/<ts>/Server.log + Client1.log + Client2.log
```

`Summary.json` 顶层字段：
`startedAt / finishedAt / status / project / testFilter / stages / error`。

阶段字段：`name / status / startedAt / durationSeconds / error`。

七阶段端口与阶段名：

```text
Build / Automation / Standalone / DedicatedNetwork / ListenNetwork
/ EmulatedNetwork / DisconnectCleanup
17777 Dedicated / 17778 Listen / 17779 Emulated / 17780+17781 Disconnect
```

`DisconnectCleanup` 内部跑两次 `RunNetworkSession.ps1`，
产生两个 `Saved/Automation/Network/<ts>/` 目录。

Emulated 必须确认 Server 与两个 Client 均出现
`PktLag set to 100` 与 `PktLoss set to 2`。

---

## 10. 风险登记与回退

- R1 监听主机第一人称双播。
  触发判据：Host 上预测与远端确认同帧各计 1 且可见。
  处置：回退 P1-B；检查 Multicast 的音效与第一人称枪口抑制。
- R2 NPC 被误判为拥有者本地视图。
  触发判据：Listen Server 的 NPC 被抑制第三人称音效或触发 Owner 预测表现。
  处置：回退 P1-B；确认 GA 与 Weapon 都使用本地玩家判据，而不是普通本地 Controller 判据。
- R3 Host / Standalone Recoil 双次。
  触发判据：单次按下俯仰增量为两倍 `FiringRecoil`。
  处置：回退 P1-B；检查 `ExecuteFireAtTarget` 的 Recoil 门。
- R4 本地纯表现节拍错误。
  触发判据：同一武器在 `RefireRate` 内连续提交普通预测，或切换到另一把武器后错误继承前一把冷却；
  服务器接受的射击被冷却误挡后没有确认回退。
  处置：检查冷却是否归每个 WeaponActor、确认路径是否显式旁路，以及池 / Owner 边界是否复位。
- R5 Reject 后本地 Timer 残留。
  触发判据：拒绝后 Timer 仍活动；半自动没有活动 Timer 时不强求停止标记。
  处置：确认清理放在 `EndAbility`，而不是只放在 `InputReleased`。
- R6 长寿命预测激活缺乏官方明文指引。
  触发判据：无客观判据，属证据缺口。
  处置：本地路径严格零 Gameplay 状态，不建预测 GameplayEffect 与 Attribute。
- R7 客户端 Pawn 销毁后本地 Timer 残留。
  触发判据：重生后旧 Timer 仍 Tick。
  处置：`EndPlay` 补充取消加 Tick 内武器复核双保险。
- R8 远端第三人称表现退化。
  触发判据：无丢包场景 Remote 不再收到 `State.Firing` 或确认表现；
  Emulated 多发窗口一次确认都未收到。
  处置：已验证观察端集合等价；若退化则回退 P1-B 并对齐 Tag 复制通道。

必须立即回退当前子阶段（沿用上游 13 节）：

```text
客户端能直接扣 Ammo / Spawn Projectile / Apply Damage / 计分
一次输入在服务器生成两个 Projectile
Owner 收到两次可见的第一人称反馈
Listen Host 与 Dedicated Client 的 Gameplay 权威不一致
  （Reject 之前的纯表现次数不要求一致：Host 可能 0 次假反馈，远端 Owner 可能短暂预测若干次）
Prediction Reject 后残留 Local Timer、Ability 或 State.Firing
全自动松开后仍持续产生权威弹丸
P1 修改破坏 NPC ServerOnly 开火
为消除测试失败而降低既有 Gameplay 断言
```

必须立即回退的还包括：出现双 Gameplay、预测循环残留、Reject 后继续射击。

可接受的 P1 边界（修订版，替代“只允许一次极短反馈”）：

```text
Reject 到达前允许有限的纯本地预测表现，含全自动武器的短暂连续反馈
  前提：不产生任何客户端 Gameplay 结果（不扣 Ammo / 不生成 Projectile / 不结算伤害）
  节拍：普通预测始终受当前 WeaponActor 的 RefireRate 纯表现冷却约束
  要求：Reject 到达后立即停止 PredictedFeedbackTimer，并清理 Ability / State.Firing / CachedWeapon
本机已知 State.Reloading / State.Equipping / State.Dead，或武器已隐藏、已不是当前装备
  只禁止本地预测表现，请求仍照常发给服务器，由服务器完整校验并 Reject
若迟到的本地阻塞 Tag 跳过了初始表现，而服务器接受同一次激活
  ConfirmActivateSucceed 复核当前武器后只补播一次；宽限只持续到这批 Tag 首次清除；Reject 不补播
若本地纯表现冷却误挡了一个最终被服务器接受的射击
  ConfirmActivateSucceed 旁路冷却只补播一次并推进冷却；Reject 不产生任何后续反馈
全自动本地预测循环不等待服务器 Confirm，允许与权威节拍存在短暂相位偏移
```

同一失败项最多自主修复 3 轮。仍不收敛时停止扩展并报告：
失败测试、日志路径、PredictionKey、三方计数、已尝试修复、是否建议回退。

---

## 11. 待确认决策点

### 11.1 新增测试文件还是追加到现有文件

- 新增 `ShooterAbilityFirePredictionAutomationTests.cpp`：结构更清晰，
  但会触发一次 `RefreshVisualStudioFiles.ps1` 与 VS 重载提示。
- 追加到现有三个 Fire 测试文件：避免结构变化，但文件继续膨胀且与 7.1 节重命名耦合。

建议：新增文件。

### 11.2 本地节拍 Tick 起点相位

不再保留两个实际相同的候选方案，冻结为：

```text
按下当帧同步播放 PredictedShotOrdinal=1
→ 循环 Timer 首次回调延后一个 RefireRate
→ 此后每 RefireRate 播放一次
```

不得把 Timer FirstDelay 设为 0，否则会在按下帧把手动首发与首次 Tick 重复播放。

### 11.3 `IShooterWeaponHolder` 新方法形态

- 纯虚：必须同步实现两个实现者，接口契约显式，与现有接口风格一致。
- 带默认实现的虚函数：改动面更小，但引入项目当前没有的可选接口方法模式。

建议：纯虚。

---

## 12. 提交与开发记录要求

每个子阶段一份 `Docs/开发记录/YYYY-MM-DD-HHMM-<主题>.md`，
与代码进入同一提交，必填小节的格式按 `Docs/开发记录/README.md`。

提交前必须运行：

```powershell
Scripts/Development/CheckTextLayout.ps1 -Scope Staged
Scripts/Development/CheckSourceIncludePaths.ps1
git diff --cached --check
git diff --cached --name-only
```

落地顺序：

```text
P1-0 证据
→ P1-A（无策略变化，可独立提交并随时停下）
→ P1-B（策略切换与半自动闭环，风险最集中，必须独立提交并跑定向网络场景）
→ P1-C（全自动节拍与取消收敛）
→ P1-D（网络证据、清理、七阶段回归与阶段验收）
```

阶段结束后的强制复盘问题见上游 15 节。
本次补做的检索已为复盘问题 3（是否迁移 GameplayCue）提供依据：
GAS 自带 `EGameplayCueNotify_LocallyControlledPolicy` 的
`Always` / `LocalOnly` / `NotLocal` 原语，
与 P1 手工实现的“拥有者本地 vs 远端确认”分工是同一语义。
迁移的收益是复用官方表现通道与资产级过滤，
成本是引入 Cue 资产管线、每发预测键关联与新旧路径并存期。
该评估必须在 P1 验收后单独进行，不得提前实施。
