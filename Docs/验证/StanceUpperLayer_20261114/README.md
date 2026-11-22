# 下蹲完整上身分层与运行验证

UE 5.8.4；数值采集于 2026-11-14，补充外部视角与整理于 2026-11-15。
目标为原 Stance 隔离实验，不接入生产角色或网络下蹲。

## 实现

骨盆和双腿使用现有 Blender 动画及方向混合。
完整上身来自实验 Stand Aim / LowReady，不再只恢复双臂旋转。
分层在普通姿态切换混合后执行，FP 继续复制最终 TP 姿态。
未恢复眼点前移、47 cm 双肩迁移或 C++ 接地补偿。

通过官方 MCP 核对正式 ABP_TP_Player 的分层节点：

- Branch Filter：spine_01，Blend Depth=1，权重为 1。
- Mesh Space Rotation Blend=true；Mesh Space Scale Blend=false。
- 正式上身层位于武器姿态链路，后续另有 ShooterAimIK。

实验调用同一引擎底层 FAnimationRuntime::BlendPosesPerBoneFilter。
使用 CreateMaskWeights 生成骨骼权重，映射 Skeleton 与实际网格的骨骼索引。
旋转按 Mesh Space 混合，位移按引擎规则在 Local Space 混合。
因此覆盖躯干、锁骨、头颈及双臂，同时保持骨盆和腿部输入。

与正式实现的差异：夹具仍使用 PoseableMesh，未改建成完整 AnimBP 或 Linked Layer。
上身来源为既有 Stand 片段，保留原脊柱瞄准与左手 Grip，没有接入正式 AimOffset / AimIK。
Mode 1、2 使用新分层；滑铲不在本轮分层验收范围。
动画资产、Blender 文件、正式蓝图和实验地图均未保存或修改。

[官方分层说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-layered-animations-in-unreal-engine)
与正式节点参数一致。
本地 UE 5.8 的 AnimationRuntime.h、AnimNode_LayeredBoneBlend.cpp 已核对调用签名和空间规则。

## 上身验证

Layer.csv.gz 为实际运行的阶段姿态、Body、FP、相机及枪械变换。
固定 60 Hz，每六帧记录一次，共 108 场景、2,160 个采样帧。
覆盖站立、蹲姿、前后左右移动、Aim / LowReady，以及七个俯仰角和正负偏航。
每场景两秒；稳定几何比较剔除最初约 0.5 秒，不构成完整切换过程验收。

参考直接取同一轮实际站立组件，匹配持枪状态、俯仰与偏航。
没有用 LayerUpperBody 内部算出的预期姿态充当通过证据。
比较躯干、头、双肩、双臂、双手以及枪械挂点和枪口的相机空间位置。

结果：

- 上身与站立参考的最大位置差，在 CSV 记录精度内为 0 cm。
- 最大旋转差小于 0.000004°，门禁为 0.01 cm / 0.01°。
- FP 与 Body 的最大位置差为 0 cm，旋转差小于 0.000003°。
- 站立和骨盆相对上一轮没有位置变化。
- 分层改变了预期的蹲姿上身，不能再使用“整个 TP 完全不变”作为本轮门禁。

平视 Aim 的实际典型值：

| 项目 | 分层前蹲姿 | 分层后蹲姿 | 站立参考 |
| --- | ---: | ---: | ---: |
| 枪械挂点前向距离 cm | 6.06 | 13.40 | 13.40 |
| 枪械挂点右向距离 cm | 1.28 | 11.31 | 11.31 |
| 挂点横向 NDC | 0.2110 | 0.8441 | 0.8441 |

挂点不是整支枪的屏幕中心；投影按 16:9、90° FOV 计算。
数据说明蹲姿额外带来的头肩与枪械相对位置偏差已消除，不表示所有视角自然度通过。

## 下肢独立对照

Lower.csv.gz 为同样 60 Hz 下实际 Body 的 264 场景、43,808 帧运行。
对照既有 Blender 接地轮的 RuntimeFinal.csv.gz，逐行核对输入和采样时刻。
检查骨盆、左右 thigh / calf / foot / ball 及 heel 参考标记。
共比较 481,888 条下肢记录，组件空间及世界空间位置差均为 0，旋转差为 0。

这证明新分层没有改变原下肢动作，不证明脚掌平贴、脚滑或穿地通过。
heel 仍为骨骼参考标记，不是鞋底逐顶点碰撞或地面 Trace。
既有踮脚和启停约 2.92 cm 穿地继续保留为未解决项。

## 渲染与边界

Images 保留六个代表性第一人称场景，固定 10 Hz 辅助渲染。
平视的站立、蹲姿和前进蹲走已恢复相同持枪构图。
这些画面用于补充蒙皮观察，不混入 60 Hz 数值门禁。
外部视角保存在 External，用于观察完整躯干与下肢衔接。

极端俯仰仍有站立共有的问题。
相机可达正负 85°，原 AimSpines 仍限制在 -45° 到 55°，本轮没有改变。
平视枪口与视线夹角约 14.42°，-85° 时约 52.45°，+85° 时约 19.66°。
这些值与站立参考一致，不属于分层后的额外偏差，也未标为瞄准通过。

未运行正式网络、换弹、贴墙、Cook、坡面、台阶或整套滑铲回归。
脚掌平贴需要继续修改 Blender 动画，完整瞄准链路需另行接入。

## 文件与复测

- Layer.summary.json、Layer.csv.gz：上身与 FP 的实际运行数据。
- Lower.comparison.json、Lower.csv.gz：全部下肢逐帧对照及实际运行输入。
- FormalLayer.json：正式分层节点的只读参数回读。
- Protection.json、Manifest.json：保护资产及本轮代码、数据的哈希。
- Build.log、TextLayout.log：构建和版式结果。
- Images、External：本轮第一人称与外部视角辅助画面。

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureView -ViewOutput 'StanceLayer/New.csv'
$engine = & './Scripts/Development/ResolveShootGameEngine.ps1'
$python = "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
& $python './Scripts/Animation/Stance/analyze_view.py' './Saved/StanceLayer/New.csv' --require-copy --require-layer
~~~

下肢比较入口为 Scripts/Animation/Stance/compare_lower.py，参数依次为基线与当前 CSV。
基线可以使用 .csv.gz；输入时刻、场景或行数不同时直接失败。
完整输入和中间数据保留于 Saved/StanceLayer_20261114。
