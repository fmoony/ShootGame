# Pickup 接入 WeaponId 预创建池并删除旧 RowName 授予路径

- 日期：2026-09-12
- 计划提交说明：`Pickup：接入 WeaponId 预创建池并删除旧 RowName 授予路径`
- 变更类型：生产代码 / 测试 / 资产 / 构建

## 目的

完成 [武器启动预配置与实体池简化重构方案](../执行计划/武器启动预配置与实体池简化重构方案.md) S4：
Pickup 只保存 `FName WeaponId`，玩家与 NPC 都从同一 WeaponRuntimeSubsystem 池租用预配置
WeaponActor，并删除旧 RowHandle / RowName、Instance 兼容分支与通用 ActorPool。

## 本提交完成内容

- `AShooterPickup::WeaponType(FDataTableRowHandle)` 改为 `WeaponId(FName)`；
  授予链改为 `HasWeaponId → AcquireWeapon → Inventory.AddWeapon → Equipment.EquipWeapon`，
  失败均不消费 Pickup；编辑器预览 Mesh 是唯一 Editor-only 查表入口；
- `AShooterNPC` 删除 WeaponClass / WeaponRowName 兼容路径，BeginPlay 从 WeaponRuntimeSubsystem
  租用 WeaponId 对应 Actor 并激活；NPC 销毁时由 OnOwnerDestroyed 归还池；
- `AShooterWeapon` 删除 `WeaponRowName / ResolveWeaponRow / SetWeaponRow` 与 NPC 自动补弹分支；
  弹药与开火行为完全以 WeaponId / ConfigSnapshot 为准；
- 删除 `UShooterActorPoolSubsystem`、`IShooterPoolableActor` 及旧通用池测试；
- `DefaultEngine.ini` 增加 `ShooterNPC.WeaponRowName → WeaponId` 的 PropertyRedirect；
- 一次性 `ShootGame.Tools.WeaponIdAssetMigration` 工具把四个 Pickup BP、BP_ShooterNPC 与
  DT_WeaponData 通过 `UPackage::SavePackage` 保存；`ShootGame.Tools.WeaponIdAssetReadback`
  在独立进程回验落盘值；工具执行后删除，不在正式测试套件中；
- 网络协调器换弹阶段改为使用正式 Pistol 的实际弹匣容量，并按 `ReloadDuration` 动态等待提交，
  删除已不再使用的 `AShooterNetworkTestReloadWeapon`；
- 新增 `ShootGame.Weapon.Configuration` 中的 Pickup / NPC BP WeaponId 与 InitialPoolSize 资产校验。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：通过；
- `Scripts/Tests/RunAutomation.ps1`：通过，80 项成功（含 33 项带警告），Failed=0、NotRun=0；
  报告：`Saved/Automation/Reports/20260912_125743_ShootGame/index.json`；
- `Scripts/Tests/RunNetworkSession.ps1 -MapPath /Game/Shooter/Maps/Lvl_Shooter -ServerMode Dedicated
  -ClientCount 2 -Port 17800 -SessionDurationSeconds 45 -SuccessMarker AUTOMATION_TEST_CLIENT_SUCCESS
  -SuccessMarkerCount 2 -ServerExtraArgs -ShootGameNetworkTest`：通过；
  日志：`Saved/Automation/Network/20260912_125655`；
- 资产迁移独立回验：`ShootGame.Tools.WeaponIdAssetReadback` 通过；
  报告：`Saved/Automation/Reports/20260912_123859_ShootGame.Tools.WeaponIdAssetReadback/index.json`。

## 遇到的问题

- 一次性资产保存首次因 `Error Code 32` 失败：运行中的 UnrealEditor 持有六个资产文件句柄，
  SavePackage 无法把旧包移动到 temp；
- Dedicated 2 Clients 首轮 0 成功标记：网络协调器仍按旧“替换成 30 发测试手枪”的假设执行换弹阶段，
  正式 Pistol 容量为 10、ReloadDuration 为 2.2s，导致满弹匣与提交时机断言错误。

## 处理方式

- 确认相关 Blueprint `packageDirty=false` 后关闭运行中的 UnrealEditor，重新执行资产迁移与
  独立进程 Readback，六个资产均确认落盘；
- 换弹测试改为使用正式 Pistol 的实际 `GetMagazineSize()`，并让 `ReloadTransferCheckTime`
  按 `GetReloadDuration() + 0.3s` 动态等待，删除已无消费者的测试武器类；
  复跑 Dedicated 2 Clients 通过。

## 遗留项

- S5 的七阶段完整回归（Standalone / Listen / Emulated / DisconnectCleanup）尚未运行；
- 没有对 Lvl_Test 中 Pickup 实例做逐实例属性审计；四种 Pickup BP 默认值迁移后，
  依赖默认值的实例自动跟随，个别覆盖值仍需编辑器人工抽查。
