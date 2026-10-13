# Rifle 收枪动画表现

维护日期：2026-10-13；适用 UE 5.6.1。
用户已验收 Local 方案 2 与临时 K 网络动画预览；本功能只改变动画表现。

## 当前正式方案

正式图为 `/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player`。
父类保持运行时 ShooterThirdPersonAnimInstance，不依赖实验 AnimInstance。
WeaponAction 的 ArmedAim 内沿用 RifleReady：ArmedAim ↔ AimTransition ↔ LowReady。

AimTransition 使用 MM_Rifle_AimToLowReady_CorrectedGrip，LowReady 使用原低位动画。
动态 ArmedAimPose 是分层底姿态，肩臂按原 RifleReloadWeightedUpperBody Mask 做 Local 混合。
运行时 ShooterRifleReadyLayer 保持子播放器相关，姿态 Alpha 按真实 t/T 渐进。
正式三个选片槽均引用 CorrectedGrip；Down30 / Down60 保留作回退，不用于当前选片。
不接入方案 3 的额外反向边界混合；原播放时长约 0.833333 秒，速率仍为 ±1。
中途反向保持当前累计时间，实时改俯仰不会重置动画或切换素材。

当前 Rifle 素材目录：

```text
/Game/Characters/Mannequins/Anims/Rifle
    MM_Rifle_AimToLowReady_CorrectedGrip
    MM_Rifle_AimToLowReady_Down30
    MM_Rifle_AimToLowReady_Down60
```

原始 Mixamo、ArmedAim、AO_Rifle、Skeleton、Socket 和 Blender 调整输入保留。
本轮未调整 Mask 权重、Aim IK、LeftHand IK、FP Proxy 或相机层级。
本地 FP 沿用 CopyPose 和现有净空机制，远端只消费自己的 TP 动画实例。

## K 蓝图与临时网络接口

输入位于 BP_ShooterCharacter 的 EventGraph：K Pressed → ToggleDebugRifleLowReady(Self)。
该入口要求本地控制及有效 Rifle；按住不重复触发，过渡中再次按 K 可以反向。

Standalone 直接切换本地目标；多人模式委托 ShooterRifleReadyPreviewComponent。
拥有端提交可靠 Server Toggle，服务器检查现有武器和动作限制，再复制姿态目标 bool。
Owner / Observer 各自推进原播放器；Dedicated Server 仅保存目标，不要求渲染动画。
原 Fire、Reload、Equipping、Dead、Montage 等边界仍可中断，临时接口不禁止任何 Gameplay 动作。
组件不修改 GAS Tag、Ammo、射速、Hit、Damage、装备规则或权威射击结果。

只同步目标，不同步逐帧时钟；网络 K 等服务器确认，动画阶段可能存在延迟。
快速目标可被属性复制合并；晚加入、跨机器与极端延迟尚未专项验收。
不能用本次动画验收替代完整射击预测与反馈四路径验证。

快速关闭：各进程执行 `ShootGame.RifleReady.NetPreview 0`；恢复用 1。
彻底移除：删除 PreviewComponent h / cpp，撤销 Character 构造挂点和 AnimInstance 网络桥接。
K 蓝图、Local 节点及素材仍可保留；详细范围见历史[网络预览报告][net]。

## 调整工具与回归

Blender 工具与必要输入见[工具链文档](Blender动画编辑工具链.md)。
正式图的三个素材字段可在 Class Defaults 中查看，Local 节点也保留编辑器外壳。
旧分段 ProductionApply 已移除，避免重新覆盖正式选片；不再提供一次性正式接入工具。
保留 `Run.ps1 -Mode Measure -Production` 作为内存只读测量入口，不保存生产资产。
analyze_segmented.py 用于保留的分段回退夹具，不把旧阈值检查作为 Local 方案验收。

```text
Automation RunTests ShootGame.Animation.RifleReady
```

五项回归覆盖 Standalone、Listen、Dedicated、延迟丢包及关闭网络开关的本地对照。
网络夹具使用真实 NetDriver、拥有连接、K 蓝图输入与原 Fire / Reload 入口。
纯 K 在固定权威武器上检查 Shot / Ammo 增量为 0，不构造新的 Gameplay 状态。

## 手动检查

在正式 Lvl_Shooter 设置 2 Players / Listen Server，双方装备 Rifle。
一端按 K，从另一端观察收枪、抬枪、中途反向及连续改变俯仰；再交换操作者。
本地检查第一人称肩臂和枪械可见性，再触发原 Fire / Reload 确认动作仍可执行。
Dedicated 对照选 Play As Client、2 Players；主观画面不由无头 Automation 判定。

一次性 Local / Mesh / Boundary 原型已清理，只保留 Segmented50 回退地图及必要依赖。
该地图仍用于素材和 Natural Pitch Mask 对照，不代表正式 Local 方案或真实 FP 角色。
历史报告原文保留，已删除的截图、JSON、CSV 和脚本路径可能失效，见[验证索引][index]。

[net]: 验证/RifleReadyNetworkPreview_20261013/实施与验证.md
[index]: 验证/README.md
