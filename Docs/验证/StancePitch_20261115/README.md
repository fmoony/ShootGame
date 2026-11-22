# 俯仰解限与生产姿态差异定位

日期：2026-11-15；UE 5.8.4；沿用原 Stance 实验地图。

## 已实施

删除 ApplyPose 中两层实验俯仰裁剪：正负 85°，以及身体的 -45° 到 55°。
现在将控制器归一化后的实际 Pitch 直接交给既有 AimSpines。
没有修改引擎相机默认视角范围，也没有增加眼点、双肩或枪械位置补偿。
完整上身分层、Blender 腿部动画及 FP 最终姿态复制保持原职责。

受控采样改为 -90、-85、-30、0、30、85、90 度。
扩展至完整五节脊柱、两节颈骨、头、锁骨和双臂，共 17 个骨骼。
记录七个阶段，加相机及四个枪械变换，每采样帧 124 行。

本轮取消限制并定位差异，没有将生产 AimOffset 或 AimIK 接入实验。
放开角度不等于复现生产姿态，当前仍由纯脊柱公式驱动俯仰。

## 实验实际运行结果

Unclamped.csv.gz 是实际游戏运行后的 Body / FP 变换，不是截图推断。
固定 60 Hz，108 场景，每六帧采样，共 2,160 个采样帧。
覆盖站立、蹲姿、四向蹲走、Aim / LowReady 和偏航。

对比实际 Body 上胸在 layer 与最终阶段的旋转，得到：

- 最小俯仰约 -89.9999995°，最大约 +89.9999995°。
- 实际响应与控制器 Pitch 的最大误差约 0.00000388°。
- FP 与 Body 位置差为 0 cm。
- 完整上身相对相机与同视角站立参考的最大位置差约 0.000001 cm。
- 上身参考旋转差小于 0.000004°。

这些结果证明额外裁剪已取消，完整分层和 FP 复制仍成立。
不代表自然度、蒙皮穿插、精确瞄准或任意翻转角度已验收。

## 为什么无法得到生产的轻微后仰与独立手臂旋转

### 当前实验把全部俯仰交给脊柱

AimSpines 将 Pitch 按 0.12、0.16、0.20、0.24、0.28 分给五节脊柱，总和为 1。
手臂保持来源局部姿态，主要随脊柱父级运动。
左臂另有握持 Grip，但这不是生产的独立瞄准姿态。

实际运行抬头 90° 时，上胸累计旋转约 90°。
右上臂相对锁骨、右前臂相对上臂、右手相对前臂的旋转变化均为 0°。
因此看到的是身体带着手臂一起转，手臂缺少独立抬枪动作。
并非存在一个 UE 手臂关节限角，也不是完整上下身分层把手臂锁住。

### 生产通过 Mesh Space AimOffset 分配躯干与肩臂动作

官方 MCP 读回的 Rifle 数据表配置为：

- Hold：MF_Rifle_Idle_ADS。
- AimOffset：AO_Rifle。
- 向上样本：MM_Rifle_Idle_ADS_AO_CU。
- 向下样本：MM_Rifle_Idle_ADS_AO_CD_Fixed。
- 中间样本：MM_Rifle_Idle_ADS_AO_CC。

生产 AimPitchN=sin(Pitch)，向上 90° 对应 Y=1 的样本。
使用原生压缩动画求值，并按资源声明的 Additive Base 还原为完整姿态。
以下生产值相对中间样本计算，不是把实验公式换一组权重后算出的结果。

| 向上端点的变化 | 实验运行 +90° | 生产 AO 资源 |
| --- | ---: | ---: |
| 上胸组件朝向变化 | 90.00° | 17.31° |
| 右上臂相对锁骨 | 0.00° | 51.20° |
| 右前臂相对上臂 | 0.00° | 1.90° |
| 右手相对前臂 | 0.00° | 25.41° |

上胸 17.31° 是总旋转角，前向向量的俯仰投影约 16.36°。
生产资源让脊柱只轻微后仰，由上臂和手腕承担更多抬枪动作。
实验没有消费这些逐骨骼的 AO 轨道，所以单纯放宽脊柱角度不会得到同样效果。

生产后面还有 ShooterAimIK，根据枪口方向与目标方向修正右手，并由左手 IK 维持握持。
本地 SolveHandCorrection 仅在 MaxCorrectionAngle 大于 0 时额外限角。
本轮没有执行这些生产节点，不能将生产资源端点与完整生产运行结果混为一谈。

## 证据范围

实验数据来自实际 Body / FP 运行组件。
ProductionAO.json 来自当前生产资产的原生压缩端点求值，不是完整生产 AnimBP 或联机测试。
资源求值使用 RetrieveAdditiveAsFullPose，对照本地 AnimPose.cpp 的实际实现。
正式数据表、AO 样本与节点设置经官方 MCP 只读回读。
本轮没有修改或保存生产蓝图、动画资源、Skeleton、Blender 源文件或实验地图。

[官方 AimOffset 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/aim-offset-in-unreal-engine)
说明其使用 Mesh Space Additive 姿态。
本地 RotationOffsetBlendSpace 节点实际调用网格空间旋转增量累积。
这些资料用于核对生产规则；数值结论来自本项目的实际资源和实验运行数据。

## 文件与复测

- Unclamped.csv.gz、Unclamped.summary.json：17 骨完整运行采样与分层检查。
- Unclamped.pitch.json：实际脊柱角度、关节局部变化及生产参考对照。
- RifleRow.json、AimOffset.json：生产配置来源。
- ProductionAO.json：生产三个 AO 样本的原生完整姿态和逐骨差值。
- ProductionGraph.json：正式 AimOffset / AimIK 的只读设置。
- Protection.json、Manifest.json、BuildFinal.log、TextLayout.log：保护、来源和检查记录。

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureView -ViewOutput 'StancePitch/New.csv'
$engine = & './Scripts/Development/ResolveShootGameEngine.ps1'
$python = "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
& $python './Scripts/Animation/Stance/analyze_view.py' './Saved/StancePitch/New.csv' --require-copy --require-layer
~~~

analyze_pitch.py 的参数为运行 CSV 与 --production 指定的生产资源求值 JSON。
本轮完整输入与中间日志保留于 Saved/StancePitch_20261115。

## 遗留项

生产“轻微后仰、独立抬臂”尚未移植到实验。
后续应复用实际 AO 及后续瞄准链路，避免继续调整固定脊柱权重来替代动画。
现有枪口与视线仍有约 14.42° 的基准差，未标为精确瞄准通过。
脚掌平贴、启停穿地、生产接入及完整滑铲、网络、换弹、贴墙回归未处理。
