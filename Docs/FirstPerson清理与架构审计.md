# FirstPerson 清理与架构审计

## 审计目标

Shooter 已经成为项目唯一默认玩法，但 `/Game/FirstPerson` 和根目录模板 C++ 类仍然存在。本审计用于区分：

- 可以直接删除的旧模板内容；
- Shooter 仍在使用、必须先迁移的共享资产；
- 名称像旧模板、但当前仍承担共享基类职责的 C++ 类型。

审计只形成迁移边界，不在同一步中移动或删除资产。

## 当前结论

### FirstPerson 资产

Asset Registry 共识别到 73 个 FirstPerson 包：

- `/Game/FirstPerson`：7 个主资产；
- `/Game/__ExternalActors__/FirstPerson`：65 个地图 External Actor；
- `/Game/__ExternalObjects__/FirstPerson`：1 个地图 External Object。

其中只有 3 个主资产存在 FirstPerson 目录外的直接引用；把它们的 FirstPerson 内部依赖递归展开后，必须保留和迁移的最小闭包为 4 个资产：

| 迁移源资产 | 目录外用途 | 当前目标目录 |
| --- | --- | --- |
| `/Game/FirstPerson/Anims/ABP_FP_Copy` | `BP_ShooterCharacter` 与 `BP_ShooterNPC` 使用 | `/Game/Variant_Shooter/Anims/Base` |
| `/Game/FirstPerson/Anims/CtrlRig_FPWarp` | `ABP_FP_Copy` 的内部依赖 | `/Game/Variant_Shooter/Anims/Base` |
| `/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter` | `ABP_FP_Weapon` 与 `ABP_FP_Pistol` 使用 | `/Game/Variant_Shooter/Blueprints/AnimationSupport` |
| `/Game/FirstPerson/MI_FirstPersonColorway` | Shooter 投射物、场景 External Actor 和 LevelPrototyping 资产使用 | `/Game/Shared/Materials` |

上述 4 个资产已于 2026-08-14 迁移完成，资产名称保持不变。Asset Registry 复查确认 4 个旧路径的 FirstPerson 目录外引用者总数为 0。`MI_FirstPersonColorway` 的旧路径暂时保留 Redirector，只供下一步将被删除的 FirstPerson 地图包使用。

其余 FirstPerson 内容主要是旧示例地图、GameMode、PlayerController 和对应 External Actor/Object，已于 2026-08-14 删除。`/Game/FirstPerson`、`/Game/__ExternalActors__/FirstPerson` 和 `/Game/__ExternalObjects__/FirstPerson` 当前均不存在。

### 根目录 C++ 类

| 类型 | 当前关系 | 去留结论 |
| --- | --- | --- |
| `AShootGameCharacter` | `AShooterCharacter` 与 `AShooterNPC` 的直接基类 | 当前保留；早期网络实验代码已移除，只负责第一人称组件和基础移动输入 |
| `AShootGameGameState` | 仅被已移除的 NetCounter/RPC 实验代码使用 | 已删除；Shooter 比赛状态统一由 `AShooterGameState` 承担 |
| `AShootGamePlayerController` | Shooter Controller 不继承它；只服务旧 FirstPerson 蓝图 | FirstPerson 模板清理后删除 |
| `AShootGameCameraManager` | 仅由 `AShootGamePlayerController` 设置 | 随根 PlayerController 删除 |
| `AShootGameGameMode` | Shooter GameMode 直接继承 `AGameModeBase` | FirstPerson 模板清理后删除 |
| `AShooterPlayerController` | 直接继承 `APlayerController` | 保留 |
| `AShooterGameMode` | 直接继承 `AGameModeBase` | 保留 |
| `AShooterGameState` | 直接继承 `AGameStateBase` | 保留 |

`AShootGameCharacter` 已移除 NetCounter、RPC Matrix 与可靠性实验代码，现在只承担 Shooter 玩家和 NPC 共用的角色基础能力。短期仍不改类名；是否改为 Shooter 命名或拍平继承，需要与蓝图父类和动画蓝图类型引用一起讨论。

## 配置残留

- `DefaultInput.ini` 已将不存在的旧触控资源改为 `DefaultTouchInterface=None`；Shooter 的移动端控件继续由 `BP_ShooterPlayerController` 创建 `UI_TouchInterface_Shooter`。
- `DefaultEditor.ini` 的 `SimpleMapName` 已改为 `/Game/Variant_Shooter/Lvl_Shooter`。
- `DefaultEditorPerProjectUserSettings.ini` 的 Content Browser 默认路径已改为 `/Game/Variant_Shooter`。
- `DefaultGame.ini` 的项目显示名称已从 `First Person Template` 改为 `ShootGame`。
- `DefaultEngine.ini` 继续保留从 `TP_FirstPerson*` 到根目录模板类的历史 Game/Class Redirect。当前 `BP_ShooterPlayerController` 仍包含旧 CameraManager 类标识；移除 Redirect 必须与蓝图及根目录类迁移放在同一个架构改造闭环中，本轮不处理。

资源路径配置已经独立清理。Class Redirect 属于类型兼容层，不按无效资源路径处理。

## 推荐执行顺序

1. 通过 UE 资产系统迁移 4 个保留资产，修复 Redirector，并运行完整七阶段验证。
2. 重新运行 Asset Registry 审计，确认 `/Game/FirstPerson` 不再有目录外引用。
3. 删除 FirstPerson 主资产及其 External Actor/Object，修复 Redirector，并运行完整验证。
4. 清理陈旧的编辑器、触控界面和 Class Redirect 配置，独立验证并提交。
5. 已从 `AShootGameCharacter` 移除早期网络实验输入与 RPC，并删除 `AShootGameGameState`。
6. 已在编辑器中清理 `IMC_Default` 和 `BP_FirstPersonCharacter` 保存的三个实验输入引用，并删除对应 Input Action 资产。
7. 删除已无有效资产引用的根 PlayerController、CameraManager 与 GameMode，并更新项目导航。

## 暂不执行的改动

- 不重命名 `ShootGame` Runtime 模块；模块名与项目名一致，当前没有迁移收益。
- 不立即把 `AShootGameCharacter` 改名为 Shooter 类型；它仍是玩家和 NPC 共用基类，重命名需要单独设计 Class Redirect 与蓝图迁移。
- 不在资产清理阶段接入 GAS；先保住当前 Shooter 网络闭环，再以干净基线开始能力系统改造。
