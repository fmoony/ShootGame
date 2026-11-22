# 左前与右后直接镜像制作

日期：2026-11-16；工作树 ShootGame_UE58，UE 5.8.4。
本轮按用户要求只制作镜像，不增加 IK、接地、轨迹求解或平滑。

## 实际落地

| 新资产 | 镜像来源 |
|---|---|
| Mirrored/AS_Mirrored_CrouchForwardLeft | EightWay/AS_EightWay_CrouchForwardRight |
| Mirrored/AS_Mirrored_CrouchBackRight | EightWay/AS_EightWay_CrouchBackLeft |

来源是当前已存在的右前、左后；没有重新执行它们之前的制作算法。
仅反射 Body 组件 X 轴，交换左右骨骼，按生产参考姿态转换骨骼轴约定。
脚部 IK 标记随骨骼一起镜像，没有单独求解标记或肢体。
保持来源 60 FPS、61 点、1 秒循环，逐帧一一对应，循环移相为零。
镜像不改变落脚高度、节奏或轨迹形状；浮点误差见 MirrorSummary.json。

新 blend/FBX/逐帧目标位于 SourceAssets/StanceCrouch/Mirrored。
标准 FbxFactory 导入，绑定生产 SK_Mannequin。
MCP 仅将 BS_SourceLayer_Lower 的左前、右后资产引用替换为新镜像。
坐标、速率、其余七个样本和八向单点布局保持原状。
没有修改 AnimBP、C++、生产骨架、来源动画或旧制作文件。

## 镜像保真与导入验证

- 308 个保护文件 SHA256 一致。
- Blender 原始来源与既有逐帧目标一致。
- 双重镜像返回原片段：位置差为零，旋转差约 0.00004°。
- 镜像对应骨骼的高度差小于 0.000004 cm；没有抬脚或接地修正。
- 原始/压缩导入对镜像目标：位置误差最大约 0.00320 cm，旋转约 0.0476°。
- 原生导入片段相对于来源对侧同一时刻：最大高度误差约 0.000246 cm。
- 两段共核对 10,858 个骨骼帧，循环位置差为零。
- 原生 TP/FP 图审计通过，仍无自写 C++ 姿态处理。

## 运行与网格复核

实际采集 32 方向、两持枪与两 idle，共 66 场景、5,680 帧。
FP 世界位置复制差为零。
使用上一轮只读导出的生产网格、权重与本轮实际运行骨骼重建鞋部表面，
并用三角形相交检测复核双脚，方法沿用 StanceFootAudit_20261116。

| 主方向 | 持枪 | 双脚表面相交帧 / 稳定采样帧 | 鞋部最低 Z |
|---|---|---:|---:|
| 左前 | Aim | 0 / 68 | -4.045 cm |
| 左前 | LowReady | 0 / 68 | -4.064 cm |
| 右后 | Aim | 0 / 68 | -5.057 cm |
| 右后 | LowReady | 0 / 68 | -5.105 cm |

旧左前为 7～9 帧互穿，旧右后为 11 帧互穿；本次主方向采样均未再发现。
镜像继承来源右前约 -4 cm、左后约 -5 cm 的鞋面穿地，没有宣称接地修复。

## 接触质量仍未通过

Full32.eightway.json 的状态为 Failed，不能用镜像保真通过代替接触通过。
主方向的 foot/ball 点保持正高度，但邻向原生混合仍会穿地：

- 右移→右后部分方向最低前掌约 -4.6 cm。
- 右后→后退约 157.5° 最低前掌约 -13.7 cm。
- 左侧与左前、左前与前向之间也有约 -3～-6.9 cm 的骨骼点穿地。

原轴向动作并非严格左右镜像；直接交换左右脚后，与原轴向的支撑相位和姿态兼容性不同。
本轮没有额外改脚步、轴向来源、混合策略或时间轴。
只读半周期对照也未消除全部问题，因而未把这种变更加入本轮资源。
本轮交付的是用户指定的直接镜像版本，不是完整接触质量验收通过的版本。

## 工具与复测

mirror_stance.py 只依赖 bpy/mathutils 和已有坐标转换，拒绝覆盖已有输出。
没有导入 SciPy、优化工具或旧腿部制作模块；源骨架必须没有未烘焙约束。
import_mirrored.py 只允许新目标，保存时仅保存两个新动画。
verify_mirrored.py 核验保真、循环、标记、同一时刻的镜像高度及两处引用替换。
Run.ps1 的 Verify 现指向该资源契约检查，不代表网格接触全部通过。

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode Verify
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureNative -Stationary -Directions 32 -CaptureOutput 'New/Mirror32.csv'
~~~

本轮没有 C++ 变更，复用已有原生夹具，无需编译或刷新 VS。
可视化 UE 5.8 与实验 PIE 已恢复供人工检查；主观自然度仍需用户验收。

## 证据

MirrorSummary.json、Import.json、VerifyMirrored.json：镜像与导入结果。
BlendSpaceBefore.json、BlendSpaceAfter.json：仅两个样本引用变化的 MCP 读回。
VerifyNative.json、Protection.json：原生图与保护文件。
Full32.csv.gz、Full32.eightway.json：实际运行记录和接触检查失败明细。
SkinAudit.json：主方向鞋部表面高度与双脚相交统计。
PhaseProbe.json：只读相位对照，未用于制作。
Manifest.json：最终资产、来源、工具与证据校验值。
