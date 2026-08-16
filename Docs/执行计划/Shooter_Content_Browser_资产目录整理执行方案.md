# Shooter Content Browser 资产目录整理执行方案

## 1. 文档用途

本文用于在 **GAS 接入之前**，对 ShootGame 当前 Content Browser 资产进行一次独立、可验证、可回滚的目录整理。

本计划是现有源码与旧模板架构整理完成后的下一阶段工作，目标不是继续改玩法逻辑，而是把 Shooter 正式资产从 `/Game/Variant_Shooter` 收束到清晰、稳定的正式目录，并同步清理代码、配置、测试与文档中的旧资产路径。

本计划执行完成后，再进入《GAS、动画分层与客户端预测扩展计划》的 GAS 接入阶段。

本轮处理：

- `/Game/Variant_Shooter` 资产审计；
- Shooter 正式资产目录设计；
- Shooter 非地图资产迁移；
- `Lvl_Shooter` World Partition 地图迁移；
- `Lvl_Test` 普通地图迁移；
- Redirector 清理；
- C++、Config、Automation、脚本中的硬编码资产路径同步；
- 自动化回归；
- 文档更新；
- Content 基线冻结。

本轮不处理：

- GAS 接入；
- AttributeSet / GameplayAbility / GameplayEffect；
- 客户端预测；
- 背包系统；
- 射击逻辑重构；
- 网络协议重构；
- 第三人称动画分层改造；
- `/Game/Characters`、`/Game/Weapons` 等大规模模板资源搬迁。

---

# 2. 当前基线

## 2.1 当前主要 Content 目录

当前已知：

| 目录 | 资产数量 | 当前定位 |
|---|---:|---|
| `/Game/Variant_Shooter` | 43 | Shooter 专属资产，包含蓝图、动画、地图等 |
| `/Game/Characters` | 128 | Mannequin、Skeleton、动画等模板/共享角色资源 |
| `/Game/Weapons` | 28 | 武器 Mesh、Material、Texture 等模板/美术资源 |
| `/Game/LevelPrototyping` | 36 | 长期保留的开发/测试资源库 |
| `/Game/Shared` | 少量 | 跨长期目录共享的正式资源 |

本轮首先处理 `/Game/Variant_Shooter`。

`/Game/Characters`、`/Game/Weapons` 和 `/Game/LevelPrototyping` 不因为目录“不够统一”而在本轮大规模迁移。

## 2.2 当前工作区

计划形成时已知：

- 当前工作区只剩 `CtrlRig_FPWarp.uasset` 的动画修改；
- 该修改确认 **保留**；
- 当前分支比远端超前 7 个提交。

执行时仍必须重新运行：

```powershell
git status --short --branch
```

现场状态优先于本文记录。

当前领先远端 7 个提交暂不作为本计划的额外处理项，不要求因为本轮迁移专门 push、tag 或重写历史。

## 2.3 `CtrlRig_FPWarp` 处理

`CtrlRig_FPWarp` 是 Shooter 当前实际使用的第一人称动画依赖，不属于待删除旧模板垃圾资产。

正式开始目录迁移前：

1. 保留当前 `CtrlRig_FPWarp` 修改；
2. 在编辑器中确认修改结果；
3. 保存资产；
4. 单独补对应开发记录；
5. 独立提交；
6. 然后再进入 Content 目录迁移。

建议提交说明：

```text
动画：调整第一人称 Warp Control Rig
```

目的：

> 不把“动画行为修改”和“大规模资产路径修改”放在同一个提交中。

---

# 3. 目录设计原则

## 3.1 Shooter 以资产类型为主要组织方式

ShootGame 的主要 Gameplay 逻辑长期放在 C++。

蓝图主要承担：

- 表现层；
- 数据配置；
- 编辑器可视化配置；
- C++ 中不方便直接实现的资产侧逻辑；
- 少量需要平衡开发效率与性能的 Blueprint 逻辑。

因此 Content Browser 不需要复制一套和 C++ `Source/` 完全相同的 Gameplay 模块树。

本项目优先使用：

> **资产类型优先，资产类型内部再按具体用途细分。**

而不是：

> 每一个 C++ Gameplay 子系统都对应一个 Content 顶层目录。

## 3.2 最终 `/Game/Shooter` 建议结构

目标结构：

```text
/Game/Shooter/
├── Blueprints/
│   ├── Framework/
│   ├── Characters/
│   ├── AI/
│   └── Weapons/
├── Animation/
│   ├── FirstPerson/
│   ├── ThirdPerson/
│   └── Rigs/
├── Data/
├── Input/
├── UI/
├── FX/
└── Maps/
```

如果审计发现 Shooter 专属且数量值得独立归类的其他资产类型，例如：

```text
Audio/
Materials/
Textures/
Meshes/
```

