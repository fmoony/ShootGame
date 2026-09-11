# WeaponDefinition 类型与 AssetManager 解析闭环（A1）

- 日期：2026-09-11
- 计划提交说明：`武器：新增 WeaponDefinition 正式定义资产与 AssetManager 解析闭环`
- 变更类型：生产代码 / 测试 / 资产 / 构建

## 目的

实施《武器与 Inventory 正式架构实施计划》大阶段 A1：建立 `UShooterWeaponDefinition` 正式定义类型、固定 PrimaryAssetType、AssetManager 扫描配置与同步解析入口，并覆盖无效 ID、错误类型、未注册资产、非法弹匣容量的 fail closed 失败路径，为 A2 Inventory 切换授予数据源做准备。

## 本提交完成内容

- 新增 `Weapons/Definitions/ShooterWeaponDefinition.h/.cpp`：
  - `UShooterWeaponDefinition : UPrimaryDataAsset`，保存共享只读配置：`WeaponActorClass`、`AmmoConfig`（MagazineSize / InitialReserveAmmo）、`FireConfig`（bFullAuto / RefireRate / ShotNoise 三件套）、`PresentationConfig`（FP/TP 网格与 AnimClass、Montage、Niagara、音效、后坐力、散布、Pickup 预览网格）；
  - `GetPrimaryAssetId()` 显式收敛到固定类型 `ShooterWeaponDefinition`（重写实现，不依赖任何蓝图类名；本地 UE 5.6 `DataAsset.cpp` 已核对默认行为）；
  - `IsValidForGrant()`：无效 ID、缺失/非法 WeaponActorClass、非法弹匣容量、非法备弹声明全部拒绝；
  - `ResolveDefinitionSync()`：类型校验 + AssetManager 同步解析，失败统一返回 nullptr 并记录日志。
- `Config/DefaultGame.ini`：新增 `[/Script/Engine.AssetManagerSettings]` 扫描规则，目录固定 `/Game/Shooter/Weapons/Definitions`，`bHasBlueprintClasses=False`。
- `ShootGame.Build.cs`：新增 `ShootGame/Weapons/Definitions` include 路径。
- 新增测试 `Tests/WeaponDefinition/ShooterWeaponDefinitionAutomationTests.cpp`：
  - `DefinitionIdStability`：固定类型、名称寻址、跨读取稳定、CDO 无身份、不同资产不冲突；
  - `InvalidConfigFailClosed`：五类非法配置全部拒绝，备弹 -1 自动回落与显式值直用正确；
  - `ResolveFailClosed`：无效 ID / 异类型 / 未注册资产均返回 nullptr；
  - `AssetResolution`：WD_TestAuto 经 AssetManager 发现并同步加载，路径与 ID 互认。
- 新增临时迁移工具测试 `Tests/Tools/ShooterWeaponDefinitionMigrationToolTests.cpp`（`ShootGame.Tools.WeaponDefinition.CreateTestAsset`）：通过 Editor 资产系统创建/更新并保存 `WD_TestAuto`；按计划在 B4 收口时删除。
- 新增内容资产 `Content/Shooter/Weapons/Definitions/WD_TestAuto.uasset`（最小测试 Definition，指向 BP_ShooterWeapon_Rifle）。

## 验证结果

- `BuildEditor.ps1`：Passed。
- `RunAutomation.ps1 -TestFilter ShootGame.WeaponDefinition`：Passed=3 Warnings=1 Failed=0；Warning 来自 ResolveFailClosed 故意触发的解析失败日志被测试捕获，属预期。
- `RunAutomation.ps1 -TestFilter ShootGame.Tools.WeaponDefinition`：Passed=1；WD_TestAuto 成功落盘并被后续 AssetResolution 测试发现（诊断输出 `Discovered=1 Paths=[/Game/Shooter/Weapons/Definitions/WD_TestAuto.WD_TestAuto]`）。

## 遇到的问题

- 首版 INI 规则使用 `Directories="/Game/Shooter/Weapons/Definitions"`，规则与 AssetBaseClass 均正常加载，但 Directories 数组成员导入为空，AssetManager 发现 0 个资产。
- 初次编译报 C2487（dll 接口类成员重复标记 SHOOTGAME_API）、`FindObject` 重载不匹配（FName 参数）、`SavePackage` 第 3 参需 `const TCHAR*`。

## 处理方式

- 核对引擎 `BaseGame.ini` 默认规则写法：`Directories` 是 `TArray<FDirectoryPath>`，导入必须用结构体语法 `Directories=((Path="..."))`；修正后规则目录加载、扫描发现 1 个资产，解析测试通过。
- 移除成员级 SHOOTGAME_API（类已导出）、改用 `FindObjectFast`、`SavePackage` 传 `*FileName`。

## 遗留项

- Cook/Standalone 下的可发现性尚未真实打包验证（七阶段回归无 Cook 阶段）；当前证据为 AssetManager 配置正确 + Editor 命令行解析闭环。Standalone 消费路径将在 A4（Pickup 消费 Definition）后由 Standalone 冒烟间接覆盖。
- PresentationConfig 字段当前为数据归档，WeaponActor 组件模板仍是表现堆栈的活动数据源；消费者迁移按计划在 A4/B 阶段推进。
- 临时迁移工具测试将在 B4 删除。
