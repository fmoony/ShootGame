# 脚掌平贴与生产链路评估

2026-11-15；UE 5.8.4 工作树；实验地图 Stance_20261022/L_StancePreview。

## 结果

88 段移动动作已在 Blender 中改为支撑期整掌平贴，并经 UE 资产系统导入实验。
90 段资源包含两段不改轨道的 idle；两套 BlendSpace 仍各含 45 个样本。
本轮没有修改运行时 C++ 接地、骨盆补偿或膝盖求解。

以下均为最差单场景的百分位，覆盖实际样本方向及样本间方向。
旧值与新值使用同一新脚本和同一鞋底定义。

| 指标 | 旧轨道 | 本轮轨道 | 门禁 |
| --- | ---: | ---: | ---: |
| 支撑三点绝对高度 P99 / cm | 17.146 | 0.374 | ≤1 |
| 支撑三点高度差 P99 / cm | 17.353 | 0.244 | ≤0.6 |
| 鞋底法线倾角 P99 / ° | 57.179 | 0.804 | ≤2 |
| 固定前掌世界脚滑 P95 / cm/s | 3.691 | 3.602 | ≤5 |
| 稳定移动最低鞋底点 / cm | -1.037 | -0.308 | ≥-0.5 |
| 稳定移动双脚高于 1 cm 的帧 | 0 | 0 | 0 |
| 含启停的最低鞋底点 / cm | -3.456 | -3.832 | 未通过 |

启停过渡仍有穿地，且最低点略有加深；不得把稳定平脚通过写成全流程通过。
旧报告约 -2.92 cm 使用 ball / heel 口径，与本报告的鞋底三点不同。
自然度、膝盖姿态与鞋底蒙皮仍需人工验收，门禁没有替代这些结论。

## 原因与制作

旧判定只取 ball 骨与 heel 的最低值，脚尖低而后跟高仍能通过。
参考姿态 ball 骨原点本身约高于鞋底 0.75 cm，不能把骨原点直接当鞋底。

本轮以参考姿态 Z=0 平面定义后跟、前掌、外侧三个点，转换到 foot 局部空间。
在真实 Blender Action 中校平 foot，保留脚部朝向和原前掌的平面移动轨迹。
支撑期 ball 使用参考局部旋转；摆腿期平滑恢复原脚部姿态并抬脚。
按原膝盖弯曲平面离线求解大腿和小腿，烘焙至 Action、blend、FBX。
UE 导入只复制实际 bpy 求值轨道，不从目标点再次制作腿部。

两段 idle 的 Authored 轨道与修改前完全相等。
移动骨盆、躯干和上肢局部轨道保持原值，仅允许 Blender 浮点回读误差。
源 Action 中受保护局部位置误差最大 0.000075 cm，角度最大 0.000150°。
旧最低接触点生成工具已加版本保护，拒绝覆盖当前平脚源。

## 运行数据与门禁范围

Runtime.csv.gz 来自实际运行 Body 骨骼，每帧 14 行。
共 264 场景、43,808 帧，固定 60 FPS，包含 128 方向、两种持枪和六种压力场景。
前两场景为完整 idle，256 场景为移动，最后六场景记录动态压力。

从实际 foot 组件位置及四元数重建鞋底，再通过真实世界 foot 位置转换。
固定采集夹具 ActorYaw=0、MeshYaw=-90；用实际 heel 行验证该坐标假设。
后跟重建最大误差为 0.000001660 cm，腿部骨长最大误差为 0.000001472 cm。
这是骨骼参考鞋底平面，不是网格逐顶点碰撞或地面 Trace。

平贴指标检查 caseTime>0.5 秒、Mode=2 的稳定移动。
相位按左脚 phase、右脚 phase+0.5；支撑内部 0.03<phase<0.62 检查高度和倾角。
稳定移动最低点与双脚悬空检查覆盖全部相位，启停最低点另行记录。
idle 的跪姿后脚不要求整掌平贴。

RAW / COMPRESSED、循环、骨长及上身保护检查覆盖全部 90 段资源，通过。
Blender 文件 90 个 Action 回读位置误差最大 0.00001576 cm、角度 0.00005491°。
UE 资源与 bpy 轨道世界位置差最大 0.001360 cm。
运行骨盆、头、双手共 175,232 行对照旧运行，位置与旋转差均为 0。
21 项正式 Skeleton、网格、AnimBP、来源资产等哈希保持一致。
92 个实验包中仅 88 个移动动画包改变，两个 idle 与两个 BlendSpace 字节不变。
旧运行输入与上轮 Lower.csv.gz 解压后完全一致，校验值见 Baseline.json。