可以在 `/Game/Shooter` 下按实际需要建立。

**不要为了预先把目录树填满而创建长期空目录。**

---

# 4. 各目录职责

## 4.1 `/Game/Shooter/Blueprints`

用于保存 Shooter 正式玩法 Blueprint。

内部按 Blueprint 承担的 Gameplay 职责细分，而不是把所有资产类型都混在一起。

建议：

```text
Blueprints/
├── Framework/
├── Characters/
├── AI/
└── Weapons/
```

### `Blueprints/Framework`

例如：

- `BP_ShooterGameMode`
- `BP_ShooterPlayerController`
- 其他 Shooter 正式框架入口蓝图

### `Blueprints/Characters`

例如：

- `BP_ShooterCharacter`
- Shooter 专属角色 Blueprint

### `Blueprints/AI`

例如：

- NPC Blueprint
- AI Controller Blueprint
- 其他 Shooter AI Blueprint

StateTree 如果属于独立资产类型，优先按照审计结果决定是否放在 `Data` 或单独子目录，不强制塞进 `Blueprints/AI`。

### `Blueprints/Weapons`

例如：

- `BP_ShooterWeapon_Rifle`
- `BP_ShooterWeapon_Pistol`
- `BP_ShooterWeapon_GrenadeLauncher`
- Pickup Blueprint
- Projectile Blueprint

如果 Pickup / Projectile 数量后续明显增长，可以再在：

```text
Blueprints/Weapons/
```

内部细分：

```text
Pickups/
Projectiles/
```

本轮不为了未来可能存在的规模提前建立过度细分结构。

## 4.2 `/Game/Shooter/Animation`

动画资产按表现视角与 Rig 类型组织：

```text
Animation/
├── FirstPerson/
├── ThirdPerson/
└── Rigs/
```

### `FirstPerson`

用于本地第一人称表现，例如：

- 第一人称 AnimBP；
- 第一人称 Montage；
- Copy Pose 支撑动画；
- 只服务第一人称手臂的动画资产。

### `ThirdPerson`

用于世界空间第三人称表现，例如：

- `ABP_TP_Rifle`
- `ABP_TP_Pistol`
- 第三人称 Montage
- Shooter 专属 Aim / Pose 资源

### `Rigs`

用于 Shooter 实际使用的 Control Rig，例如：

- `CtrlRig_FPWarp`
- `Ctrl_HandAdjusment`
- `Ctrl_HandAdjusment_Pistol`

第一人称与第三人称的职责边界保持不变：

```text
本地玩家
→ 第一人称手臂 / 第一人称摄像机表现

服务器与其他客户端
→ 第三人称全身表现
```

## 4.3 `/Game/Shooter/Data`

用于纯数据与配置资产，例如：

- Data Asset；
- Data Table；
- Curve；
- Shooter 专属配置资产；
- 后续 GAS 接入前仍属于表现/配置层的数据资产。

数据资产按用途命名，不需要再复制一整套 C++ 目录结构。

## 4.4 `/Game/Shooter/Input`

用于 Shooter 正式玩法输入资产：

- Input Action；
- Input Mapping Context；
- Shooter 触控输入配置。

只迁移明确属于 Shooter 正式玩法的 Input。

仍被其他长期目录共享的资产不能仅因为 Shooter 使用就强行收进这里。

## 4.5 `/Game/Shooter/UI`

用于 Shooter 正式玩法 UI：

- HUD；
- Widget；
- Bullet Counter；
- Touch Interface；
- Shooter 专属 UI 表现资产。

## 4.6 `/Game/Shooter/FX`

用于 Shooter 专属表现特效，例如：

- Niagara；
- 粒子；
- Shooter 专属命中/枪口表现资源。

只有 Shooter 独占的 FX 才迁入这里。

跨 Shooter 与 `LevelPrototyping` 共用的资产进入 `/Game/Shared`。

## 4.7 `/Game/Shooter/Maps`

最终保存：

```text
/Game/Shooter/Maps/Lvl_Shooter
/Game/Shooter/Maps/Lvl_Test
```

两张地图必须单独于普通资产批次处理。

其中：

- `Lvl_Shooter`：已确认是 **World Partition 地图**；
- `Lvl_Test`：已确认 **不是 World Partition 地图**。

---

# 5. `/Game/Shared` 设计

## 5.1 是否保留

保留：

```text
/Game/Shared
```

作为长期正式目录。

原因：

- `/Game/LevelPrototyping` 会长期保留作为开发/测试资源库；
- Shooter 与 `LevelPrototyping` 存在共享资产；
- 已经存在类似 `MI_FirstPersonColorway` 这种跨目录实际引用资源；
- 把共享资源强行归入 `/Game/Shooter` 会造成错误所有权。

