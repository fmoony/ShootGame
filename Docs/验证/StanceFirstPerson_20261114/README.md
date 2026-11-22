# Stance 第一人称恢复与运行时定位

日期：2026-11-14；UE 5.8.4；目标为原 Stance 隔离地图。

## 实际改动

取消头部前方 26 cm、上方 5 cm 的独立眼点及其平滑。
取消双肩前移 47 cm 和围绕独立眼点旋转的 FP 专用处理。
相机直接附着 FP head，并读取 BP_ShooterCharacter 的实际相机设置。
实际头部局部偏移为 (-2.8, 5.89, 0) cm，世界及 FP FOV 均为 90°，FP Scale 为 1。
Scale 以蓝图实际值为准，不使用 C++ 构造默认的 0.6，也不保留实验的 0.45。

FP 使用正式角色配置的完整 Manny 网格，不再叠加裁切手臂与腿两套显示组件。
历史 SKM_FP_Arms、SKM_FP_Legs 资产保留，未修改或删除。
FP 在脊柱瞄准、持枪恢复和左手 Grip 完成后，复制本帧最终 TP 姿态。

夹具仍为 PoseableMesh，使用同帧骨骼数组复制对齐正式 Copy Pose 的普通姿态语义。
未移植正式 AnimBP、Linked Layer、Arms Montage 或 CharacterMovement。
未改生产角色、正式蓝图、动画轨道、实验地图或 Blender 文件。

## 采样与文件

Before.csv.gz 是取消补偿前实际运行的基线。
ProductionView.csv.gz 是完整正式 FP 网格恢复后的实际运行。
两版使用相同控制输入与 60 Hz 固定时间步，每六帧记录一次。
每版覆盖 108 个场景、2,160 个采样帧，不以单段动画静态求值替代实际组件。

场景包括站立、蹲姿和前后左右蹲走，分别取 Aim / LowReady。
每组覆盖 -85、-55、-30、0、30、55、85 度俯仰，另外覆盖平视正负 60 度偏航。
每场景持续两秒，稳定统计剔除最初约 0.5 秒。
idle 仅覆盖该场景的前两秒，不把本轮称为完整 idle 周期或启停过渡验收。

记录 raw、spine、held、final 四个处理阶段。
另外从实际 Body / FP 读取九个骨骼的世界位置及旋转，并记录相机、双枪及枪口。
旧版还包含裁切腿网格的九条记录；最终版每采样帧为 59 行。
旧版 BeginPlay 的 case=-1 保留在原始文件，不纳入尚未设定输入的受控场景。

主要文件：

- Before.summary.json、ProductionView.summary.json：前后逐场景统计与处理阶段差值。
- Before.csv.gz、ProductionView.csv.gz：实际运行变换。
- Protection.json：上一轮保护清单的 21 项资产哈希仍一致。
- Build.log：最终单 FP 网格版本构建成功。
- Runtime.log：实际相机设置、108 场景结束与正常退出记录。
- FormalGraph.json：正式 TP 分层、ShooterAimIK 和 FP Copy Pose 节点的只读回读。
- Manifest.json：本轮夹具、脚本和原始运行 CSV 的 SHA256。
- Images：最终版本六张 D3D12 辅助画面。

几何投影按 16:9、90° FOV 计算，不包含后处理或逐像素可见性判断。
gun_screen_x_ndc 指枪械挂点的横向坐标，不是整支枪的屏幕包围盒或中心。
第一人称 Scale 不能单独解释横向偏中，需结合相机相对变换判断。

## 数值结果

最终 FP 与 Body 的最大位置差为 0 cm，旋转差小于 0.000004°。
最终实际 Body 与处理阶段 final 的位置差为 0 cm。
前后 Body 最大位置差为 0 cm，旋转差小于 0.000004°。
相机恢复为固定 head 局部偏移，实际运行读回与正式配置一致。
这些检查通过仅说明同帧姿态复制及 TP 隔离成立，不代表构图或遮挡通过。

## 已定位原因

### 来源头肩关系仍随姿态改变

平视 Aim 稳定阶段的典型值：

