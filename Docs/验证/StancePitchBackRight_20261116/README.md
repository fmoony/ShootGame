# 俯仰归一化与右后方向资源验证

日期：2026-11-16。工作树：ShootGame_UE58；引擎：UE 5.8.4。
本报告只验收隔离实验，不代表生产角色、网络或全部连续方向通过。

## 实际改动

通过官方 MCP 将实验 TP 图的 Pitch → AO.Y 改为 Pitch → Sin(Degrees) → AO.Y。
Pitch 保留角度单位，AO 使用生产 AO_Rifle 的 [-1,1] 轴，与生产 AimPitchN 转换相同。
没有修改鼠标轴符号、增加俯仰裁剪或恢复任何 C++ 骨骼修正。

Blender 从 LayerPlayback/CrouchBack 派生三个独立方向：
112.5°、135°、157.5°，分别整体旋转 -67.5°、-45°、-22.5°。
它们是明确制作的派生动作，不是未经修改的 Source。
保留原后退局部骨骼曲线、垂直运动、骨长和周期，只改变全局朝向。
上半身仍由原生持枪层覆盖；标准 FBX 导入直接绑定生产 SK_Mannequin。

MCP 为 BS_SourceLayer_Lower 增加三个样本，原六个样本的动画、坐标和速率保留。
打开原生 BlendSpace 编辑器重建三角网格，确保重建结果实际保存到磁盘。
C++ 仅补采集输入、读取真实 AO 坐标，以及同步 Editor 建图工具的 Sin 节点。
TP/FP 仍为 Engine.AnimInstance；FP 仅原生 Copy Pose。

## 数据证据

| 检查 | 实际结果 |
|---|---|
| 原生图与保护文件检查 | Passed；273 个原始/生产/历史资源保持 SHA256 一致 |
| 项目 Editor 编译 | BuildV2 Succeeded |
| FBX 原始姿态对 Blender | 最大位置误差 0.000630 cm，角度误差 0.001884° |
| 压缩姿态对 Blender | 同样通过；共 8,277 个骨骼帧 |
| 非 root 局部姿态对原后退 | 最大位置误差 0.000145 cm，角度误差 0.000916° |
| 俯仰运行采集 | 18 场景、1,840 帧、TP/FP 各 89 根骨骼 |
| AO 实际输入误差 | 2.20e-8；11 个非零及零俯仰样本全部覆盖 |
| 头部实际输出 | idle Aim 两端平均朝向相差约 139°，并非只检查输入 |
| 完整层与下半身对照 | 下半身位置差 0 cm；旋转差约 0.0000035° |
| FP 最终姿态复制 | 世界位置差 0 cm；旋转只有浮点误差 |
| 腿部骨长 | 最大误差约 0.0000013 cm |
| 右后 135° | 两种持枪均通过；前掌最低 +0.410 / +0.367 cm |
| 右后进入帧 | 包含前 0.6 秒仍无负高度骨骼点 |
| 连续方向加密检查 | 32 方向 × 两持枪 + 两 idle，共 66 场景、5,680 帧 |
| 右后扇区 | 112.5、123.75、135、146.25、157.5° 均无超过 1 cm 的骨骼点穿地 |

俯仰输入为 -85、-45、-10、-1、-0.5、0、0.5、1、10、45、85°。
采集读取运行中原生 AO.GetPosition().Y，不以计算期望值替代实际值。
旧缺陷是直接输入角度，约 1° 就达到 AO 端点。
原先仅测 Pitch=0 的验收不足以覆盖这个问题。

右后原生基线前掌最低约 -16.39 cm，本轮稳定扇区最低约 +0.225 cm。
接触判定使用 foot/ball 骨骼高度、地面 Z=0、静止 Actor。
没有执行鞋底顶点碰撞、足底压力、移动脚滑、坡面或联机验收。

## 明确未解决的范围

全方向接触质量仍未通过。前向最低约 -3.83 cm、左后约 -6.99 cm。
32 方向也记录了其他混合方向问题，详见 Sector32.contact.json。
新增右后样本消除了目标八向中的右后混合，但右移与后退步态的兼容性仍有缺口：
在 90°～112.5° 过渡区，约 101.25° 前掌仍可达 -15.8 cm。
这是当前实际限制，不能把本轮结果描述为 360° 连续移动全部修复。
原始单膝跪姿 idle 与半蹲移动高度不匹配，本轮没有重做 idle。

## 排错与保存契约

官方 MCP 数组属性写入不会自动保证 BlendSpace 运行三角网格刷新。
只打开 BlendSpace 会重采样，但资产若不脏，save_assets 可成功返回而不写文件。
打开编辑器后重排数组还可能触发逐元素校验，删除中途重复的样本。
本轮出现过上述两种中间状态，均未作为最终结果交付。

最终在资产编辑器关闭时按原顺序追加缺失样本；
再打开原生编辑器重建，临时切换并恢复 TargetWeightInterpolationSpeedPerSec=0，
使资产标脏后保存。最终九个样本和磁盘重载均验证通过。
保存始终明确指定 TP 或 BlendSpace，没有 Save All。
Editor 建图工具曾因 BindInput 只接受 AnimGraphNode 而编译失败，
改为显式 VariableGet → Sin 调用后 BuildV2 通过。

## 复测入口

在项目根目录运行：

~~~powershell
& './Scripts/Animation/Stance/Run.ps1' -Mode Verify
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureNative -Stationary -PitchSweep
& './Scripts/Animation/Stance/Run.ps1' -Mode CaptureNative -Stationary -Directions 32 -CaptureOutput 'New/Sector.csv'
~~~

CaptureOutput 必须是 Saved 中尚不存在的文件。
bake_back_right.py 默认拒绝覆盖已有 BackRight 目录，可指定新的 --output-dir 重放。
import_back_right.py 的 -BackRightCommit 只导入三个全新目标，不覆盖既有资产。
verify_back_right.py 读取磁盘原始与压缩姿态，不保存资产。
保护范围不含本轮有意修改的 TP、BlendSpace 及当前来源 README。
没有创建、删除、移动 C++ 文件，也没有提交或操作 VS。

## 证据文件

- Pitch8.contact.json、Sector32.contact.json：最终运行统计与剩余问题。
- Pitch8.csv.gz、Sector32.csv.gz、Raw8.csv.gz：实际 89 骨 TP/FP 运行记录。
- LayerComparison.json：完整分层与原始下半身对照。
- ExtraRuntime.json：头部输出与含转入帧的右后最低点。
- VerifyNative.json、Protection.json：原生图审计及保护文件哈希。
- VerifyBackRight.json、Import.json、BakeSummary.json：制作、导入与压缩核对。
- PitchNodesAfter.json、PitchAfterReload.json、BlendSpaceFinal.json：MCP 读回。
- Manifest.json：本轮资产、来源和证据校验值。
- ReviewFP.png：实际可视化 PIE；不作为俯仰或接触通过的唯一证据。