## 5.2 Shared 的进入条件

只有满足以下条件之一的资产才进入 `/Game/Shared`：

1. 被两个或以上长期保留的正式目录共同使用；
2. 明确设计为项目级共享资源；
3. 没有合理的单一长期所有者。

例如：

```text
/Game/Shooter
+
/Game/LevelPrototyping
```

共同使用的材质，可以进入：

```text
/Game/Shared/Materials
```

## 5.3 Shared 不能成为垃圾桶

以下理由不能作为进入 Shared 的依据：

- “不知道应该放哪里”；
- “这个东西看起来比较通用”；
- “以后可能会用”；
- “先放这里再说”。

默认规则：

```text
有明确单一所有者
→ 放在所有者目录

被长期多个目录实际共享
→ /Game/Shared
```

## 5.4 Shared 按资产类型组织

目标形式：

```text
/Game/Shared/
├── Materials/
├── Textures/
├── Meshes/
├── Animation/
├── FX/
└── Audio/
```

同样遵循：

> 有实际资产再建目录，不为了目录树完整创建大量空目录。

---

# 6. 暂不迁移的根目录

本轮长期保留：

```text
/Game/Characters
/Game/Weapons
/Game/LevelPrototyping
/Game/Shared
```

## 6.1 `/Game/Characters`

约 128 个 Mannequin、Skeleton、动画等资源。

当前视为：

> 模板 / 共享角色 Art 与动画资源库。

本轮不移动。

原因：

- Skeleton 与动画依赖链可能较大；
- AnimSequence / Montage / AnimBP / Control Rig 等可能交叉引用；
- 迁移收益暂时不足；
- GAS 接入不要求先完成这部分资源项目化。

以后如需要整理，单独建立：

```text
Characters Art Migration
```

计划。

## 6.2 `/Game/Weapons`

约 28 个武器模型、材质和贴图。

当前定义：

```text
/Game/Weapons
= 模板 / 共享武器美术资产

/Game/Shooter/Blueprints/Weapons
= Shooter 武器 Gameplay Blueprint
```

因此二者长期并存没有问题。

如果以后决定把所有武器 Art 项目化，再单独迁移。

## 6.3 `/Game/LevelPrototyping`

约 36 个资源。

长期保留作为：

> 开发 / 测试 / 原型资源库。

不合并到 `/Game/Shooter`。

如果某个原型资产后续成为 Shooter 正式资产：

1. 先确认它是否仍被 LevelPrototyping 使用；
2. Shooter 独占则迁入 `/Game/Shooter`；
3. 两边长期共用则迁入 `/Game/Shared`。

---

# 7. 资产迁移通用原则

## 7.1 43 个 `/Game/Variant_Shooter` 资产必须先审计

第一步不是直接拖目录。

先对全部 43 个资产做只读审计，至少记录：

- 资产名；
- 资产类型；
- 当前路径；
- 主要用途；
- 主要引用者；
- 是否地图；
- 是否 Shooter 独占；
- 是否与 `Characters` / `Weapons` / `LevelPrototyping` / `Shared` 共享；
- 建议目标路径；
- 是否存在硬编码文本路径；
- 迁移风险。

形成迁移清单后，再执行移动。

## 7.2 43 个资产的迁移原则

审计完成后：

- 所有确认属于 Shooter 的 **非地图资产** 第一批迁移；
- 地图单独第二批迁移；
- 发现实际属于 Shared 的资产，不强行迁入 Shooter；
- 发现仍与模板根目录形成明确共享关系的资产，先保留或转 Shared；
- 无法判断归属的资产先不动，继续确认。

## 7.3 最终路径只改一次

禁止：

```text
/Game/Variant_Shooter/OldPath
        ↓
/Game/Variant_Shooter/NewPath
        ↓
/Game/Shooter/NewPath
```

推荐：

```text
/Game/Variant_Shooter/OldPath
        ↓
/Game/Shooter/FinalPath
```

目的：

- 减少 Redirector；
- 减少资产重保存；
- 减少硬编码路径重复修改；
- 减少 Git 二进制变化；
- 降低迁移失败后的定位成本。

## 7.4 必须通过 Unreal Editor 移动 `.uasset`

禁止：

- Windows Explorer 直接移动；
- PowerShell `Move-Item`；
- Git 文件级移动代替 UE Asset Move；
- 直接修改 `.uasset`。

所有资产路径变化必须通过 Unreal Editor 的资产系统执行。

## 7.5 每批移动后处理 Redirector

标准顺序：

```text
Move Assets
→ Save All
→ Fix Up Redirectors
→ 检查引用
→ 修改文本硬编码路径
→ 自动验证
```

Redirector 是迁移工具，不是最终依赖机制。

---

# 8. 用户与 Agent 职责边界