| 项目 | 站立 | 蹲姿 |
| --- | ---: | ---: |
| 枪械挂点前向距离 cm | 13.40 | 6.06 |
| 枪械挂点右向距离 cm | 11.31 | 1.28 |
| 右手骨骼前向距离 cm | 6.21 | -1.13 |
| 右锁骨右向位置 cm | -3.52 | -8.42 |

前向距离以眼点为零，负值表示位于相机后方。
蹲姿右手接近或越过眼点，枪械挂点靠近相机，因此仍可能贴枪或看到枪体内部。
最终画面仍需结合几何遮挡判断，骨骼点不等价于蒙皮表面。

raw 阶段已经带入来源躯干、头部和锁骨变化。
spine 到 held 阶段没有移动锁骨；右手最大位置变化仅约 0.00362 cm。
这与当前代码只恢复上臂、前臂及手的组件旋转一致，没有恢复站立头肩相对位置。
因此原有完整上身来源仍决定持枪起点，取消 FP 偏移并不能补齐正式上身分层。
这不是 FP 与 TP 复制不同步：最终两者实际骨骼一致。

### 俯仰处理范围与相机不同

相机接受正负 85° 俯仰，实验 AimSpines 仍将俯仰夹在 -45° 到 55°。
恢复复制后，FP 继承这条受限的 TP 瞄准结果。
Aim 站立的枪口与视线夹角，平视约 14.42°、-85° 约 52.45°、+85° 约 19.66°。
旧版独立旋转 FP 肩部时会掩盖这种处理范围差异。

实验仅运行既有脊柱分配、持枪旋转恢复和左手 Grip。
没有执行正式 AnimGraph 的 ShooterAimIK 手部枪口修正。
正式图与实验流程仍需区分，不能将恢复相机等同于恢复完整正式动画链路。

### 网格问题单独判断

初次位置恢复后仍使用两套裁切网格，存在既有切口与重叠风险。
最终改为正式完整 Manny，消除该实验显示路径与生产基线的差异。
这不改变上述头肩位置和俯仰数值，也不自动证明所有近裁切问题已解决。
最终 017 蹲姿画面仍明显贴近枪体，031 蹲走也存在枪械过近，视觉未通过。

## 原因追溯

2026-10-22 的 FP 调整为缓解低头时身体和枪械相交，并经多轮截图校准。
2026-11-09 的资源替换记录明确保留 47 cm 参数，低头切口没有通过。
没有找到 26 cm、47 cm 的定量选值对比或正式站立构图一致性验收。
相关历史文件保留原貌，本轮仅补充当前实现和新证据。

## 复测

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureView -ViewOutput 'StanceView/New.csv'
$engine = & './Scripts/Development/ResolveShootGameEngine.ps1'
$python = "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
& $python './Scripts/Animation/Stance/analyze_view.py' './Saved/StanceView/New.csv' --require-copy
~~~

需要对比时额外传入 --baseline，前后必须采用相同控制输入和时间步。
CaptureView 加 ViewImages 可生成六个代表场景的截图，目标已有数据时拒绝覆盖。
本轮辅助渲染以 10 Hz 固定时间步缩短渲染量，不将其数据混入 60 Hz 前后门禁。

## 依据与边界

正式蓝图已通过官方 MCP 读回：ABP_FP_Player 为 Copy Pose From Mesh 接 Arms Slot。
Copy Pose 使用附着父组件，实际角色选择 ABP_FP_Player。
本地 UE 5.8 CameraComponent.cpp 已核对 head 附着后的取景和 Pawn 控制旋转行为。
[官方 Copy Pose 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/copy-a-pose-in-unreal-engine)
用于核对复制语义；本实验使用 PoseableMesh 数组复制，不声明原生节点已执行。

后续修复对象为来源头肩关系、上身分层和实验瞄准链路。
本轮没有继续修改这些动画或瞄准求解，也没有恢复 C++ 接地补偿。
支撑期踮脚和启停约 2.92 cm 穿地仍待处理，不因本轮 FP 门禁通过而变为通过。
未验证正式网络、换弹、贴墙、Cook、坡面、台阶或完整滑铲回归。
