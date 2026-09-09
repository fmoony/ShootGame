# NewAnimBlueprint 第三人称动画复刻

- 日期：2026-09-08
- 计划提交说明：`功能：扩展 MCP 动画图复制并建立第三人称 Rifle 动画副本`
- 变更类型：插件代码 / 测试 / 文档 / 资产

## 目的

用户验收普通蓝图 MCP 编辑后，要求把新建的 `/Game/Shooter/Blueprints/NewAnimBlueprint` 改为第三人称 AnimInstance 父类，并复刻 ABP_TP_Rifle 中有用的连线、状态机及必要配置。

## 本轮完成内容

- 新资产最初是 ABP_FP_Copy 的子 AnimBP，仅有 3 个初始 EventGraph 节点，无自身 AnimGraph；先通过 MCP 编译保存用户新建资产。
- 新增 `copy_anim_blueprint`：限定同骨架、原生 AnimInstance 父类、空目标；先经 Asset Tools 复制保存目标备份，再在原目标对象内复制图表。目标路径不变，未用文件系统替换或移动 uasset。
- 复制 FunctionGraphs / UbergraphPages 及其内部图、状态转换、缓存姿势、变量和 40 项非瞬态默认值；通过 DuplicationSeed 和引用替换映射源 Blueprint / 生成类 / CDO。
- 动画父类设置为 `/Script/ShootGame.ShooterThirdPersonAnimInstance`，目标骨架为 `/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin`。
- 保留移动和跳跃状态机、ArmedAim / Reload 状态机、ArmedAimPose 缓存姿势、Arms Slot、Layered Blend Per Bone、AimOffset、Aim / LeftHand IK、Reload 躯干俯仰、ReloadRecovery 通知。
- 两条 Reload 规则沿用现有基线：进入为 `bIsReloading && !bReloadPresentationRecovering`，退出为 `!bIsReloading || bReloadPresentationRecovering`。动画只负责第三人称表现，不提交弹药，不结束服务器 Ability。
- 清理副本中的旧 Character / MovementComponent 初始化和无消费者的校准计算；父类 C++ 负责移动、瞄准、Tag 和 IK 数据更新。
- ArmedAim 内旧 Sequence / AimOffset / AimPitchN 链没有连接到状态输出，实际由 ArmedAimPose 缓存输出；补充清理这 3 个遗留节点，顶层缓存姿势链保持。
- `get_blueprint` 增加 Skeleton 和唯一 graphPath，支持两张同名 Transition 图的精确读取；`get_node_details` 增加可编辑节点参数；动画编辑器支持实际图表截图。
- 新增专项脚本 `Scripts/Tests/TestMcpAnimBlueprintCopy.py`，比较所有嵌套图、节点及引脚逻辑连接、可编辑动画结构参数和默认值，显式参数控制副本清理与保存。

## 实现依据

- Epic [Animation Blueprints](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprints-in-unreal-engine) / [Graphing](https://dev.epicgames.com/documentation/unreal-engine/graphing-in-animation-blueprints-in-unreal-engine)：AnimGraph、EventGraph 分别负责姿势输出和事件逻辑；当前网页为 5.8，未据此升级项目。
- Epic [FObjectDuplicationParameters](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/FObjectDuplicationParameters) / [StaticDuplicateObject](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/StaticDuplicateObject)：对象层级复制及引用重映射。
- 对照本机 UE 5.6 `UObjectGlobals.h` 的 DuplicationSeed / CreatedObjects、`UAnimBlueprint` 的 Skeleton / Groups / PreviewMesh、`BlueprintEditorLibrary.cpp` 的 ReparentBlueprint，以及 Animation Blueprint Editor 继承 FBlueprintEditor 的实际声明。
- 项目源码核对 `ShooterThirdPersonAnimInstance.h` 和 `Rifle第三人称换弹动画实施计划.md`；CodeGraph 未能正确返回该 AnimInstance 文件，随后按限定路径读取源码。新增插件编辑不依赖归档插件，不新增模块。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：编译成功。仅编辑既有插件源文件，无新模块、依赖或源码文件结构变化，不需要再次刷新 VS 项目。
- 首次复制编译 0 Error / 0 Warning，复制 179 个内部对象；副本清理后保存编译仍为 0 Error / 0 Warning。
- 首轮全结构专项 Passed，20 张图、99 个保留节点参数和连线与源一致，40 项默认值复制；报告 `Saved/MCP/AnimBlueprintCopyValidation.before-reload.json`。
- 编辑器正常重启后，从已保存资产重新读取全部图表和默认值，99 节点专项仍为 Passed，报告 `Saved/MCP/AnimBlueprintCopyValidation.reload.json`。
- 最终补充清理 ArmedAim 的 3 个无消费者节点后再次保存编译：0 Error / 0 Warning；20 张图、96 个剩余节点全部通过参数及连线核对，报告 `Saved/MCP/AnimBlueprintCopyValidation.json`。合计去除 19 个遗留节点，未移除状态或转换。
- 非空目标拒绝覆盖、AnimBP 拒绝改成 Actor 父类、同名 Transition 短名称拒绝误选三项检查通过：`Saved/MCP/AnimCopyGuards.json`。
- 已查看 AnimGraph、Locomotion、WeaponAction、Reload、EventGraph 五张实际 Slate 截图，图表显示正常；截图索引 `Saved/MCP/AnimBlueprintScreenshots.json`。
- `Scripts/Tests/RunAll.ps1 -Port 17847`：Build Passed；Automation 80 成功、13 带警告成功、1 失败，唯一失败仍是 `ShootGame.Equipment.Presentation.AnimClassMapping`，断言 `Rifle FP AnimClass matches baseline`。总控停止，其余五阶段未执行，没有运行网络客户端。
- 本轮完整回归报告：`Saved/Automation/Runs/20260908_175054/Summary.json`；用例报告：`Saved/Automation/Reports/20260908_175056_ShootGame/index.json`。
- ABP_TP_Rifle 前后 SHA256 相同：`F3B00563724AFD288BB9BAA025AACDAA08FA72060C93E0EFB15FAA7720E3BA38`。

## 遇到的问题及处理

- 新建 AnimBP 继承了第一人称 AnimBP，旧 MCP 仅支持 Actor 父类和标准 BlueprintEditor。扩展了合法 AnimInstance 父类和动画编辑器分支。
- 状态机存在两张名为 Transition 的嵌套图。图读取/选择改为支持唯一相对路径，重名短名称不再返回第一张图。
- 源图存在已被缓存姿势和 C++ 数据源替代的遗留节点；以实际引脚消费者和父类代码确认后只清理副本。
- 全量回归失败与上一轮相同，未改动第一人称动画基线或用户其他资产以绕过断言。

## 遗留项与边界

- 新副本尚未替换正式角色 / 武器中的 AnimClass；当前验证是资产复制、编译、节点配置和图表检查，不宣称已通过实机运动、换弹和多人网络视觉验收。
- 原始目标备份位于 `/Game/Shooter/Blueprints/NewAnimBlueprint_BeforeAnimCopy`；保留供用户回退。
- 未改动源 ABP_TP_Rifle、正式 Character 配置或用户已有 ABP_FP_Pistol 修改。未暂存、未提交。
- 通用动画重定向、接口 / 子 AnimBP 的父资产覆盖、任意非空图表合并不属于本次复制接口的支持范围。