## 8.1 用户负责 UE Editor 中的资产操作

用户执行：

1. 根据审计结果建立目标目录；
2. 在 Content Browser 中移动资产；
3. 保存资产；
4. 执行 `Fix Up Redirectors in Folder`；
5. 打开核心 Blueprint / AnimBP 检查；
6. 移动地图；
7. 检查 World Partition 地图状态；
8. 必要时进行带渲染 PIE 人工表现验收。

## 8.2 Agent 负责迁移后的文本与验证

Agent 执行：

1. `git status --short --branch`；
2. 扫描旧资产路径；
3. 修改 C++ 硬编码路径；
4. 修改 Config；
5. 修改 Automation 测试；
6. 修改网络测试协调器；
7. 修改 PowerShell 自动化脚本；
8. 更新当前有效文档；
9. 运行 `git diff --check`；
10. 运行自动化；
11. 读取 `Summary.json`；
12. 编写开发记录；
13. 检查暂存范围；
14. 输出验证结论和遗留边界。

## 8.3 Agent 禁止

- 直接编辑 `.uasset`；
- 擅自恢复用户改动；
- 手工整理 `__ExternalActors__`；
- 手工整理 `__ExternalObjects__`；
- 因为 Redirector 能解析就认为旧路径可以长期保留；
- 混入与当前 Batch 无关的代码重构；
- 在资产整理阶段接入 GAS。

---

# 9. World Partition 特殊规则

## 9.1 `Lvl_Shooter`

已确认：

```text
Lvl_Shooter
= World Partition 地图
```

因此属于本轮风险最高资产。

目标：

```text
/Game/Variant_Shooter/Lvl_Shooter
        ↓
/Game/Shooter/Maps/Lvl_Shooter
```

## 9.2 `__ExternalActors__` 和 `__ExternalObjects__`

永远不要人工整理：

```text
/Game/__ExternalActors__
/Game/__ExternalObjects__
```

特别禁止：

- 在文件系统里移动；
- 手工重命名；
- 为了目录漂亮人工归档；
- 单独挑选某些 External Actor 移动。

这些内容由 UE 随地图管理。

对于 `Lvl_Shooter`：

> 只移动地图本身，让 Unreal Editor / World Partition 系统管理对应 External Actor/Object。

## 9.3 `Lvl_Test`

已确认：

```text
Lvl_Test
≠ World Partition
```

它仍然和 `Lvl_Shooter` 放在同一个“地图迁移 Batch”中处理，但不需要套用 WP 特殊规则。

目标：

```text
/Game/Variant_Shooter/Lvl_Test
        ↓
/Game/Shooter/Maps/Lvl_Test
```

这样做的原因是：

- 两张地图都有大量硬编码默认路径影响；
- 地图路径同时影响 Config 和 Automation；
- 地图迁移与普通 Blueprint 迁移风险明显不同。

---

# 10. 已知硬编码路径

当前已经确认代码和配置中存在写死资产路径。

每次相关资产移动后必须同步修改。

## 10.1 `ShooterPlayerController.cpp`

存在 HUD 资产路径。

迁移相关 UI 后必须同步修改。

## 10.2 武器 Automation Tests

存在两组武器自动化测试资产路径。

需要检查：

```text
Source/ShootGame/Tests/ShooterWeaponAutomationTests.cpp
```

及实际扫描出的其他测试文件。

## 10.3 网络测试协调器

存在武器路径。

需要检查：

```text
Source/ShootGame/Tests/Network/ShooterNetworkTestCoordinator.*
```

## 10.4 `DefaultEngine.ini`

包含：

- 默认地图；
- Server Default Map；
- GameMode；
- 其他可能的 Shooter 资产路径。

地图迁移后必须同步。

## 10.5 `DefaultEditor.ini`

包含默认地图路径。

地图迁移后同步到：

```text
/Game/Shooter/Maps/Lvl_Shooter
```

## 10.6 自动化脚本

需要检查默认地图：

```text
Scripts/Tests/RunStandaloneSmoke.ps1
Scripts/Tests/RunNetworkSession.ps1
Scripts/Tests/RunAll.ps1
```

以及扫描出的其他脚本。

## 10.7 文档

迁移后检查当前有效文档：

```text
AGENTS.md
Docs/Shooter模板蓝图分析.md
Docs/FirstPerson清理与架构审计.md
Docs/多人网络改造计划.md
Docs/GAS动画分层与预测扩展计划.md
Docs/GAS接入前架构整理执行方案.md
Docs/自动化测试.md
Docs/Agent自动化验证操作手册.md
```

历史开发记录不为了清除旧字符串而修改。

---

# 11. 旧路径残留扫描规则

每个 Batch 完成后搜索：

```text
/Game/Variant_Shooter
```

