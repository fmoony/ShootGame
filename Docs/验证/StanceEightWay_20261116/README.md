# 八向动作补齐与右后单点验证

日期：2026-11-16。UE 5.8.4 工作树；仅隔离 Stance 实验。
本轮替换五段制作资源，原始 Source、后/左/右轴向资产与生产代码保持不变。

## 最终布局

BS_SourceLayer_Lower 现在是中心 idle 加八个标准移动方向，共九个样本。

| 方向 | Forward | Right | 使用资源 |
|---|---:|---:|---|
| 前 | 1 | 0 | EightWay/CrouchForward |
| 右前 | 0.7071 | 0.7071 | EightWay/CrouchForwardRight |
| 右 | 0 | 1 | 原 SourceLayer/CrouchRight |
| 右后 | -0.7071 | 0.7071 | EightWay/CrouchBackRight |
| 后 | -1 | 0 | 原 SourceLayer/CrouchBack |
| 左后 | -0.7071 | -0.7071 | EightWay/CrouchBackLeft |
| 左 | 0 | -1 | 原 SourceLayer/CrouchLeft |
| 左前 | 0.7071 | -0.7071 | EightWay/CrouchForwardLeft |

旧 BackRight 目录三个样本引用全部退出 BlendSpace。
112.5°、157.5° 加密点已移除；135° 仅引用新的完整右后动作。
旧资产和 Source 文件保留作来源与历史对照，没有人工删除 uasset。

正前原先缺少独立动作，所以另外从既有前斜向来源制作了正前。
原轴向后、左、右的文件、坐标、速率均保留。
四个斜向分别有完整、可编辑的 blend 与独立 FBX，不再靠加密右后点凑数量。

## 制作方法

资源位于 SourceAssets/StanceCrouch/EightWay，UE 动画位于实验 EightWay 目录。
全部为 60 FPS、61 点、1 秒循环，直接绑定生产 SK_Mannequin。
它们是来源融合后制作的派生动作，不是新采集或未经调整的原始 Source。

正前以原左前、镜像右前为基础，制作双腿落脚。
四斜向分别取相邻轴向作为来源，使用离线双腿 IK 和原生短弧 NLerp 混合约束。
固定脚部组件空间目标朝向，约束两侧各 16 个混合权重的 foot/ball 高度。
再做四轮周期曲线连续性求解，处理帧间与循环接缝。
所有结果写入真实 Blender Action，并逐骨骼核对后导出 FBX。
数学制作工具使用固定 SciPy 1.16.3；依赖只在 Saved，不进入运行时。

TP、FP AnimBP 与 C++ 夹具没有修改。
运行时继续使用原生播放器、AO、Layered Blend 和 Copy Pose。
没有增加 C++ IK、骨骼回写、Trace 或逐帧接地处理。

## 最终验证

| 检查 | 结果 |
|---|---|
| 原始/生产/既有资源与 C++ 保护 | 288 文件 SHA256 一致 |
| 标准 FBX 导入与 Skeleton 指纹 | 通过，生产骨架未改变 |
| 原始与压缩姿态 | 通过，27,145 个骨骼帧 |
| 动作循环首尾 | 位置差为零，旋转仅浮点误差 |
| 压缩动画腿骨最大单帧旋转 | 约 12.98°，60 FPS；低于本轮 18° 制作门禁 |
| 八向两持枪、两 idle | 18 场景，1,840 帧 |
| 整圈 32 方向两持枪、两 idle | 66 场景，5,680 帧 |
| 整圈稳定 foot/ball 穿地 | 无低于 -1 cm 的采样帧 |
| 转入阶段前掌骨骼点 | 同样无低于 -1 cm 的采样帧 |
| 32 方向最低前掌骨骼点 | 约 +0.053 cm |
| 原回归点 101.25° | 从上一版约 -15.8 cm 改善为约 +0.053 cm |
| FP 世界位置复制误差 | 0 cm |
| 完整分层对仅下半身 | 下半身位置差 0 cm |
| 腿骨长度 | 最大误差约 0.0000013 cm |
| 非零俯仰回归 | 11 个角度通过，真实 AO 输入误差小于 2.2e-8 |

Loop、压缩误差、骨长与各方向详细值见 JSON，不以截图替代运行证据。
本轮没有 C++ 改动，复用已有编译夹具；不需要刷新 VS 工程。

## 本轮处理的失败

初版接触通过，但进一步逐帧审查发现约 75.6° 的腿骨旋转突变。
增加单向时间约束后，循环末尾仍出现约 51.6° 跳变，因此两版均未交付。
最终使用周期邻帧约束，单帧峰值降至约 13°，重新烘焙、导入并重跑全部验证。
Saved 保留中间版本以便追查；本报告的 Final8/Final32 为最终资源采集。

## 验证边界

接触指标是静止 Actor、平面 Z=0 下的 foot/ball 骨骼点高度。
32 方向通过不等于连续所有角度、鞋底每个顶点或地形碰撞全部通过。
制作旋转门禁用于排除明显抽动，不等价于主观自然度验收。
固定 150 cm/s 预览移动没有进行脚滑或步幅速度匹配验收。
原 idle 仍是单膝跪姿，其与半蹲行走的风格差异本轮未重做。
没有验收网络、坡面、完整生产角色生命周期或自定义生产 AimIK。

## 复测

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode Verify
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureNative -Stationary -PitchSweep
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureNative -Stationary -Directions 32 -CaptureOutput 'New/Full32.csv'
~~~

采集目标必须为 Saved 内的新文件。
author_eight_way.py、bake_eight_way.py 默认拒绝覆盖，重放时指定新输出目录。
import_eight_way.py 默认只允许新目录；EightWayReimportOwn 仅允许明确的本轮五个来源目标。
verify_eight_way.py 检查压缩姿态、循环、单帧旋转、布点、保护文件与原生图。
当前 Source、输入与导出选项见
[制作说明](../../../SourceAssets/StanceCrouch/EightWay/README.md)。

## 证据

Final8.eightway.json、Final32.eightway.json：最终运行方向与接触结果。
Final8.csv.gz、Final32.csv.gz、FinalRaw8.csv.gz：实际 TP/FP 骨骼与 AO 数据。
FinalLayerComparison.json：完整层与原始下半身对照。
VerifyEightWay.json、VerifyNative.json、Protection.json：磁盘重载、图与保护证据。
AuthorQuality.json、AuthorSummary.json、BakeSummary.json、Import.json：制作与导入。
BlendSpaceAfter.json：MCP 读回最终九个样本。
Manifest.json：最终来源、资产、工具与证据校验值。
Review.png：实际可视化 PIE，用于辅助验收。