9 张 D3D12 截图仅辅助观察。
配套 1,055 行移动数据的方向、握持和 FP 不回写 TP 检查通过。

## 生产链路复刻评估

正式数据表、AO 样本和图设置沿用本任务前一阶段的只读证据：
[生产资源与俯仰对照](../StancePitch_20261115/README.md)。

当前差异是：实验在上身分层后把 Pitch 按固定权重转到五节脊柱，再执行实验 Grip。
生产读取实际 Hold 和 AO，AimPitchN=sin(Pitch)，AO 的 Y 轴取 -1 到 1。
AO 让脊柱只轻微后仰，由肩臂和手腕承担更多抬枪变化。
之后由 ShooterAimIK 处理右手枪口方向，由左手 IK 维持实际握把绑定。
FP 最后复制 TP 的最终姿态。

推荐下一阶段在实验中依序复用：

1. 正式武器配置中的 Hold / AimOffset 和实际样本。
2. 引擎 Mesh Space Rotation Additive 累积，替换固定脊柱 Pitch 分配。
3. 实际 ShooterAimIK 数学求解和枪口、握把绑定，再核对左手 IK。
4. 保留当前 Blender 下肢，最后复制 FP；逐阶段采集胸、肩、臂、手和枪口。

应以现有实验 Pawn 做最小表现验证，再决定是否迁移到正式 AnimBP。
直接替换 AnimClass 无法完成复刻：正式 AnimInstance 初始化需要 ShooterCharacter，
还要求实际武器归属、Mesh 附着与静态绑定；不满足时会清空表现绑定。
本轮只完成评估，没有把实验接入生产角色、装备权威或复制逻辑。

外部依据为 Epic 的 [Aim Offset 说明][aim] 和 [分层动画说明][layer]。
前者规定 Mesh Space Additive 的样本与基础姿态关系，后者说明按骨骼覆盖上身。
当前网页默认标注 5.9，具体执行按本地 UE 5.8.4 源码核对。
本地 AnimNode_RotationOffsetBlendSpace.cpp 的 Evaluate_AnyThread 先求 BasePose，
再调用 AccumulateMeshSpaceRotationAdditiveToLocalPose 并归一化旋转，与此规则一致。
项目现有固定脊柱权重不是该生产 AO 行为的等价实现。

[aim]: https://dev.epicgames.com/documentation/en-us/unreal-engine/aim-offset-in-unreal-engine
[layer]: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-layered-animations-in-unreal-engine

## 复测与文件

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode Verify
$engine = & './Scripts/Development/ResolveShootGameEngine.ps1'
$python = "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
$csv = './Saved/StanceFootFlat_20261115/Runtime.csv'
& $python './Scripts/Animation/Stance/analyze_flat_feet.py' $csv --require-flat-feet
~~~

新采集可使用 Run.ps1 -Mode CaptureGround，其默认路径见当前实验说明。
上述 csv 指向本轮已保存的完整运行输入，避免误用旧采集。
新制作从 blender_flat_feet.py 的 --output-dir 指定未存在的 Saved 目录开始。
export_blend_grounding.py 支持 --source-dir 与 --report，默认只读。

- Before.flat.json / Runtime.flat.json：旧数据负对照与本轮逐场景指标。
- Runtime.csv.gz：完整原始运行采样。
- FlatFeetSummary.json / BlendReadback.json：Blender 制作及源文件回读。
- VerifyBlender.json：UE 90 段资源回读。
- UpperProtection.json / Protection.json：运行上身与正式资产保护。
- MoveSummary.json / Images：辅助画面及移动检查。
- AssetChanges.json / Baseline.json：实验包变化与旧运行输入的来源校验。
- ReviewWindow.json：唯一验收编辑器的可见、未最小化窗口记录。
- TextLayout.log / Manifest.json：文本检查及源文件、脚本、证据校验值。

本轮文本通过独立临时索引的 Staged 版式检查及 git diff --check。
Run.ps1 的 ValidateSet 长参数提示一条人工核对警告，已按参数完整性核对。
Python 脚本语法检查通过；真实 Git 暂存区保持空白，未创建提交。

启停专用过渡、坡面、碰撞、网络、Cook、换弹、贴墙和完整滑铲回归未验收。