最终每条结果分类为：

## A. 当前有效路径

必须修改。

## B. 历史文档描述

可以保留。

例如：

> “此前资产位于 `/Game/Variant_Shooter`。”

只要语义明确是历史状态即可。

## C. 历史开发记录

保持不变。

不因为路径迁移修改已完成的历史记录。

## D. Redirector / 当前 Batch 迁移临时状态

只能暂时存在。

提交前尽量处理干净。

## E. 未知

禁止忽略。

继续确认。

---

# 12. 正式执行阶段

# 阶段 0：提交 `CtrlRig_FPWarp`

## 操作

1. 打开 UE Editor；
2. 检查当前 `CtrlRig_FPWarp` 修改；
3. 确认结果正确；
4. Save；
5. 创建对应开发记录；
6. 验证必要的第一人称动画；
7. 单独提交。

建议提交：

```text
动画：调整第一人称 Warp Control Rig
```

## 验收

- Control Rig 修改已独立记录；
- 后续资产迁移提交不再混入行为修改。

---

# 阶段 1：审计 `/Game/Variant_Shooter` 43 个资产

## 目标

只读建立迁移清单，不移动资产。

## 输出表

建议至少形成：

| 当前路径 | 类型 | 用途 | 主要引用者 | Shooter 独占 | 共享 | 目标路径 | 风险 |
|---|---|---|---|---|---|---|---|

## 必须确认

- 哪些是 Blueprint；
- 哪些是 AnimBP；
- 哪些是 Control Rig；
- 哪些是 Input；
- 哪些是 UI；
- 哪些是 FX；
- 哪些是 Data；
- 哪些是地图；
- 哪些实际属于 Shared；
- 哪些依赖根 `/Game/Characters`；
- 哪些依赖根 `/Game/Weapons`；
- 哪些被 `LevelPrototyping` 使用。

## 本阶段禁止

- 移动；
- 删除；
- 重命名；
- 修改 `.uasset`；
- 顺手整理其他目录。

## 验收

43 个资产都有明确分类或明确标记为“待确认”。

---

# 阶段 2：确定最终 Migration Manifest

在阶段 1 审计结果基础上形成最终清单。

形式：

```text
Source:
    /Game/Variant_Shooter/...

Target:
    /Game/Shooter/...

Reason:
    ...

References:
    ...

Text Paths To Update:
    ...
```

如果属于 Shared：

```text
Target:
    /Game/Shared/<AssetType>/...
```

只有 Manifest 确认后才进入实际移动。

---

# 阶段 3：迁移全部 Shooter 非地图资产

## 范围

43 个资产审计后，所有确认属于 Shooter 的非地图资产统一进入第一批迁移。

不包含：

```text
Lvl_Shooter
Lvl_Test
```

不手动碰：

```text
__ExternalActors__
__ExternalObjects__
```

## UE Editor 操作

用户：

```text
建立目标目录
→ 按 Manifest 移动资产
→ Save All
→ Fix Up Redirectors
```

## Agent 后处理

移动完成后：

1. `git status --short --branch`；
2. 扫描旧路径；
3. 更新 C++；
4. 更新武器测试；
5. 更新网络协调器；
6. 更新需要同步的文档；
7. 检查 Asset Registry；
8. `git diff --check`；
9. 运行自动化。

## 验收

- Shooter 非地图资产从新路径可加载；
- 核心 Blueprint 无编译错误；
- AnimBP 无编译错误；
- Control Rig 可加载；
- 武器自动化测试通过；
- Standalone 正常；
- 网络主闭环无回退；
- 日志无找不到 Package；
- 日志无 Blueprint Runtime Error；
- `Lvl_Shooter` 与 `Lvl_Test` 此时仍可暂时留在旧路径。

## 建议提交

```text
资产：迁移 Shooter 非地图资源
```

---

# 阶段 4：迁移地图

两张地图放在一个独立高风险 Batch，但分别按实际地图类型处理。

## 4.1 迁移 `Lvl_Shooter`

来源：

```text
/Game/Variant_Shooter/Lvl_Shooter
```

目标：

```text
/Game/Shooter/Maps/Lvl_Shooter
```

由于是 WP 地图：

- 只通过 UE Editor 移动；
- 不手工移动 External Actor；
- 不手工移动 External Object；
- 保存地图；
- 确认 World Partition 能正常加载；
- 检查地图引用。

## 4.2 迁移 `Lvl_Test`

来源：

```text
/Game/Variant_Shooter/Lvl_Test
```

目标：

```text
/Game/Shooter/Maps/Lvl_Test
```

普通地图按 UE 正常资产移动流程处理。

## 4.3 地图迁移后的 Agent 同步

至少更新：

```text
DefaultEngine.ini
DefaultEditor.ini
DefaultEditorPerProjectUserSettings.ini（如果当前受版本控制且仍维护）
RunStandaloneSmoke.ps1
RunNetworkSession.ps1
RunAll.ps1
Agent自动化验证操作手册.md
自动化测试.md
```

以及全仓扫描发现的其他使用者。

新默认 Shooter 地图：

```text
/Game/Shooter/Maps/Lvl_Shooter
```

## 4.4 地图迁移后的强制搜索

搜索：

```text
/Game/Variant_Shooter/Lvl_Shooter
/Game/Variant_Shooter/Lvl_Test
/Game/Variant_Shooter
```

逐项解释。

## 4.5 验收

### `Lvl_Shooter`

- Editor 可打开；
- World Partition 正常；
- External Actor 没有人工错位；
- Standalone 能加载；
- Dedicated 能加载；
- Listen 能加载；
- 客户端正常进入；
- 网络协调器正常工作。

### `Lvl_Test`

- Editor 可打开；
- 普通地图引用正常；
- 无找不到 Package。

### 完整自动化

七阶段全部通过。

## 建议提交

```text
资产：迁移 Shooter 地图
```

如果实际执行发现 WP 地图产生的变更量过大，也允许拆成：

```text
资产：迁移 Shooter World Partition 主地图
资产：迁移 Shooter 测试地图
```

但不强制预拆。

---

# 阶段 5：清理 `/Game/Variant_Shooter`

这里不是再做一次目录重命名。

前面资产已经直接进入最终路径。

本阶段只负责：

- Fix Up Redirectors；
- 清理旧空目录；
- 检查旧路径残留；
- 确认没有有效资产继续依赖 Variant 路径。

## 最终目标

```text
/Game/Variant_Shooter
→ 不再作为有效资产根存在

/Game/Shooter
→ Shooter 正式 Content 根
```

## 操作

1. Content Browser 显示 Redirector；
2. Fix Up；
3. Save；
4. Asset Registry 复查；
5. 全仓库扫描；
6. 检查 C++；
7. 检查 Config；
8. 检查测试；
9. 检查脚本；
10. 检查当前有效文档。

## 验收

- 运行态无 `/Game/Variant_Shooter` 依赖；
- Config 无旧路径；
- Tests 无旧路径；
- Script 无旧路径；
- C++ 无旧路径；
- 当前有效文档已更新；
- 历史记录保持历史原文；
- 完整自动化通过。

## 建议提交

```text
清理：移除 Variant_Shooter 旧资产路径
```

---

# 阶段 6：冻结 Shooter Content 基线

## 6.1 最终 Shooter 结构

预期：

```text
/Game/Shooter/
├── Blueprints/
│   ├── Framework/
│   ├── Characters/
│   ├── AI/
│   └── Weapons/
├── Animation/
│   ├── FirstPerson/
│   ├── ThirdPerson/
│   └── Rigs/
├── Data/
├── Input/
├── UI/
├── FX/
└── Maps/
    ├── Lvl_Shooter
    └── Lvl_Test
```

实际审计如果证明某一类资产确实值得新增：

```text
Materials/
Audio/
Meshes/
Textures/
```

则按实际结果补充。

文档不能为了匹配预设树，强迫资产进入错误目录。

## 6.2 最终 Shared 结构

按实际资产建立：

```text
/Game/Shared/
├── Materials/
├── Textures/
├── Meshes/
├── Animation/
├── FX/
└── Audio/
```

只有真正存在共享资产的类型才需要创建目录。

## 6.3 长期目录职责

最终明确：

```text
/Game/Shooter
= Shooter 正式玩法资产

/Game/Shared
= 多个长期目录共同使用的项目级共享资产

/Game/Characters
= 当前模板/共享角色 Art 与动画资源

/Game/Weapons
= 当前模板/共享武器 Art

/Game/LevelPrototyping
= 长期开发/测试/原型资源库
```

---

# 13. 文档更新策略

## 13.1 `AGENTS.md`

增加 Content 导航和资产操作原则。

建议规则：

```text
## Content 资产规则

- Shooter 正式资产根为 /Game/Shooter。
- 共享资产只有在被多个长期目录实际使用时才进入 /Game/Shared。
- 资产整理先审计依赖，再执行迁移。
- 不根据文件名判断资产是否无用。
- 每次只处理一个明确迁移 Batch。
- .uasset 必须通过 Unreal Editor 资产系统移动。
- 移动后必须 Save All 并 Fix Up Redirectors。
- World Partition 的 __ExternalActors__ 和 __ExternalObjects__ 禁止人工整理。
- 核心 Blueprint、AnimBP、Map 迁移后必须重新验证。
- 无法证明可以删除的资产默认保留。
```

## 13.2 《FirstPerson 清理与架构审计》

保留历史事实。

补充最终状态：

- 2026-08-14 从 FirstPerson 迁出的资产当时进入 `/Game/Variant_Shooter`；
- 后续在独立 Content 目录整理任务中再次迁入新的正式路径；
- 不改写当时的审计结论。

## 13.3 《GAS 接入前架构整理执行方案》

保持为历史执行方案。

旧方案中的：

```text
不把 /Game/Variant_Shooter 大规模改名为 /Game/Shooter
```

仍然是当时那个架构整理阶段的真实边界。

只在最终状态补充：

> 架构整理完成后，项目另执行《Shooter Content Browser 资产目录整理执行方案》，最终将 Shooter Content 正式根收束到 `/Game/Shooter`。

不回写历史计划。

## 13.4 《GAS、动画分层与客户端预测扩展计划》

更新当前实际资产路径，并明确：

> GAS Phase 1 基于新的 `/Game/Shooter` Content 基线开始。

## 13.5 自动化相关文档

默认地图统一更新为：

```text
/Game/Shooter/Maps/Lvl_Shooter
```

---

# 14. 自动化验证

完整入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 `
    -Port <未占用端口>
```

必须检查：

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

全部：

```text
Passed
```

## 14.1 不能只看退出码

仍按照现有 Agent 自动化 SOP：

- 顶层 status 为 Passed；
- Automation 实际执行了测试；
- failed = 0；
- Dedicated 客户端成功数正确；
- Listen 成功；
- Emulated 确实启用了延迟/丢包；
- Disconnect 成功；
- 没有 `AUTOMATION_TEST_FAILURE`；
- UnrealEditor-Cmd 进程全部回收。

---

# 15. 人工验证

自动化使用 NullRHI，不能证明最终表现质量。

目录迁移完成后进行一次带渲染检查。

## 15.1 第一人称

检查：

- 第一人称手臂；
- 武器 Mesh；
- `CtrlRig_FPWarp`；
- 第一人称 AnimBP；
- 枪口表现；
- HUD；
- 声音。

## 15.2 第三人称

检查：

- Rifle / Pistol AnimBP；
- 武器附着；
- Aim Offset；
- 远端俯仰；
- 开火 Montage；
- NPC 武器表现。

## 15.3 地图

### `Lvl_Shooter`

检查：

- World Partition；
- 场景 Actor；
- 材质；
- PlayerStart；
- Pickup；
- AI；
- 网络入口。

### `Lvl_Test`

检查：

- 普通地图正常加载；
- 测试对象无丢失。

---

# 16. 开发记录与提交

每个正式提交都必须有对应开发记录。

记录包含：

- 计划提交说明；
- 改动目的；
- 实际改动；
- 验证结果；
- 遇到的问题；
- 处理方式；
- 遗留项。

没有问题时明确：

```text
无
```

提交前：

```powershell
git diff --cached --name-only
```

确认：

- 开发记录已暂存；
- 当前 Batch 文件完整；
- 没有无关用户改动。

---

# 17. 推荐提交序列

## A. 当前动画修改

```text
动画：调整第一人称 Warp Control Rig
```

## B. 迁移计划

```text
文档：制定 Shooter Content 资产迁移方案
```

## C. 非地图资产

```text
资产：迁移 Shooter 非地图资源
```

## D. 地图

默认：

```text
资产：迁移 Shooter 地图
```

若实际发现 WP 主地图变化量明显过大，可以拆为：

```text
资产：迁移 Shooter World Partition 主地图
资产：迁移 Shooter 测试地图
```

## E. 清理旧路径

```text
清理：移除 Variant_Shooter 旧资产路径
```

## F. 冻结基线

```text
文档：冻结 Shooter Content 基线
```

## G. 之后才进入 GAS

```text
GAS：接入 Gameplay Ability System 模块
```

---

# 18. 失败处理

## 18.1 非地图资产迁移失败

优先检查：

1. Redirector；
2. Soft Reference；
3. C++ Asset Path；
4. Config；
5. Blueprint Class Reference；
6. AnimBP Reference；
7. Automation Path；
8. Asset Registry。

不要顺手扩大为 Gameplay 重构。

## 18.2 `Lvl_Shooter` 迁移失败

由于是 WP 地图：

禁止：

- 手动补 External Actor 文件；
- 文件系统移动 External Actor；
- 局部拼接二进制资产。

优先：

1. UE Editor 检查地图；
2. 检查 World Partition；
3. 检查 Redirector；
4. 检查地图默认路径；
5. 必要时回退整个地图迁移 Batch。

## 18.3 `Lvl_Test` 迁移失败

按普通地图资产路径问题处理：

- Redirector；
- Config；
- Script；
- Reference；
- Package Path。

---

# 19. 最终完成标准

只有同时满足以下条件，才能宣布 Content 整理完成。

## 19.1 目录

- `/Game/Shooter` 成为 Shooter 正式资产根；
- 资产主要按类型组织；
- Blueprint 在 `Blueprints/` 内再按 Gameplay 职责细分；
- 第一/第三人称动画职责清晰；
- `Lvl_Shooter` 和 `Lvl_Test` 位于 `/Game/Shooter/Maps`；
- `/Game/Variant_Shooter` 不再承担有效运行入口。

## 19.2 Shared

- `/Game/Shared` 长期保留；
- Shared 按资产类型组织；
- Shared 只包含真正跨长期目录共享的资产；
- 不存在“因为不知道放哪而丢进 Shared”的资源。

## 19.3 长期保留目录

职责明确：

```text
/Game/Characters
/Game/Weapons
/Game/LevelPrototyping
/Game/Shared
```

均不因为本轮目录整理被无意义大搬迁。

## 19.4 引用

- C++ 硬编码路径已更新；
- Config 已更新；
- Automation 已更新；
- 网络测试协调器已更新；
- Scripts 已更新；
- 当前有效文档已更新；
- 历史记录不被改写。

## 19.5 World Partition

- `Lvl_Shooter` 从新路径正常加载；
- External Actors 由 UE 管理；
- 未人工整理 `__ExternalActors__`；
- 未人工整理 `__ExternalObjects__`。

## 19.6 Gameplay

现有 Shooter 主闭环无回退：

- 武器拾取；
- 武器复制；
- 切枪；
- 弹药；
- 服务器权威开火；
- 弹丸；
- 伤害；
- 生命；
- 死亡；
- 复活；
- PlayerState；
- GameState；
- 第三人称远端表现；
- 断线清理。

## 19.7 自动化

```text
Build              Passed
Automation         Passed
Standalone         Passed
DedicatedNetwork   Passed
ListenNetwork      Passed
EmulatedNetwork    Passed
DisconnectCleanup  Passed
```

## 19.8 Git

- `CtrlRig_FPWarp` 行为修改与资产迁移分离；
- 每个新提交有独立开发记录；
- 没有误提交 `Saved/`；
- 没有误提交 `Intermediate/`；
- 没有误提交本地 Agent 日志；
- 工作区不存在未解释改动。

---

# 20. GAS 接入门槛

只有 Content Freeze 完成后才进入 GAS。

条件：

```text
源码架构稳定
+
/Game/Shooter 正式目录稳定
+
/Game/Shared 职责稳定
+
地图新路径稳定
+
旧 Variant_Shooter 运行路径清理完成
+
Shooter 网络闭环完整通过
+
工作区无未解释改动
```

之后按照 GAS 计划：

```text
阶段 0：重新冻结并验证当前基线
阶段 1：只接入 GAS 模块
```

第一笔 GAS 提交不顺带：

- 重做资产目录；
- 迁生命；
- 迁开火；
- 大改 Blueprint；
- 实现背包；
- 实现客户端预测。

---

# 21. Agent 执行摘要

每一个迁移 Batch 默认：

```text
1. 读取 AGENTS.md
2. 读取本文
3. git status --short --branch
4. 确认用户已有改动
5. 检查本 Batch Manifest
6. 用户在 UE Editor 中完成资产移动
7. 用户 Save All / Fix Up Redirectors
8. Agent 扫描旧路径
9. Agent 修改 C++ / Config / Tests / Scripts / Docs
10. git diff --check
11. 运行对应快速测试
12. 运行完整 RunAll.ps1
13. 读取 Summary.json
14. 完成开发记录
15. 检查暂存范围
16. 报告验证结果
```

核心职责分界：

```text
UE Editor
= 真正进行 .uasset / Map 迁移

Agent
= 规划、文本同步、审计、验证、记录
```

---

# 22. 最终执行链

```text
保留并独立提交 CtrlRig_FPWarp
        ↓
审计 /Game/Variant_Shooter 全部 43 个资产
        ↓
确定最终 Migration Manifest
        ↓
建立 /Game/Shooter 资产类型目录
        ↓
迁移全部 Shooter 非地图资产
        ↓
Fix Redirectors
        ↓
完整验证
        ↓
迁移 Lvl_Shooter（World Partition）
        ↓
迁移 Lvl_Test（普通地图）
        ↓
同步所有地图硬编码路径
        ↓
完整验证
        ↓
清理 /Game/Variant_Shooter
        ↓
重新扫描所有旧路径
        ↓
更新文档
        ↓
冻结 Shooter Content 基线
        ↓
GAS Phase 1
```

最终形成明确边界：

```text
Content Freeze 以前
= 现有 Shooter 工程与资产治理

Content Freeze 以后
= GAS 与后续 Gameplay 架构扩展
```
